#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check Sony fractional timing and native IWM reads using original synthetic bits."""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/apple-iigs-iwm-tests')
    parser.add_argument('--rtl-dir',type=Path,default=ROOT/'hdl/core/apple_iigs/ports',help='directory containing production flux_drive.sv and iwm_flux.sv')
    args=parser.parse_args();build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    rtl=args.rtl_dir.resolve();sources=[rtl/'flux_drive.sv',rtl/'iwm_flux.sv',ROOT/'hdl/sim/tb_apple_iigs_iwm.sv']
    captured=sources+[Path(__file__).resolve()]
    hashes=lambda:{path.relative_to(ROOT).as_posix():hashlib.sha256(path.read_bytes()).hexdigest() for path in captured}
    before=hashes();runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    # Exercise the production task bodies with isolated state. Calling them on a
    # running FSM would introduce another driver and obscure the actual warning.
    def task(text,name):
        found=re.findall(r'    task automatic '+name+r';.*?    endtask',text,re.S)
        if len(found)!=1:raise ValueError('Expected one production task: '+name)
        return found[0]
    def declarations(text,names):
        result=[]
        for name in names:
            found=re.findall(r'^    (?:wire|localparam) [^;\n]*\b'+name+r'\s*=[^;\n]*;',text,re.M)
            if len(found)!=1:raise ValueError('Expected one production timing definition: '+name)
            result.append(found[0])
        return '\n'.join(result)
    drive=sources[0].read_text();decoder=sources[1].read_text()
    probes=build/'fraction_probe.sv'
    probes.write_text('''// Generated verbatim production arithmetic task probes; GPL-3.0-or-later.
`timescale 1ns/1ps
module iwm_drive_fraction_probe;
wire IS_35_INCH=1;
reg [9:0] bit_cell_frac,bit_half_frac;
reg [5:0] bit_timer,bit_half_timer,bit_cell_cycles_reg;
'''+declarations(drive,['BIT_CELL_35','BIT_CELL_525','use_fractional_bitcell','bit_cell_base','bit_half_base','bit_cell_step','bit_half_step'])+'\n'+task(drive,'load_bit_timers')+'''
endmodule
module iwm_decoder_fraction_probe;
wire IS_35_INCH=1;wire [7:0] SW_MODE=8'h0f;
reg [9:0] full_window_frac,half_window_frac;
reg [5:0] window_counter;
'''+declarations(decoder,['fast_mode','base_full_window','base_half_window','use_fractional_window','full_window_step','half_window_step'])+'\n'+task(decoder,'load_full_window')+'\n'+task(decoder,'load_half_window')+'\nendmodule\n')
    env=os.environ.copy()
    if os.name=='nt':
        env['PATH']=str(runtime)+os.pathsep+str(runtime.parents[1]/'usr/bin')+os.pathsep+env.get('PATH','')
        env['VERILATOR_ROOT']=(runtime.parent/'share/verilator').as_posix()
    tool=lambda name:str(runtime/(name+'.exe')) if os.name=='nt' else shutil.which(name)
    def run(command,name):
        log=build/(name+'.log')
        with log.open('w') as stream:
            proc=subprocess.run([str(p).replace('\\','/') for p in command],cwd=build,env=env,stdout=stream,stderr=subprocess.STDOUT)
        output=log.read_text(errors='replace')
        if proc.returncode or re.search(r'%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed',output):
            print(output[-10000:]);raise ValueError(f'IWM {name} failed: {log}')
        return output
    checks=[];summaries={}
    for top,debug in [('tb_apple_iigs_iwm_fraction',False),('tb_apple_iigs_iwm',False),('tb_apple_iigs_iwm',True)]:
        name=top+('-debug' if debug else '-default');directory=build/name
        run([tool('verilator_bin') if os.name=='nt' else tool('verilator'),'--cc','--exe','--main','--timing','-Wno-fatal',
             *(['+define+DEBUG_VERBOSE','+define+SIMULATION'] if debug else []),'--top-module',top,'--Mdir',directory,*sources,probes],name+'-generate')
        executable=directory/('V'+top+('.exe' if os.name=='nt' else ''))
        if os.name=='nt':
            inc=runtime.parent/'share/verilator/include'
            run([tool('g++'),'-std=c++20','-O1','-pthread','-DVL_TIME_CONTEXT','-I'+str(inc),'-I'+str(inc/'vltstd'),'-I'+str(directory),
                 *sorted(directory.glob('*.cpp')),inc/'verilated.cpp',inc/'verilated_timing.cpp',inc/'verilated_threads.cpp','-o',executable],name+'-compile')
        else:run([tool('make'),'-C',directory,'-f','V'+top+'.mk','-j8'],name+'-compile')
        cases=[('fraction',[])] if top.endswith('fraction') else [('stream14',['+SCENARIO=0','+POLL_DIV=1']),('stream84',['+SCENARIO=0','+POLL_DIV=6']),('hold-expiry',['+SCENARIO=1']),('mode-disable',['+SCENARIO=2'])]
        for case,options in cases:
            output=run([executable,*options],name+'-'+case)
            lines=[line for line in output.splitlines() if line.startswith('PASS:')]
            if len(lines)!=1:raise ValueError('IWM case did not report exactly one PASS')
            summaries[name+'/'+case]=lines[0];print(lines[0],flush=True);checks.append(name+'/'+case)
    for case in ['stream14','stream84','hold-expiry','mode-disable']:
        if summaries['tb_apple_iigs_iwm-default/'+case]!=summaries['tb_apple_iigs_iwm-debug/'+case]:
            raise ValueError('IWM default/debug behavior differs: '+case)
    if hashes()!=before:raise ValueError('IWM source changed during test; rerun frozen inputs')
    record={'schema_version':1,'result':'PASS','files':before,'checks':checks,'extracted_arithmetic_probe_sha256':hashlib.sha256(probes.read_bytes()).hexdigest(),'assertions':{
        'all1000_valid_fraction_values':True,'full_half_drive_decoder_consistent':True,'natural1000_cell_mean28636':True,
        'completed_cpu_ack_exact_synthetic_bytes':True,'unread_latch_expires_without_data_ack':True,
        'mode_disable_clears_hold':True,'status_does_not_consume':True,'default_debug_equivalent':True},
        'limits':['Original synthetic data only; no Apple ROM, disk image, full-system boot, FPGA timing or board test.']}
    (build/'validation.json').write_text(json.dumps(record,indent=2)+'\n');return 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,subprocess.CalledProcessError) as error:print(f'ERROR: {error}',file=sys.stderr);raise SystemExit(1)
