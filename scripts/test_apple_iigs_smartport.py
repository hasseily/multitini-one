#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify the native eight-unit SmartPort controller without private assets."""
import argparse,hashlib,json,subprocess,sys,os,shutil,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,default=ROOT/'build/iigs-smartport/controller')
    a=p.parse_args();out=a.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    paths=[ROOT/'hdl/core/apple_iigs/ports'/n for n in ('hdd.sv','bram.sv','runtime_rom.sv')]
    paths += [ROOT/'hdl/sim/tb_apple_iigs_smartport.sv']
    tracked=paths+[Path(__file__).resolve(),ROOT/'scripts/port_apple_iigs_core.py']
    hashes=lambda:{x.relative_to(ROOT).as_posix():hashlib.sha256(x.read_bytes()).hexdigest() for x in tracked}
    before=hashes();runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    env=os.environ.copy();env['PATH']=str(runtime)+os.pathsep+env.get('PATH','')
    env['VERILATOR_ROOT']=(runtime.parent/'share/verilator').as_posix()
    tool=lambda name:str(runtime/(name+'.exe')) if os.name=='nt' else shutil.which(name)
    obj=out/'obj';top='tb_apple_iigs_smartport';exe=obj/('V'+top+('.exe' if os.name=='nt' else ''))
    def run(cmd,stage):
        r=subprocess.run([str(x).replace('\\','/') for x in cmd],cwd=ROOT,text=True,capture_output=True,env=env)
        text=r.stdout+r.stderr;(out/(stage+'.log')).write_text(text,encoding='utf-8')
        if r.returncode or re.search(r'%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):',text):raise ValueError(stage+': '+text[-6000:])
        return text
    run([tool('verilator_bin' if os.name=='nt' else 'verilator'),'--cc','--exe','--main','--timing','-Wno-fatal','--top-module',top,'--Mdir',obj,*paths],'generate')
    if os.name=='nt':
        inc=runtime.parent/'share/verilator/include'
        run([tool('g++'),'-std=c++20','-O1','-pthread','-DVL_TIME_CONTEXT','-I'+str(inc),'-I'+str(inc/'vltstd'),'-I'+str(obj),
            *sorted(obj.glob('*.cpp')),inc/'verilated.cpp',inc/'verilated_timing.cpp',inc/'verilated_threads.cpp','-o',exe],'compile')
    else:run([tool('make'),'-C',obj,'-f','V'+top+'.mk','-j8'],'compile')
    text=run([exe],'simulate')
    checks=[x for x in text.splitlines() if x.startswith('PASS:')]
    if len(checks)!=6:raise ValueError('Missing SmartPort groups: '+text)
    if hashes()!=before:raise ValueError('Sources changed during qualification')
    (out/'validation.json').write_text(json.dumps({'schema_version':1,'status':'PASS','files':before,'checks':checks,
        'limits':['Synthetic controller fixture; full adapter, routed timing and physical hardware are separate gates.']},indent=2)+'\n')
    print(text);return 0
if __name__=='__main__':raise SystemExit(main())
