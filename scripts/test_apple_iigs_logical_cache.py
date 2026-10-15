#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify raw-address IIgs cache coherency, including rejected broken variants."""
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

def replace_once(text: str,old: str,new: str) -> str:
    if text.count(old)!=1:raise ValueError('Mutation anchor changed: '+old)
    return text.replace(old,new)

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/core-gs3/logical-cache-final-tests')
    parser.add_argument('--hdd-rom',type=Path,default=ROOT/'build/apple-iigs-assets/hdd.rom')
    parser.add_argument('--skip-negative-controls',action='store_true',help='run only the production positive fixture')
    args=parser.parse_args();build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    rom=args.hdd_rom.read_bytes()
    if len(rom)!=256:raise ValueError('Licensed assembled HDD ROM must be256 bytes')
    fixture=build/'hdd.hex';fixture.write_text('\n'.join(f'{b:02x}' for b in rom)+'\n')
    runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    env=os.environ.copy()
    if os.name=='nt':
        env['PATH']=str(runtime)+os.pathsep+env.get('PATH','')
        env['VERILATOR_ROOT']=(runtime.parent/'share/verilator').as_posix()
    def tool(name):return runtime/(name+'.exe') if os.name=='nt' else shutil.which(name)
    def run(command,name,expected_failure=None):
        log=build/(name+'.log')
        with log.open('w') as stream:
            result=subprocess.run([str(p).replace('\\','/') for p in command],cwd=build,env=env,stdout=stream,stderr=subprocess.STDOUT)
        output=log.read_text(errors='replace')
        if expected_failure:
            if result.returncode==0 or expected_failure not in output:
                raise ValueError('Negative control did not reject its exact defect: '+name+'\n'+output[-10000:])
        elif result.returncode or re.search(r'%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed',output):
            raise ValueError(name+' failed:\n'+output[-14000:])
        return output,result.returncode
    core=ROOT/'hdl/core/apple_iigs';adapter=core/'multitini_apple_iigs_adapter.sv'
    bench=ROOT/'hdl/sim/tb_apple_iigs_logical_cache.sv';top=bench.stem
    sources=machine_sources()+[core/(name+'.sv') for name in ('multitini_apple_iigs_loader','multitini_apple_iigs_cache','multitini_apple_iigs_adapter')]+[bench]
    sources += [ROOT/"hdl/sim/apple_iigs_bufgce.sv"]
    captured=sources+[ROOT/'third_party/apple-iigs/upstream/rtl/dhr_lut.vh',ROOT/'scripts/test_apple_iigs_ports.py',ROOT/'scripts/port_apple_iigs_core.py',ROOT/'scripts/capture_vectrex_build_inputs.py',Path(__file__).resolve()]
    def hashes():return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in captured}
    before=hashes();original=adapter.read_text(encoding='utf-8')
    variants=[('production',original,None)]
    if not args.skip_negative_controls:
        vector=replace_once(original,'reg [17:0] l0_tags[0:7];','reg [18:0] l0_tags[0:7];')
        vector=replace_once(vector,'wire l0_hit=cpu_valid && cpu_vector_n && l0_valid[l0_index] && l0_tags[l0_index]==cpu_logical_addr[23:6] &&','wire l0_hit=cpu_valid && l0_valid[l0_index] && l0_tags[l0_index]=={cpu_vector_n,cpu_logical_addr[23:6]} &&')
        vector=replace_once(vector,'else if(access_valid && !cache_stall && cpu_vector_n)begin','else if(access_valid && !cache_stall)begin')
        vector=replace_once(vector,'l0_tags[l0_index]<=cpu_logical_addr[23:6];','l0_tags[l0_index]<={cpu_vector_n,cpu_logical_addr[23:6]};')
        config=replace_once(original,' || config_commit ||\n',' ||\n')
        config=replace_once(config,' && !config_commit;',';')
        valid=replace_once(original,'wire l0_hit=cpu_valid && cpu_vector_n &&','wire l0_hit=cpu_vector_n &&')
        variants += [('vpb-tag-only',vector,'Mapping data addr00ffea vector0 actual66 expected55'),
                     ('missing-config-commit',config,'Mapping data addrffe000 vector1 actual99 expected88'),
                     ('missing-raw-valid',valid,'Invalid-cycle raw cache validity gating failed')]
    controls=[];positive=None
    for name,text,expected in variants:
        selected=sources
        if expected:
            source=build/(name+'.sv');source.write_text(text,encoding='utf-8',newline='\n')
            selected=[source if p==adapter else p for p in sources]
        directory=build/name
        run([tool('verilator_bin') if os.name=='nt' else tool('verilator'),'--cc','--exe','--main','--timing','-Wno-fatal','--top-module',top,'-I'+str(ROOT/'third_party/apple-iigs/upstream/rtl'),'--Mdir',directory,*selected],name+'-generate')
        executable=directory/('V'+top+('.exe' if os.name=='nt' else ''))
        if os.name=='nt':
            include=runtime.parent/'share/verilator/include'
            run([tool('g++'),'-std=c++20','-O1','-pthread','-DVL_TIME_CONTEXT','-I'+str(include),'-I'+str(include/'vltstd'),'-I'+str(directory),*sorted(directory.glob('*.cpp')),include/'verilated.cpp',include/'verilated_timing.cpp',include/'verilated_threads.cpp','-o',executable],name+'-compile')
        else:run([tool('make'),'-C',directory,'-f','V'+top+'.mk','-j8'],name+'-compile')
        output,exit_status=run([executable,'+HDD_ROM='+fixture.as_posix()],name+'-simulate',expected)
        if expected:
            controls.append({'name':name,'status':'REJECTED_EXPECTED','exit_status':exit_status,'expected_failure':expected,'derived_adapter_sha256':hashlib.sha256(text.encode('utf-8')).hexdigest(),'log':name+'-simulate.log'})
            print('PASS: rejected '+name+' at exact '+expected,flush=True)
        else:
            if output.count('PASS: raw logical mapping ')!=7 or 'PASS: raw logical mapping total=6' not in output:
                raise ValueError('Production did not complete all6 mapping groups')
            positive=output;print(output,flush=True)
    if hashes()!=before:raise ValueError('Production or test sources changed during logical cache qualification')
    (build/'validation.json').write_text(json.dumps({'schema_version':1,'status':'PASS','files':before,'hdd_rom_sha256':hashlib.sha256(rom).hexdigest(),'result':positive,'negative_controls':controls,'checks':['LC bank switching by native IO reads','RAMRD main/aux alias','invalid internal cycle retains L1 floating bus','mixed VPB-low vector/ordinary LC8B word','warm-reset mapping','hot ROM1/ROM3 selection with both loaded','raw/physical byte lanes agree','raw hit and local register are mutually exclusive'],'limits':['Synthetic P65 bus-boundary transport test; forced CPU outputs are separate from instruction execution tests.','No synthesis, routed timing or physical board test is implied.']},indent=2)+'\n')
    return 0

if __name__=='__main__':raise SystemExit(main())
