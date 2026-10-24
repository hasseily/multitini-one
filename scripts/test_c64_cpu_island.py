#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Production T65 against itself through the draft island/proxy/synchronous RAM."""
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
    p.add_argument('--mode',type=int,action='append')
    a=p.parse_args();out=a.build_dir.resolve()
    if not out.is_relative_to(ROOT/'build') or out==ROOT/'build' or out.exists():p.error('fresh build child required')
    out.mkdir(parents=True)
    receipt={'status':'FAIL','scope':'CPU island with RAM/ROM native bus model; no chip/interrupt/golden-emulator qualification',
             'cases':{},'source_sha256':{},'sources_unchanged':False}
    sources=[Path(__file__),ROOT/'scripts/run_native_tool.py',ROOT/'scripts/test_c64_acceleration.py']
    vhdl=[ROOT/'third_party/c64/upstream/rtl/t65'/f for f in ('T65_Pack.vhd','T65_ALU.vhd','T65_MCode.vhd')]
    vhdl += [ROOT/f for f in ('hdl/core/c64/ports/T65.vhd','hdl/core/c64/multitini_c64_6510_port.vhd','hdl/core/c64/ports/cpu_6510.vhd')]
    sv=[ROOT/'hdl/core/c64'/name for name in ['multitini_c64_cpu_island.sv', 'multitini_c64_cpu_proxy.sv', 'multitini_c64_cpu_turbo_guard.sv', 'multitini_c64_island_governor.sv', 'multitini_c64_island_map.sv', 'multitini_c64_island_memory.sv', 'multitini_c64_nmi_delivery.sv', 'multitini_c64_turbo_regs.sv']]+[ROOT/'hdl/sim/tb_c64_island_cpu.sv']
    sources+=vhdl+sv
    try:
        for src in sources:
            key=src.relative_to(ROOT).as_posix();receipt['source_sha256'][key]=digest(src)
            dst=out/'sources'/key;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
        spec=importlib.util.spec_from_file_location('cpu_fixture',ROOT/'scripts/test_c64_acceleration.py')
        fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
        mem=fixture.synthetic_memory()
        # Observation-only output from the production wrapper, preserving localDi.
        wrapper=out/'observed_cpu_6510.vhd';text=vhdl[-1].read_text()
        assert text.count('port (')==1 and text.count('-- Cunnect zee wires')==1
        text=text.replace('port (','port (\n        test_data : out std_logic_vector(7 downto 0);',1)
        text=text.replace('-- Cunnect zee wires','test_data <= localDi;\n\t-- Cunnect zee wires',1)
        wrapper.write_text(text)
        island=out/'observed_island.sv';text=(ROOT/'hdl/core/c64/multitini_c64_cpu_island.sv').read_text()
        assert text.count('input wire clk_cpu,')==1 and text.count('.di(cpu_di),')==1
        text=text.replace('input wire clk_cpu,','output wire [7:0] test_data,\n    input wire clk_cpu,',1)
        text=text.replace('.di(cpu_di),','.test_data(test_data),.di(cpu_di),',1)
        island.write_text(text)
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
        run('xvlog',['xvlog','--nolog','--sv',*[island if s.name=='multitini_c64_cpu_island.sv' else snap(s) for s in sv]])
        run('xvhdl',['xvhdl','--nolog','--2008',*[wrapper if s==vhdl[-1] else snap(s) for s in vhdl]])
        run('xelab',['xelab','--nolog','--timescale','1ns/1ps','-s','island_cpu','tb_island_cpu'])
        for mode in a.mode or [16,0,1,2,3,7,12,14,15]:
            if mode not in range(17):raise RuntimeError('mode must be 0..16 (16=exact)')
            case=out/f'mode-{mode:02d}';case.mkdir()
            for name,data in {'memory':mem,'low':mem[:32768],'high':mem[32768:],'rom':mem[0xa000:0xc000]+mem[0xe000:],
                              'char':mem[0xd000:0xe000]}.items():
                (out/f'{name}.hex').write_text(''.join(f'{b:02x}\n' for b in data))
            (out/'mode.txt').write_text(str(mode)+'\n')
            log=run(f'simulate-{mode:02d}',['xsim','island_cpu','--runall'])
            for name in ('reference.csv','island.csv'):shutil.copyfile(out/name,case/name)
            reference=(case/'reference.csv').read_text().splitlines();actual=(case/'island.csv').read_text().splitlines()
            if 'ISLAND_CPU PASS' not in log:raise RuntimeError(f'mode {mode} missing completion')
            if reference!=actual:
                first=next((i for i,(x,y) in enumerate(zip(reference,actual)) if x!=y),min(len(reference),len(actual)))
                raise RuntimeError(f'mode {mode} bus mismatch at row {first}: ref={reference[first:first+3]} dut={actual[first:first+3]}')
            receipt['cases'][str(mode)]={'status':'PASS','bus_cycles':len(actual),'sha256':digest(case/'island.csv')}
        receipt['sources_unchanged']=all(digest(ROOT/k)==v for k,v in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:raise RuntimeError('source changed')
        receipt['status']='PASS'
    except Exception as e:receipt['error']=str(e)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2));return 0 if receipt['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
