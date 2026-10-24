#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Fresh, source-captured component tests of production CPU-island RTL."""
from __future__ import annotations
import argparse, hashlib, json, os
from pathlib import Path
import re, shutil, subprocess, sys

ROOT=Path(__file__).resolve().parents[1]
HERE=ROOT
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,required=True)
    p.add_argument('--test',choices=['memory','map','nmi','governor','proxy','regs','guard','window'],action='append')
    a=p.parse_args(); out=a.build_dir.resolve()
    if not out.is_relative_to(ROOT/'build') or out==ROOT/'build' or out.exists():
        p.error('a fresh child directory of build/ is required')
    out.mkdir(parents=True)
    roles={'memory':'island_memory','map':'island_map','nmi':'nmi_delivery',
           'governor':'island_governor','proxy':'cpu_proxy','regs':'turbo_regs','guard':'cpu_turbo_guard','window':'turbo_regs'}
    tests=a.test or list(roles)
    sources=[Path(__file__),ROOT/'scripts/run_native_tool.py']
    for name in tests:
        sources += [ROOT/f'hdl/core/c64/multitini_c64_{roles[name]}.sv', ROOT/f'hdl/sim/tb_c64_island_{name}.sv']
    receipt={'status':'FAIL','scope':'CPU-island component RTL, not full-machine, E1, or hardware qualification',
             'tests':{},'source_sha256':{},'sources_unchanged':False}
    try:
        for src in sources:
            if not src.is_file(): raise RuntimeError(f'missing source {src}')
            key=src.relative_to(ROOT).as_posix();receipt['source_sha256'][key]=digest(src)
            dst=out/'sources'/key;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
        suffix='.exe' if os.name=='nt' else ''
        runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
        guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--cwd',str(out),'--']
        for name in tests:
            snapshot=out/'sources'/HERE.relative_to(ROOT)
            exe=out/f'{name}.vvp'
            commands=[['iverilog','-g2012','-s',f'tb_island_{name}','-o',str(exe),
                       str(snapshot/f'hdl/core/c64/multitini_c64_{roles[name]}.sv'),str(snapshot/f'hdl/sim/tb_c64_island_{name}.sv')],
                      ['vvp',str(exe)]]
            logs=[]
            for i,command in enumerate(commands):
                command[0]=str(runtime/(command[0]+suffix))
                result=subprocess.run(guard+command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                                      text=True,errors='replace',timeout=120)
                log=out/f'{name}-{i}.log';log.write_text(result.stdout,encoding='utf-8');logs.append(log.name)
                if result.returncode: raise RuntimeError(f'{name} command failed: {log}')
            match=re.search(rf'{name.upper()} PASS checks=(\d+)',result.stdout)
            if not match: raise RuntimeError(f'{name} PASS signature absent')
            receipt['tests'][name]={'status':'PASS','checks':int(match[1]),'logs':logs}
        receipt['sources_unchanged']=all(digest(ROOT/k)==v for k,v in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']: raise RuntimeError('source changed during test')
        receipt['status']='PASS'
    except Exception as e: receipt['error']=str(e)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(receipt,indent=2))
    return 0 if receipt['status']=='PASS' else 1
if __name__=='__main__': raise SystemExit(main())
