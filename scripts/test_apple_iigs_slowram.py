#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify IIgs banked slow RAM behavior and optional 7-series mapping/DRC."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
PART='xc7z020clg484-2'

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/core-gs3/slowram-tests')
    parser.add_argument('--vivado',type=Path,help='also synthesize/optimize/place and reject all RAM DRC errors')
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
    ram=ROOT/'hdl/core/apple_iigs/ports/bram.sv';bench=ROOT/'hdl/sim/tb_apple_iigs_slowram.sv'
    captured=[ram,bench,ROOT/'hdl/core/apple_iigs/ports/iigs.sv',ROOT/'scripts/port_apple_iigs_core.py',Path(__file__).resolve(),ROOT/'scripts/capture_vectrex_build_inputs.py']
    def hashes():return {p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in captured}
    before=hashes();top=bench.stem;directory=build/top
    run([tool('verilator_bin') if os.name=='nt' else tool('verilator'),'--cc','--exe','--main','--timing','-Wno-fatal','--top-module',top,'--Mdir',directory,ram,bench],top+'-generate')
    executable=directory/('V'+top+('.exe' if os.name=='nt' else ''))
    if os.name=='nt':
        include=runtime.parent/'share/verilator/include'
        run([tool('g++'),'-std=c++20','-O1','-pthread','-DVL_TIME_CONTEXT','-I'+str(include),'-I'+str(include/'vltstd'),'-I'+str(directory),*sorted(directory.glob('*.cpp')),include/'verilated.cpp',include/'verilated_timing.cpp',include/'verilated_threads.cpp','-o',executable],top+'-compile')
    else:run([tool('make'),'-C',directory,'-f','V'+top+'.mk','-j8'],top+'-compile')
    result=run([executable],top+'-simulate');print(result,flush=True)
    if 'PASS: banked slowram' not in result:raise ValueError('Missing full RAM coverage marker')
    mapping=None
    if args.vivado:
        script=build/'slowram_mapping.tcl'
        script.write_text('''read_verilog -sv {'''+ram.as_posix()+'''}
synth_design -top apple_iigs_slowram -part '''+PART+''' -mode out_of_context -flatten_hierarchy rebuilt
# External-CPU production clocks both ports from the one 57.272728 MHz
# primary clock: port A on CPU_CLK, port B on clk_vid. No clock-group cut.
create_clock -name core57 -period 17.460 [get_ports {clock_a clock_b}]
opt_design
set memories [get_cells -hier -filter {REF_NAME == RAMB36E1}]
if {[llength $memories] != 32} {error "Expected exactly32 RAMB36E1, got [llength $memories]"}
set census [open slowram_mapping.txt w]
foreach cell [lsort $memories] {
    foreach port {A B} {
        foreach property [list RAM_EXTENSION_$port] {
            if {[get_property $property $cell] ne "NONE"} {error "Unexpected hard cascade: $cell $property"}
        }
        foreach property [list READ_WIDTH_$port WRITE_WIDTH_$port] {
            if {[get_property $property $cell] != 2} {error "Expected16Kx2: $cell $property"}
        }
        if {[get_property WRITE_MODE_$port $cell] ne "WRITE_FIRST"} {error "Wrong write mode: $cell"}
    }
    puts $census "$cell A=[get_property RAM_EXTENSION_A $cell]/[get_property READ_WIDTH_A $cell]/[get_property WRITE_MODE_A $cell] B=[get_property RAM_EXTENSION_B $cell]/[get_property READ_WIDTH_B $cell]/[get_property WRITE_MODE_B $cell]"
}
close $census
# Same-clock true dual port: a WRITE_FIRST write on A and a same-address read
# on B in one cycle return invalid B data (UG473). The machine never writes
# port A on the video port's read edge, so the mode is reported, not changed.
puts "WRITE_MODE: A=[lsort -unique [get_property WRITE_MODE_A $memories]] B=[lsort -unique [get_property WRITE_MODE_B $memories]] clock=core57 17.460ns both ports"
set clocks [get_clocks]
if {[llength $clocks] != 1} {error "Expected one synchronous slow-RAM clock, got $clocks"}
report_clock_interaction -file slowram_clock_interaction.rpt
report_drc -file slowram_optimized_drc.rpt
if {[llength [get_drc_violations -quiet -filter {SEVERITY == Error}]]} {error "Optimized RAM DRC error"}
place_design
report_drc -file slowram_placed_drc.rpt
if {[llength [get_drc_violations -quiet -filter {SEVERITY == Error}]]} {error "Placed RAM DRC error"}
report_utilization -file slowram_utilization.rpt
write_checkpoint -force slowram_placed.dcp
puts "PASS: banked slowram mapped32 parallel16Kx2 RAMB36E1, no hard cascades, opt/place DRC clean, one synchronous 17.460ns clock"
''')
        output=run([shutil.which('python') or 'python',ROOT/'scripts/run_native_tool.py','--',args.vivado.resolve(),'-mode','batch','-source',script,'-log',build/'slowram_vivado.log','-journal',build/'slowram_vivado.jou'],'slowram-vivado-console')
        if 'PASS: banked slowram mapped32' not in output:raise ValueError('Missing Vivado mapping marker')
        mode=re.search(r'WRITE_MODE: A=(\S+) B=(\S+) clock=core57',output)
        if not mode:raise ValueError('Missing Vivado write-mode report')
        mapping={'part':PART,'ramb36':32,'shape':'16Kx2','extension_a':'NONE','extension_b':'NONE',
            'clock':'one 17.460 ns clock on both ports (no asynchronous groups)','write_mode_a':mode.group(1),'write_mode_b':mode.group(2),
            'optimized_drc':'PASS','placed_drc':'PASS','limits':'Isolated mapping/placement gate; full machine timing and DRC remain required.'}
        print(mode.group(0),flush=True)
        print(output[output.index('PASS: banked slowram mapped32'):].splitlines()[0],flush=True)
    if hashes()!=before:raise ValueError('Inputs changed during slow RAM qualification')
    (build/'validation.json').write_text(json.dumps({'schema_version':1,'files':before,'result':result,'mapping':mapping,'limits':['Production clocks both ports from one 17.460 ns clock. Port A is WRITE_FIRST: a port-A write and a same-cycle port-B read of the same byte give invalid port-B data, so the machine stages every port-A write off the video read edge. This bench drives two unrelated clocks on disjoint bytes during concurrent operations.','No physical board test is implied.']},indent=2)+'\n')
    return 0

if __name__=='__main__':raise SystemExit(main())
