#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the pinned exact sampler reference and original UA behavior model."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import ua_model

ROOT=Path(__file__).resolve().parents[1]


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/c64-ultimate-audio')
    parser.add_argument('--vivado',type=Path,default=Path('C:/Xilinx/2025.2/Vivado'))
    parser.add_argument('--reference-only',action='store_true')
    args=parser.parse_args()
    build=args.build_dir.resolve()
    if build==(ROOT/'build').resolve() or not build.is_relative_to((ROOT/'build').resolve()):
        parser.error('Use a dedicated build/ child')
    build.mkdir(parents=True,exist_ok=True)
    base=ROOT/'third_party/ultimate_reference'
    manifest=json.loads((base/'import_manifest.json').read_text())
    if manifest['commit']!=ua_model.REFERENCE_COMMIT: raise ValueError('Reference commit mismatch')
    imported=[base/'upstream'/row['path'] for row in manifest['files']]
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    for row,path in zip(manifest['files'],imported,strict=True):
        data=path.read_bytes()
        if sha(path)!=row['sha256'] or hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()!=row['git_blob_sha1']:
            raise ValueError('Exact upstream file changed: '+str(path))
    order=['fpga/ip/busses/vhdl_source/io_bus_pkg.vhd','fpga/ip/busses/vhdl_source/mem_bus_pkg.vhd',
           'fpga/io/sampler/vhdl_source/sampler_pkg.vhd','fpga/io/sigma_delta_dac/vhdl_source/my_math_pkg.vhd',
           'fpga/ip/sync_fifo/vhdl_source/sync_fifo.vhd','fpga/ip/srl_fifo/vhdl_source/srl_fifo-generic.vhd',
           'fpga/io/sampler/vhdl_source/sampler_regs.vhd','fpga/io/sampler/vhdl_source/sampler_accu.vhd',
           'fpga/io/sampler/vhdl_source/sampler2.vhd']
    bridge=ROOT/'hdl/sim/c64_ua_upstream_bridge.vhd'
    bench=ROOT/'hdl/sim/tb_c64_ua_upstream_ref.sv'
    model_tests=ROOT/'tests/test_c64_ua_model.py'
    inputs=[Path(__file__).resolve(),ROOT/'scripts/ua_model.py',model_tests,bridge,bench,base/'import_manifest.json',*imported]
    hashes=lambda:{p.relative_to(ROOT).as_posix():sha(p) for p in inputs}
    before=hashes()
    result={'status':'FAIL','reference_commit':manifest['commit'],'source_sha256':before,'checks':[],
            'limits':'Exact upstream test-only VHDL, original flattened IO/memory driver and behavioral model. '
                     'Checks observable byte-fetch timing/order and IRQ, with fractional phase inferred across '
                     'control stops. No physical C64U clock measurement or Multitini product/board proof.'}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'}
    env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+os.environ.get('PATH',os.environ.get('Path',''))
    def run(label: str,tool: str,*arguments:object)->str:
        executable=args.vivado/'bin'/(tool+('.bat' if os.name=='nt' else ''))
        child=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',str(executable),*map(str,arguments)],
            cwd=build,env=env,capture_output=True,text=True,errors='replace',timeout=300,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name=='nt' else 0)
        output=child.stdout+child.stderr
        (build/(label+'.log')).write_text(output)
        if child.returncode or re.search(r'\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b',output,re.I):
            raise RuntimeError(label+' failed: '+output[-10000:])
        return output
    try:
        model_run=subprocess.run([sys.executable,str(model_tests)],cwd=ROOT,env=env,capture_output=True,text=True,timeout=60)
        (build/'model.log').write_text(model_run.stdout+model_run.stderr)
        if model_run.returncode:raise RuntimeError('UA model contract tests failed: '+model_run.stderr)
        run('reference-vhdl','xvhdl','--nolog','--2008',*[base/'upstream'/p for p in order],bridge)
        run('reference-sv','xvlog','--nolog','--sv',bench)
        for clock,top in ((100000000,'tb_c64_ua_upstream_ref'),(50000000,'tb_c64_ua_upstream_ref_50'),(66666667,'tb_c64_ua_upstream_ref_66')):
            snapshot=top+'_sim'
            run(str(clock)+'-elaborate','xelab','--nolog','--timescale','1ns/1ps','--snapshot',snapshot,top)
            output=run(str(clock)+'-simulate','xsim','--nolog',snapshot,'--runall')
            if f'PASS: UA exact upstream reference clock={clock} cases=21' not in output:
                raise RuntimeError('Missing upstream completion')
            trace=build/'upstream_trace.csv'
            rows=list(csv.DictReader(trace.read_text().splitlines()))
            counts=ua_model.verify_upstream(rows,clock)
            corrupted=[dict(row) for row in rows]
            target=next(row for row in corrupted if row['event']=='FETCH')
            target['cycle']=str(int(target['cycle'])+1)
            try:ua_model.verify_upstream(corrupted,clock)
            except AssertionError:pass
            else:raise RuntimeError('Timing corruption negative control was accepted')
            trace.replace(build/(str(clock)+'-trace.csv'))
            result['checks'].append({'clock_hz':clock,**counts,'shifted_fetch_rejected':True})
            print('PASS: exact upstream sampler',clock,counts,flush=True)
        if hashes()!=before: raise RuntimeError('Sources changed during validation')
        result.update(status='PASS',sources_unchanged=True)
    except Exception as error:
        result['error']=str(error)
        raise
    finally:
        (build/'reference_validation.json').write_text(json.dumps(result,indent=2)+'\n')
    if not args.reference_only:
        aggregate={'status':'FAIL','reference_commit':manifest['commit'],'checks':[],
            'limits':'Automated reference, RTL, bounded DDR, spectrum and original probe control-path gates. Physical board gates remain separate.'}
        jobs=[('rtl','test_c64_ua_rtl.py',[]),('contention','test_c64_ua_contention.py',[]),
              ('spectrum-pal','test_c64_ua_spectral.py',['--confirm-rtl','--clock-hz','31524725']),
              ('spectrum-ntsc','test_c64_ua_spectral.py',['--confirm-rtl','--clock-hz','32727273'])]
        try:
            for label,script,extra in jobs:
                child=subprocess.run([sys.executable,str(ROOT/'scripts'/script),'--build-dir',str(build/label),*extra],
                    cwd=ROOT,env=env,capture_output=True,text=True,timeout=1800)
                (build/(label+'.log')).write_text(child.stdout+child.stderr)
                if child.returncode:raise RuntimeError(label+' gate failed: '+child.stderr[-2000:])
                receipt=build/label/'validation.json';record=json.loads(receipt.read_text())
                if record['status']!='PASS':raise RuntimeError(label+' receipt did not pass')
                aggregate['checks'].append({'gate':label,'receipt':str(receipt.relative_to(build)),'sha256':sha(receipt)})
                print('PASS: UA aggregate '+label,flush=True)
            child=subprocess.run([sys.executable,'-m','unittest','tests.test_c64ua'],cwd=ROOT,env=env,capture_output=True,text=True,timeout=300)
            (build/'board-probes.log').write_text(child.stdout+child.stderr)
            if child.returncode:raise RuntimeError('Board probe control-path tests failed')
            aggregate['checks'].append({'gate':'original board probe assembly and 6502 control paths','log_sha256':sha(build/'board-probes.log')})
            aggregate['reference_receipt_sha256']=sha(build/'reference_validation.json')
            aggregate['status']='PASS'
        except Exception as error:aggregate['error']=str(error);raise
        finally:(build/'validation.json').write_text(json.dumps(aggregate,indent=2)+'\n')
    return 0


if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,AssertionError,RuntimeError,subprocess.SubprocessError) as error:
        print('ERROR: '+str(error),file=sys.stderr)
        raise SystemExit(1)
