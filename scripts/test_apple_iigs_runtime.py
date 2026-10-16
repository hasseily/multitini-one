#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run production IIgs transport/cache tests with exact input hashes."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/core-gs/runtime-tests')
    parser.add_argument('--negative-controls',action='store_true',
        help='also prove the cache bench fails without the retire-slot hit block or the staged-commit hit block')
    args=parser.parse_args();BUILD=args.build_dir.resolve()
    BUILD.mkdir(parents=True,exist_ok=True)
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--']
    checks=[];files={}
    for name in ('cache','loader','ddr'):
        bench=ROOT/f'hdl/sim/tb_apple_iigs_{name}.sv'
        if not bench.exists():continue
        hdl=[ROOT/f'hdl/core/apple_iigs/multitini_apple_iigs_{name if name!="ddr" else "ddr_path"}.sv',bench]
        # The DDR path runs its AXI master on the machine clock; only the
        # drain summary crosses into control_clk through cdc_bit_sync.
        if name=='ddr':hdl += [ROOT/'third_party/appletini/hdl/cdc_bit_sync.sv']
        files.update({p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in hdl})
        out=BUILD/f'{name}.vvp'
        for stage,cmd in [('compile',['C:/msys64/ucrt64/bin/iverilog.exe','-g2012','-s',f'tb_apple_iigs_{name}','-o',out,*hdl]),('simulate',['C:/msys64/ucrt64/bin/vvp.exe',out])]:
            p=subprocess.run(guard+list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True)
            (BUILD/f'{name}-{stage}.log').write_text(p.stdout+p.stderr)
            if p.returncode:raise RuntimeError(f'{name}/{stage}: '+(p.stdout+p.stderr)[-8000:])
            if stage=='simulate':
                if 'PASS:' not in p.stdout:raise RuntimeError('No PASS: '+p.stdout)
                # The DDR bench must cover the stop-proof drain window and
                # machine reset in the middle of AXI transactions.
                if name=='ddr' and not all(k in p.stdout for k in
                        ('drain only after the core reset request reached the machine','mid-AW/W/B/AR/burst')):
                    raise RuntimeError('DDR bench coverage missing: '+p.stdout)
                print(p.stdout.strip());checks.append(name)
    negatives=[]
    if args.negative_controls:
        cache=ROOT/'hdl/core/apple_iigs/multitini_apple_iigs_cache.sv';bench=ROOT/'hdl/sim/tb_apple_iigs_cache.sv'
        mutations=(('retire_slot_hit','&& tag_hit && !rs_valid &&','&& tag_hit &&',
                    'same-word read hit while the retire slot was pending'),
                   ('staged_commit_hit',' &&\n        !commit_staged;',';','CPU read hit during the staged DMA commit'))
        for name,before,after,message in mutations:
            text=cache.read_text(encoding='utf-8')
            if text.count(before)!=1:raise RuntimeError('negative-control mutation anchor changed: '+name)
            directory=BUILD/('negative-'+name);directory.mkdir(exist_ok=True)
            mutated=directory/cache.name;mutated.write_text(text.replace(before,after),encoding='utf-8',newline='\n')
            out=directory/'cache.vvp'
            p=subprocess.run(guard+list(map(str,['C:/msys64/ucrt64/bin/iverilog.exe','-g2012','-s','tb_apple_iigs_cache','-o',out,mutated,bench])),
                cwd=ROOT,capture_output=True,text=True)
            if p.returncode:raise RuntimeError(f'negative {name}/compile: '+(p.stdout+p.stderr)[-8000:])
            p=subprocess.run(guard+list(map(str,['C:/msys64/ucrt64/bin/vvp.exe',out])),cwd=ROOT,capture_output=True,text=True)
            log=BUILD/f'negative-{name}.log';log.write_text(p.stdout+p.stderr)
            if 'PASS:' in p.stdout or message not in p.stdout+p.stderr:
                raise RuntimeError('negative control did not fail the intended check: '+name)
            negatives.append({'mutation':name,'expected_failure':message,'log_sha256':hashlib.sha256(log.read_bytes()).hexdigest()})
            print(f'negative control {name}: failed as intended')
    for name,digest in files.items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:raise RuntimeError('Inputs changed: '+name)
    (BUILD/'validation.json').write_text(json.dumps({'checks':checks,'files':files,'negative_controls':negatives,
        'negative_controls_run':args.negative_controls},indent=2)+'\n')
    return 0
if __name__=='__main__':raise SystemExit(main())
