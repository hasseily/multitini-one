#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run production IIgs transport/cache tests with exact input hashes."""
import argparse,hashlib,json,os,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/core-gs/runtime-tests')
    BUILD=parser.parse_args().build_dir.resolve()
    BUILD.mkdir(parents=True,exist_ok=True)
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--']
    checks=[];files={}
    for name in ('cache','loader','ddr'):
        bench=ROOT/f'hdl/sim/tb_apple_iigs_{name}.sv'
        if not bench.exists():continue
        hdl=[ROOT/f'hdl/core/apple_iigs/multitini_apple_iigs_{name if name!="ddr" else "ddr_path"}.sv',bench]
        if name=='ddr':hdl += [ROOT/'hdl/common/multitini_memory_cdc.sv',ROOT/'hdl/common/multitini_async_mailbox.sv',ROOT/'third_party/appletini/hdl/cdc_bit_sync.sv',ROOT/'hdl/sim/xilinx_primitives.sv']
        files.update({p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in hdl})
        out=BUILD/f'{name}.vvp'
        for stage,cmd in [('compile',['C:/msys64/ucrt64/bin/iverilog.exe','-g2012','-s',f'tb_apple_iigs_{name}','-o',out,*hdl]),('simulate',['C:/msys64/ucrt64/bin/vvp.exe',out])]:
            p=subprocess.run(guard+list(map(str,cmd)),cwd=ROOT,capture_output=True,text=True)
            (BUILD/f'{name}-{stage}.log').write_text(p.stdout+p.stderr)
            if p.returncode:raise RuntimeError(f'{name}/{stage}: '+(p.stdout+p.stderr)[-8000:])
            if stage=='simulate':
                if 'PASS:' not in p.stdout:raise RuntimeError('No PASS: '+p.stdout)
                print(p.stdout.strip());checks.append(name)
    for name,digest in files.items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=digest:raise RuntimeError('Inputs changed: '+name)
    (BUILD/'validation.json').write_text(json.dumps({'checks':checks,'files':files},indent=2)+'\n')
    return 0
if __name__=='__main__':raise SystemExit(main())
