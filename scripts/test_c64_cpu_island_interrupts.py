#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real T65 interrupt handlers driven by the island NMI carry protocol."""
from __future__ import annotations
import argparse,hashlib,importlib.util,json,os
from pathlib import Path
import re,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
HERE=ROOT
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,required=True)
    a=p.parse_args();out=a.build_dir.resolve()
    if not out.is_relative_to(ROOT/'build') or out==ROOT/'build' or out.exists():p.error('fresh build child required')
    out.mkdir(parents=True)
    receipt={'status':'FAIL','scope':'actual NMOS CPU handler dispatch with island NMI delivery; synthetic memory, not full-machine or VICE comparison',
             'cases':{},'source_sha256':{},'sources_unchanged':False}
    sources=[Path(__file__),ROOT/'scripts/run_native_tool.py',ROOT/'scripts/test_c64_acceleration.py']
    vhdl=[ROOT/'third_party/c64/upstream/rtl/t65'/f for f in ('T65_Pack.vhd','T65_ALU.vhd','T65_MCode.vhd')]
    vhdl += [ROOT/f for f in ('hdl/core/c64/ports/T65.vhd','hdl/core/c64/multitini_c64_6510_port.vhd','hdl/core/c64/ports/cpu_6510.vhd')]
    sv=[ROOT/'hdl/core/c64/multitini_c64_nmi_delivery.sv',ROOT/'hdl/sim/tb_c64_island_interrupts.sv']
    sources+=vhdl+sv
    try:
        for src in sources:
            key=src.relative_to(ROOT).as_posix();receipt['source_sha256'][key]=digest(src)
            dst=out/'sources'/key;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
        vivado=Path(os.environ.get('XILINX_VIVADO','C:/Xilinx/2025.2/Vivado'))/'bin'
        suffix='.bat' if os.name=='nt' else ''
        guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--cwd',str(out),'--']
        def run(name,args,timeout=180):
            result=subprocess.run(guard+[str(vivado/(args[0]+suffix)),*map(str,args[1:])],stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,text=True,errors='replace',timeout=timeout)
            (out/f'{name}.log').write_text(result.stdout,encoding='utf-8')
            if result.returncode:raise RuntimeError(f'{name} failed; see {name}.log')
            return result.stdout
        snap=lambda s:out/'sources'/s.relative_to(ROOT)
        run('xvlog',['xvlog','--nolog','--sv',*[snap(s) for s in sv]])
        run('xvhdl',['xvhdl','--nolog','--2008',*[snap(s) for s in vhdl]])
        run('xelab',['xelab','--nolog','--timescale','1ns/1ps','-s','island_interrupts','tb_c64_island_interrupts'])
        log=run('simulate',['xsim','island_interrupts','--runall'])
        match=re.search(r'ISLAND_INTERRUPTS PASS checks=(\d+) nmi=3 irq=1',log)
        if not match:raise RuntimeError('Missing interrupt completion signature')
        receipt['checks']=int(match[1]);receipt['nmi_handlers']=3;receipt['irq_handlers']=1
        receipt['sources_unchanged']=all(digest(ROOT/k)==v for k,v in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:raise RuntimeError('source changed')
        receipt['status']='PASS'
    except Exception as e:receipt['error']=str(e)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2));return 0 if receipt['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
