#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run NES transport/audio checks and the real mixed-language CPU/PPU simulation."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
def sources():
    base=ROOT/'hdl/core/nes';up=ROOT/'third_party/nes/upstream/rtl'
    vhdl=[up/'bus_savestates.vhd',*(up/'t65'/f'T65{s}.vhd' for s in ('_Pack','_ALU','_MCode',''))]
    sv=[up/'regs_savestates.sv',base/'multitini_nes_defs_pkg.sv',*sorted((base/'ports').glob('*.sv')),
        *(base/f'multitini_nes_{s}.sv' for s in ('loader','memory','save','audio','adapter'))]
    return vhdl,sv

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--test',action='append',choices=('loader','memory','save','audio','machine'))
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/nes-tests')
    parser.add_argument('--vivado-bin',type=Path,default=Path(os.environ.get('XILINX_VIVADO','C:/Xilinx/2025.2/Vivado'))/'bin')
    runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    parser.add_argument('--iverilog',default=str(runtime/'iverilog.exe') if os.name=='nt' else 'iverilog')
    parser.add_argument('--vvp',default=str(runtime/'vvp.exe') if os.name=='nt' else 'vvp')
    args=parser.parse_args();build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    (build/'validation.json').unlink(missing_ok=True)
    vhdl,sv=sources();tests=args.test or ['loader','memory','save','audio','machine']
    tracked=vhdl+sv+[Path(__file__).resolve(),ROOT/'scripts/port_nes_core.py']
    benches=[ROOT/f'hdl/sim/tb_nes_{test}.sv' for test in tests]
    tracked+=benches
    before={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked}
    def run(name,command,cwd):
        arguments=[value.as_posix() if isinstance(value,Path) else str(value) for value in command]
        result=subprocess.run(arguments,cwd=cwd,capture_output=True,text=True,timeout=600)
        text=result.stdout+result.stderr;(cwd/f'{name}.log').write_text(text)
        if result.returncode or re.search(r'\b(?:ERROR|FAIL|FATAL)\s*:',text,re.IGNORECASE):
            print(text[-18000:]);raise RuntimeError(f'NES {name} failed; see {cwd}')
        return text
    checks=[]
    for test,bench in zip(tests,benches):
        cwd=build/test;cwd.mkdir(exist_ok=True)
        bin=args.vivado_bin
        select=sv if test=='machine' else sv[:2]+[p for p in sv if p.name in ('multitini_nes_loader.sv','multitini_nes_memory.sv','multitini_nes_save.sv','multitini_nes_audio.sv')]
        top=f'tb_nes_{test}'
        if test=='machine':
            suffix='.bat' if os.name=='nt' else ''
            run('xvlog',[bin/('xvlog'+suffix),'--sv',*select,bench],cwd)
            run('xvhdl',[bin/('xvhdl'+suffix),'--2008',*vhdl],cwd)
            run('xelab',[bin/('xelab'+suffix),top,'--snapshot',top,'--debug','typical'],cwd)
            (cwd/'run.tcl').write_text('run all\nquit\n')
            output=run('simulate',[bin/('xsim'+suffix),top,'--tclbatch',cwd/'run.tcl'],cwd)
        else:
            guard=[sys.executable,ROOT/'scripts/run_native_tool.py','--'] if os.name=='nt' else []
            image=cwd/(top+'.vvp')
            run('compile',guard+[args.iverilog,'-g2012','-s',top,'-o',image,*select,bench],cwd)
            output=run('simulate',guard+[args.vvp,image],cwd)
        if not re.search(r'\bPASS:?\s+NES\b',output):
            raise RuntimeError(f'NES {test} has no NES PASS marker: {output[-5000:]}')
        print('\n'.join(line for line in output.splitlines() if 'PASS' in line),flush=True)
        checks.append({'name':test,'passed':True})
    after={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in tracked}
    if before!=after:raise ValueError('NES simulation inputs changed during validation')
    (build/'validation.json').write_text(json.dumps({'schema':1,'checks':checks,'source_sha256':before,
        'real_machine_executed':'machine' in tests,
        'limits':'Transport/audio simulations use Icarus; the machine test uses original T65 VHDL + PPU/APU/mapper RTL in XSim. Physical qualification is separate.'},indent=2)+'\n')
    return 0
if __name__=='__main__':raise SystemExit(main())
