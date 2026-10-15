#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the production native clock block's pulses, phase and held reset."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/core-gs/native-clock-tests')
    args=parser.parse_args();build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    adapter=ROOT/'hdl/core/apple_iigs/multitini_apple_iigs_adapter.sv'
    model=ROOT/'hdl/sim/apple_iigs_bufgce.sv'
    bench=ROOT/'hdl/sim/tb_apple_iigs_native_clock.sv'
    captured=[adapter,model,bench,Path(__file__).resolve()]
    def hashes():return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in captured}
    before=hashes();source=adapter.read_text()
    start=source.index('    reg [1:0] phase;')
    end=source.index('    wire pixel_ce=',start)
    clock_block=source[start:end]
    # Compile the actual production block. Only its internal clock wire
    # declaration becomes module outputs; the logic is copied byte for byte.
    if clock_block.count('    wire clk14,clk28;')!=1:raise ValueError('Native clock declaration changed')
    extracted=build/'native_clock_extracted.sv'
    extracted.write_text('`timescale 1ns/1ps\nmodule native_clock_extracted(input wire core_clk,reset,output wire clk14,clk28,output wire [1:0] phase_value);\n'+
        clock_block.replace('    wire clk14,clk28;','')+'\nassign phase_value=phase;\nendmodule\n')
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
            raise ValueError(name+' failed:\n'+output[-12000:])
        return output
    obj=build/'obj';top='tb_apple_iigs_native_clock'
    run([tool('verilator_bin') if os.name=='nt' else tool('verilator'),'--cc','--exe','--main','--timing','-Wno-fatal','--top-module',top,'--Mdir',obj,model,extracted,bench],'generate')
    executable=obj/('V'+top+('.exe' if os.name=='nt' else ''))
    if os.name=='nt':
        inc=runtime.parent/'share/verilator/include'
        run([tool('g++'),'-std=c++20','-O1','-pthread','-DVL_TIME_CONTEXT','-I'+str(inc),'-I'+str(inc/'vltstd'),'-I'+str(obj),*sorted(obj.glob('*.cpp')),inc/'verilated.cpp',inc/'verilated_timing.cpp',inc/'verilated_threads.cpp','-o',executable],'compile')
    else:run([tool('make'),'-C',obj,'-f','V'+top+'.mk','-j8'],'compile')
    experiments=[]
    pattern=r'CLOCK_SUMMARY reset_tests=(\d+) startup_omissions=(\d+) live_edges=(\d+) live_comparisons=(\d+) pulse_width_checks=(\d+)'
    for delayed in (False,True):
        name='simulate-'+('1ps' if delayed else 'zero')
        output=run([executable]+(['+DELAYED_BUFG'] if delayed else []),name)
        matches=re.findall(pattern,output)
        if 'PASS:' not in output or len(matches)!=1:raise ValueError('Missing native clock coverage')
        values=dict(zip(('reset_tests','startup_omissions','live_edges','live_comparisons','pulse_width_checks'),map(int,matches[0])))
        if values['reset_tests']!=5 or values['startup_omissions']!=5 or values['live_comparisons']<500 or values['pulse_width_checks']<5000:
            raise ValueError('Native clock reset/pulse coverage incomplete')
        experiments.append({'status':'PASS','clock_delay_ps':int(delayed),'summary':values,'log':name+'.log'})
        print(output.strip())
    if before!=hashes():raise ValueError('Clock sources changed during regression')
    (build/'validation.json').write_text(json.dumps({'schema_version':1,'status':'PASS','files':before,'source_hashes_unchanged':True,
        'extracted_clock_block_sha256':hashlib.sha256(clock_block.encode()).hexdigest(),'experiments':experiments,
        'checks':['actual production clock block compiled without logic substitutions','positive native edges match the prior divider after held reset','phase counter reset at every phase','one primary half-period high pulse','only prior reset-held startup phase0 pulse omitted'],
        'limits':['Functional simulation only; physical CE setup/hold, clock pulse widths and routing require the strict Vivado gates.']},indent=2)+'\n')
    return 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:
        print('ERROR: '+str(error),file=sys.stderr);raise SystemExit(1)
