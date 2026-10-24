#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify the production W5100S bridge against an indirect parallel-bus model."""
import argparse, hashlib, json, os, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args()
    b=a.build_dir.resolve()
    if not b.is_relative_to(ROOT/'build') or b.exists():raise ValueError('fresh build subdirectory required')
    b.mkdir(parents=True)
    files=['hdl/board/multitini_w5100s_bridge.sv','hdl/sim/tb_multitini_w5100s_bridge.sv',
           'third_party/appletini/hdl/cdc_bit_sync.sv','scripts/test_w5100s_bridge.py','scripts/run_native_tool.py']
    sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    result={'status':'RUNNING','source_sha256':{f:sha(ROOT/f) for f in files},
        'limits':'Functional bus simulation only; routed IOB placement/timing and physical W5100S operation are separate gates.'}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'}
    env['PATH']='C:/msys64/ucrt64/bin;'+os.environ.get('PATH',os.environ.get('Path',''))
    def run(name,args):
        cmd=[str(x) for x in args]
        if os.name=='nt':cmd=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*cmd]
        r=subprocess.run(cmd,cwd=b,env=env,capture_output=True,text=True,timeout=60)
        out=r.stdout+r.stderr;(b/(name+'.log')).write_text(out)
        if r.returncode:raise RuntimeError(name+': '+out)
        return out
    try:
        run('compile',['C:/msys64/ucrt64/bin/iverilog.exe','-g2012','-s','tb_multitini_w5100s_bridge','-o',b/'sim.vvp',*[ROOT/f for f in files[:3]]])
        out=run('simulate',['C:/msys64/ucrt64/bin/vvp.exe',b/'sim.vvp'])
        if 'PASS W5100S bridge:' not in out:raise RuntimeError('missing PASS')
        result['checks']=out.strip().splitlines();result['status']='PASS';print(out)
    except Exception as e:result.update(status='FAIL',error=str(e));raise
    finally:(b/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
if __name__=='__main__':main()
