#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run native RTC and exact SysInfo poll checks on real stock/020 CPUs."""
from __future__ import annotations
import argparse,hashlib,json,shutil,subprocess,sys
from pathlib import Path
from test_amiga_ports import ROOT,machine_sources

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/amiga-rtc-test')
    parser.add_argument('--vivado',type=Path,default=Path('C:/Xilinx/2025.2/Vivado'))
    parser.add_argument('--mutation',choices=('drop-seconds','no-rollover'),help='Require the old timestamp/seconds defect to fail the native assertions')
    args=parser.parse_args();build=args.build_dir.resolve()
    if not build.is_relative_to(ROOT/'build'):raise SystemExit('Select a repository build directory')
    build.mkdir(parents=True,exist_ok=True)
    vhdlbase=ROOT/'third_party/amiga/upstream/rtl/tg68k'
    vhdl=[vhdlbase/name for name in ('TG68K_Pack.vhd','TG68K_ALU.vhd','TG68K_MMU030.vhd','TG68KdotC_Kernel.vhd','TG68K.vhd')]
    sources=machine_sources()+[ROOT/'hdl/sim/tb_amiga_ports.sv',ROOT/'hdl/core/amiga/multitini_amiga_memory.sv',ROOT/'hdl/sim/tb_amiga_rtc.sv']
    original_sources=list(sources)
    if args.mutation:
        native=ROOT/'hdl/core/amiga/ports/minimig.v';text=native.read_text()
        before,after=('rtc_reg <= rtc[63:0];',"rtc_reg <= {rtc[63:8],8'd0};") if args.mutation=='drop-seconds' else ("else rtc_reg[7:0] <= 8'd0; //59 wraps even without a host refresh.",'// Deliberately restore the old seconds59 hold.')
        if text.count(before)!=1:raise SystemExit('Native RTC mutation target changed')
        mutant=build/'minimig-rtc-mutant.v';mutant.write_text(text.replace(before,after));sources[sources.index(native)]=mutant
    assets=[ROOT/'hdl/core/amiga/ports/m68k_cyc_idx.hex',ROOT/'hdl/core/amiga/ports/m68k_ea_idx.hex',ROOT/'third_party/amiga/upstream/rtl/fx68k/microrom.mem',ROOT/'third_party/amiga/upstream/rtl/fx68k/nanorom.mem']
    for p in assets:shutil.copyfile(p,build/p.name)
    inputs=list(dict.fromkeys(vhdl+original_sources+sources+assets+[Path(__file__).resolve(),ROOT/'scripts/test_amiga_ports.py']))
    receipt={'schema_version':1,'board_evidence':False,'purpose':'Actual native RTC/MMIO, both real CPUs and production memory; complete timestamp bytes; seeded58-59-00-01; exact SysInfo poll; native pauses; host toggle; guest writes/reset',
             'mutation':args.mutation,'fixture_limits':['Native four-frame guest reset shortened','Only the one-second divider is seeded at its terminal count; RTC behavior is unchanged','Standalone poll ROM does not execute the full SysInfo/OS'],
             'sources':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
    (build/'inputs.json').write_text(json.dumps(receipt,indent=2)+'\n')
    def run(name,argv):
        result=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--cwd',str(build),'--','cmd.exe','/d','/c',str(args.vivado/'bin'/f'{name}.bat'),*argv],capture_output=True,text=True,errors='replace')
        (build/f'{name}.stdout.log').write_text(result.stdout+result.stderr)
        if result.returncode and name!='xsim':print((result.stdout+result.stderr)[-10000:]);raise SystemExit(result.returncode)
        print(f'{name}: completed',flush=True)
        return result
    run('xvhdl',['--2008','--work','work',*map(str,vhdl)])
    run('xvlog',['--sv','--work','work','-i',str(ROOT/'third_party/amiga/upstream/rtl'),*map(str,sources)])
    for mode,top in ((0,'stock'),) if args.mutation else ((0,'stock'),(3,'turbo')):
        run('xelab',[f'work.tb_amiga_rtc_{top}','-s',f'amiga_rtc{mode}','--debug','typical','--timescale','1ns/1ps','--mt','auto'])
        result=run('xsim',[f'amiga_rtc{mode}','--runall']);log=result.stdout+result.stderr;(build/f'cpu{mode}.log').write_text(log)
        if args.mutation:
            expected='RTC full timestamp bits changed' if args.mutation=='drop-seconds' else 'RTC seconds fallback mismatch'
            if 'Fatal:' not in log or expected not in log:print(log[-8000:]);raise SystemExit('RTC mutation did not fail the intended native assertion')
            print(f'PASS {args.mutation} rejected by native RTC assertion',flush=True)
        elif result.returncode or f'PASS nativeRTC mode{mode} pause/resume' not in log or 'Fatal:' in log:
            print(log[-8000:]);raise SystemExit(f'Native RTC mode{mode} failed')
        else:print(f'PASS real CPU mode{mode} native RTC and SysInfo poll',flush=True)
    for name,expected in receipt['sources'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=expected:raise SystemExit(f'Input changed during RTC test: {name}')
    receipt['result']='mutation_rejected' if args.mutation else 'passed';(build/'report.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return 0

if __name__=='__main__':raise SystemExit(main())
