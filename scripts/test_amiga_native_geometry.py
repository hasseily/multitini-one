#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify native DMA/Denise geometry and every normalized640x512 pixel."""
from __future__ import annotations
import argparse,hashlib,json,subprocess,sys
from pathlib import Path
from test_amiga_ports import ROOT,machine_sources

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/amiga-native-geometry')
    parser.add_argument('--mutation',choices=('x','field'),help='Require a deliberately shifted crop or disabled short-field compensation to fail')
    args=parser.parse_args();build=args.build_dir.resolve()
    if not build.is_relative_to(ROOT/'build'):raise SystemExit('Select a repository build directory')
    build.mkdir(parents=True,exist_ok=True)
    tb=ROOT/'hdl/sim/tb_amiga_native_geometry.sv'
    normalizer=ROOT/'hdl/board/multitini_amiga_video_normalizer.sv'
    # Only Verilog units instantiate in this native video fixture; the CPU
    # VHDL and FX68K are neither needed nor replaced by behavioral stubs.
    sources=[p for p in machine_sources() if p.suffix=='.v' or p.name=='amiga_ram.sv']
    sources.append(normalizer)
    original_sources=list(sources)
    if args.mutation:
        text=normalizer.read_text()
        before,after=("capture_mode==0 ? 11'd206:","capture_mode==0 ? 11'd207:") if args.mutation=='x' else ("9'd41+{8'd0,field_short_q}","9'd41")
        if text.count(before)!=1:raise SystemExit('Normalizer changed; review mutation target')
        mutant=build/'normalizer-mutant.sv';mutant.write_text(text.replace(before,after))
        sources[-1]=mutant
    inputs=list(dict.fromkeys(original_sources+sources+[tb,Path(__file__).resolve(),ROOT/'scripts/test_amiga_ports.py']))
    receipt={'schema_version':1,'board_evidence':False,'purpose':'Actual native DMA/shifter/palette window and all640x512 output pixels; distinct four-corner bitmap words; nonlace and interlace/native pauses',
             'mutation':args.mutation,'sources':{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}}
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--']
    command=['C:/msys64/ucrt64/bin/iverilog.exe','-g2012','-s','tb_amiga_native_geometry','-I',str(ROOT/'third_party/amiga/upstream/rtl'),'-o',str(build/'geometry.vvp'),str(tb),*map(str,sources)]
    result=subprocess.run(guard+command,cwd=ROOT,capture_output=True,text=True,errors='replace')
    (build/'compile.log').write_text(result.stdout+result.stderr)
    if result.returncode:print(result.stdout+result.stderr);return result.returncode
    cases=[('mutation',['+LACE','+PAUSES'])] if args.mutation else [('pal',[]),('interlace-pauses',['+LACE','+PAUSES'])]
    for name,options in cases:
        result=subprocess.run(guard+['C:/msys64/ucrt64/bin/vvp.exe',str(build/'geometry.vvp'),*options],cwd=ROOT,capture_output=True,text=True,errors='replace')
        log=result.stdout+result.stderr;(build/f'{name}.log').write_text(log)
        if args.mutation:
            if result.returncode==0 or 'Normalized RGB mismatch' not in log:print(log);raise SystemExit('Geometry mutation did not fail at the expected pixel assertion')
            print(f'PASS {args.mutation} mutation rejected at normalized pixel assertion',flush=True)
        else:
            if result.returncode or 'PASS actual native geometry:' not in log:print(log);raise SystemExit(f'Native geometry failed: {name}')
            print(f'PASS {name}: actual native DMA, all output pixels and four corners',flush=True)
    for name,expected in receipt['sources'].items():
        if hashlib.sha256((ROOT/name).read_bytes()).hexdigest()!=expected:raise SystemExit(f'Input changed during geometry test: {name}')
    receipt['result']='mutation_rejected' if args.mutation else 'passed'
    (build/'report.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return 0

if __name__=='__main__':raise SystemExit(main())
