#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify primary-clock CPU/native bridge API and installed ZipGS rates."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from test_apple_iigs_ports import machine_sources

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/core-gs3/external-cpu-tests')
    args=parser.parse_args();build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    env=os.environ.copy()
    if os.name=='nt':
        env['PATH']=str(runtime)+os.pathsep+env.get('PATH','')
        env['VERILATOR_ROOT']=(runtime.parent/'share/verilator').as_posix()
    def tool(name):return runtime/(name+'.exe') if os.name=='nt' else shutil.which(name)
    def run(command,name):
        log=build/(name+'.log')
        with log.open('w') as stream:
            result=subprocess.run([str(p).replace('\\','/') for p in command],cwd=build,env=env,stdout=stream,stderr=subprocess.STDOUT)
        output=log.read_text(errors='replace')
        if result.returncode or re.search(r'%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed',output):
            raise ValueError(name+' failed:\n'+output[-14000:])
        return output
    sources=machine_sources();benches=[ROOT/'hdl/sim'/name for name in ('tb_gs_zipgs_speed.sv','tb_gs_external_cpu.sv','tb_gs_native_event.sv')]
    captured=sources+benches+[Path(__file__).resolve(),ROOT/'scripts/port_apple_iigs_core.py',ROOT/'scripts/test_apple_iigs_ports.py']
    def hashes():return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in captured}
    before=hashes();results=[]
    for bench in benches:
        top=bench.stem;directory=build/top
        selected=[ROOT/'hdl/core/apple_iigs/ports/zipgs_regs.sv'] if 'zipgs' in top else sources
        run([tool('verilator_bin') if os.name=='nt' else tool('verilator'),'--cc','--exe','--main','--timing','-Wno-fatal','--top-module',top,
            '-I'+str(ROOT/'third_party/apple-iigs/upstream/rtl'),'--Mdir',directory,*selected,bench],top+'-generate')
        executable=directory/('V'+top+('.exe' if os.name=='nt' else ''))
        if os.name=='nt':
            include=runtime.parent/'share/verilator/include'
            run([tool('g++'),'-std=c++20','-O1','-pthread','-DVL_TIME_CONTEXT','-I'+str(include),'-I'+str(include/'vltstd'),'-I'+str(directory),
                *sorted(directory.glob('*.cpp')),include/'verilated.cpp',include/'verilated_timing.cpp',include/'verilated_threads.cpp','-o',executable],top+'-compile')
        else:run([tool('make'),'-C',directory,'-f','V'+top+'.mk','-j8'],top+'-compile')
        output=run([executable],top+'-simulate');print(output,flush=True);results.append(output)
        if top=='tb_gs_native_event':
            output=run([executable,'+DELAYED_NATIVE'],top+'-delayed-simulate');print(output,flush=True);results.append(output)
    if hashes()!=before:raise ValueError('Inputs changed during external CPU qualification')
    (build/'validation.json').write_text(json.dumps({'schema_version':1,'files':before,'results':results,
        'limits':['Ideal byte memory API test, not the production cache/scheduler or hardware timing.','No Apple system ROMs or private ROM hashes are used.','P65C816 and microcode ISA unchanged from prior independent 5.12M-vector qualification.']},indent=2)+'\n')
    return 0

if __name__=='__main__':raise SystemExit(main())
