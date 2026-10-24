#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native UCI endpoint, held RAM DMA, real XPM transport, and ARM service tests."""
from __future__ import annotations
import argparse, hashlib, json, os, re, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args()
    out=a.build_dir.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'build'):p.error('Choose a fresh build/ child')
    out.mkdir(parents=True)
    files=['hdl/core/c64/multitini_c64_uci.sv','hdl/core/c64/multitini_c64_uci_ram.sv',
        'hdl/core/c64/multitini_c64_uci_transport.sv','hdl/common/multitini_async_mailbox.sv','hdl/common/multitini_axi3_regs.sv',
        'third_party/appletini/hdl/cdc_bit_sync.sv','hdl/sim/tb_c64_uci.sv',
        'hdl/sim/tb_c64_uci_ram.sv','hdl/sim/tb_c64_uci_transport.sv','hdl/sim/tb_c64_uci_axi.sv',
        'firmware/common/c64_uci.c','firmware/common/c64_iec_dos.c','firmware/common/core_link.c','firmware/common/clock.c',
        'firmware/platform/xilinx/c64_uci_service.c','firmware/platform/xilinx/c64_uci_dos.c',
        'firmware/platform/xilinx/c64_uci_control.c','firmware/platform/xilinx/c64_uci_geos.c','firmware/platform/xilinx/c64_uci_geos.h','firmware/include/multitini_c64_uci.h',
        'firmware/platform/xilinx/c64_uci_internal.h','firmware/platform/xilinx/c64_uci_service.h',
        'tests/host/test_c64_uci.c','tests/host/test_c64_uci_service.c',
        'tests/host/c64_uci_storage_fixture.h','tests/host/c64_uci_quiesce_cases.inc',
        'tests/host/c64_iec_fixture.h','scripts/test_c64_uci.py']
    receipt={'status':'FAIL','source_sha256':{f:digest(ROOT/f) for f in files},'commands':[],
        'scope':'Synthetic native and firmware tests; no board, CPU IRQ integration, or routed timing claim.'}
    env={k:v for k,v in os.environ.items() if k.lower()!='path'}
    env['PATH']='C:/msys64/ucrt64/bin;'+os.environ.get('PATH',os.environ.get('Path',''))
    native=Path('C:/msys64/ucrt64/bin');vendor=Path('C:/Xilinx/2025.2/Vivado/bin')
    def run(name,args,marker=None):
        cmd=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*map(str,args)]
        r=subprocess.run(cmd,cwd=out,env=env,capture_output=True,text=True,errors='replace',timeout=300)
        log=out/(name+'.log');log.write_text(r.stdout+r.stderr)
        receipt['commands'].append({'argv':cmd,'returncode':r.returncode,'log':log.relative_to(ROOT).as_posix(),'sha256':digest(log)})
        if r.returncode or re.search(r'\b(?:ERROR|FATAL)\s*:',r.stdout+r.stderr):raise RuntimeError(name+': '+(r.stdout+r.stderr)[-5000:])
        if marker and marker not in r.stdout:raise RuntimeError('Missing pass marker '+name)
        print(r.stdout[-500:] if marker else name+' compiled')
    try:
        for suffix in ('','_ram'):
            name='tb_c64_uci'+suffix
            run(name+'-compile',[native/'iverilog.exe','-g2012','-s',name,'-o',name+'.vvp',
                ROOT/('hdl/core/c64/multitini_c64_uci'+suffix+'.sv'),ROOT/('hdl/sim/'+name+'.sv')])
            run(name,[native/'vvp.exe',name+'.vvp'],'PASS C64 UCI')
        xpm=vendor.parent/'data/ip/xpm';vfiles=[xpm/'xpm_cdc/hdl/xpm_cdc.sv',xpm/'xpm_memory/hdl/xpm_memory.sv',vendor.parent/'data/verilog/src/glbl.v']
        receipt['vendor_sha256']={str(f):digest(f) for f in vfiles}
        run('transport-compile',[vendor/'xvlog.bat','--nolog','--sv','-d','SYNTHESIS',*vfiles,
            ROOT/'third_party/appletini/hdl/cdc_bit_sync.sv',ROOT/'hdl/common/multitini_async_mailbox.sv',
            ROOT/'hdl/core/c64/multitini_c64_uci_transport.sv',ROOT/'hdl/sim/tb_c64_uci_transport.sv'])
        run('transport-elaborate',[vendor/'xelab.bat','--nolog','--relax','-L','unisims_ver','--timescale','1ns/1ps','--snapshot','uci_transport','tb_c64_uci_transport','glbl'])
        run('transport',[vendor/'xsim.bat','--nolog','uci_transport','--runall'],'PASS C64 UCI transport')
        run('axi-compile',[vendor/'xvlog.bat','--nolog','--sv','-d','SYNTHESIS',ROOT/'hdl/common/multitini_axi3_regs.sv',ROOT/'hdl/sim/tb_c64_uci_axi.sv'])
        run('axi-elaborate',[vendor/'xelab.bat','--nolog','--relax','-L','unisims_ver','--timescale','1ns/1ps','--snapshot','uci_axi','tb_c64_uci_axi','glbl'])
        run('axi',[vendor/'xsim.bat','--nolog','uci_axi','--runall'],'PASS C64 UCI AXI')
        common=['firmware/common/c64_uci.c','firmware/common/core_link.c']
        for name,src in [('engine',common+['tests/host/test_c64_uci.c']),('platform',common+[
            'firmware/common/c64_iec_dos.c','firmware/common/clock.c','firmware/platform/xilinx/c64_uci_service.c',
            'firmware/platform/xilinx/c64_uci_dos.c','firmware/platform/xilinx/c64_uci_control.c','firmware/platform/xilinx/c64_uci_geos.c','tests/host/test_c64_uci_service.c'])]:
            run(name+'-compile',[native/'gcc.exe','-std=c11','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',
                '-I'+str(ROOT/'firmware/include'),'-I'+str(ROOT/'firmware/platform/xilinx'),*[ROOT/f for f in src],'-o',out/(name+'.exe')])
            run(name,[out/(name+'.exe')],'passed' if name=='platform' else 'PASS')
        if receipt['source_sha256']!={f:digest(ROOT/f) for f in files}:raise RuntimeError('Sources changed during test')
        receipt['status']='PASS'
    except Exception as e:receipt['error']=str(e);print(e)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt['status']!='PASS'
if __name__=='__main__':raise SystemExit(main())
