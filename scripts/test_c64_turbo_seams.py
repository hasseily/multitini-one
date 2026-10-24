#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check forced-exact policy seams through the production CPU and native machine."""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,shutil,subprocess,sys
import test_c64_machine_schedule as base
import test_c64_turbo_ordering as ordering
ROOT=Path(__file__).resolve().parents[1]
TARGETS=[('ram_write',0x2200,True),('cia_icr',0xdc0d,False),('sid_write',0xd418,True),('vic_ack',0xd019,True),('collision_read',0xd01e,False)]

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def fixture(address,write):
    mem=bytearray([0xea]*65536)
    boot=bytes.fromhex('78 D8 A2 FF 9A A9 2F 85 00 A9 37 85 01 A9 00 8D 11 D0 8D 15 D0 8D 1A D0 AD 1E D0 AD 1F D0 A2 FF CA D0 FD 4C 00 08')
    mem[0x700:0x700+len(boot)]=boot
    program=bytearray.fromhex('A9 05 8D 20 D0 8D 20 D0 8D 20 D0')
    program+=bytes([0xea])*64
    program+=bytes([0xa9,0x5a,0x8d if write else 0xad,address&255,address>>8])
    program+=bytes([0xea])*32
    program+=bytes.fromhex('A9 A5 8D 05 21');end=0x800+len(program)
    program+=bytes([0x4c,end&255,end>>8]);mem[0x800:0x800+len(program)]=program
    return mem


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args();out=a.build_dir.resolve()
    if out==ROOT/'build' or not out.is_relative_to(ROOT/'build') or out.exists():p.error('fresh build child required')
    out.mkdir(parents=True);cp=ROOT/'hdl/core/c64';receipt={'schema':1,'status':'FAIL','source_sha256':{},'sources_unchanged':False}
    try:
        lane=ordering.make_lane()
        lane=lane.replace('input wire [31:0] test_config,','input wire test_cart,test_loader,test_guard, input wire [31:0] test_config,',1)
        replacements={".cart_present(1'b0)":'.cart_present(test_cart)',".loader_active(1'b0)":'.loader_active(test_loader)',".guard_active(1'b1)":'.guard_active(!test_guard)'}
        for old,new in replacements.items():
            if old not in lane:raise RuntimeError('policy port missing '+old)
            lane=lane.replace(old,new)
        anchor='        .ext_cpu_addr(ext_cpu_addr), .ext_cpu_do(ext_cpu_do), .ext_cpu_we(ext_cpu_we),'
        assert lane.count(anchor)==1
        lane=lane.replace(anchor,'.seam_vic_wr(seam_vic_wr),.seam_vic_wa(seam_vic_wa),.seam_sid_wr(seam_sid_wr),.seam_sid_a(seam_sid_a),\n'+anchor)
        lane=lane.replace('input wire test_cart,','output wire seam_vic_wr,seam_sid_wr, output wire [5:0] seam_vic_wa, output wire [9:0] seam_sid_a, input wire test_cart,',1)
        (out/'c64_turbo_exact_lane.sv').write_text(lane)
        observed=base.instrument((cp/'ports/fpga64_sid_iec.vhd').read_text()).replace('ext_cpu_di <= cpuDi;','ext_cpu_di <= cpuDi;\nexternal_observation: if EXTERNAL_CPU generate observedCpuRead <= cpuDi; end generate;')
        ports='seam_vic_wr,seam_sid_wr : out std_logic; seam_vic_wa : out unsigned(5 downto 0); seam_sid_a : out unsigned(9 downto 0);\n'
        assignments='seam_vic_wr <= fastVicWr; seam_sid_wr <= fastSidWr; seam_vic_wa <= fastVicWa; seam_sid_a <= fastSidA;\n'
        before=observed;observed=observed.replace('port(\n','port(\n'+ports,1).replace('\nbegin\n','\nbegin\n'+assignments,1)
        assert observed.replace(ports,'',1).replace(assignments,'',1)==before
        (out/'fpga64_sid_iec_observed.vhd').write_text(observed)
        (out/'cpu_6510_observed.vhd').write_text(base.instrument_wrapper((cp/'ports/cpu_6510.vhd').read_text()))
        for i,(_,address,write) in enumerate(TARGETS):
            (out/f'seam-{i}.hex').write_text(''.join(f'{b:02x}\n' for b in fixture(address,write)))
        vhdl=[];section=''
        for raw in (ROOT/'hdl/hdl_sources.txt').read_text().splitlines():
            line=raw.strip()
            if line.startswith('['):section=line
            elif section=='[c64]' and line and not line.startswith('#'):
                src=(ROOT/'hdl'/line.split('|')[0].strip()).resolve()
                if src.suffix=='.vhd':vhdl.append(src)
        sv=[ROOT/p for p in ('third_party/c64/upstream/rtl/mos6526.v','hdl/core/c64/ports/sid_dac.sv','hdl/core/c64/ports/sid_filter.sv','third_party/c64/upstream/rtl/sid/sid_envelope.sv','hdl/core/c64/ports/sid_tables.sv','third_party/c64/upstream/rtl/sid/sid_voice.sv','hdl/core/c64/ports/sid_top.sv','hdl/core/c64/multitini_c64_local_ram.sv','hdl/core/c64/multitini_c64_fio_port.sv','hdl/sim/tb_c64_turbo_seams.sv')]
        sv += [cp/f'multitini_c64_{n}.sv' for n in ('cpu_island','cpu_proxy','island_governor','island_map','island_memory','nmi_delivery','turbo_regs')]
        sources=[*sv,*vhdl,Path(__file__),ROOT/'scripts/test_c64_machine_schedule.py',ROOT/'scripts/test_c64_turbo_exact.py',ROOT/'scripts/test_c64_turbo_ordering.py',ROOT/'scripts/run_native_tool.py',ROOT/'hdl/hdl_sources.txt',cp/'multitini_c64_adapter.sv']
        for src in sources:
            key=src.relative_to(ROOT).as_posix();receipt['source_sha256'][key]=digest(src);dst=out/'sources'/key;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
        snap=lambda s:out/'sources'/s.relative_to(ROOT)
        v=Path(os.environ.get('XILINX_VIVADO','C:/Xilinx/2025.2/Vivado'))/'bin'
        def run(name,args):
            r=subprocess.run([sys.executable,ROOT/'scripts/run_native_tool.py','--cwd',out,'--',*args],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=900)
            (out/(name+'.log')).write_text(r.stdout,encoding='utf-8')
            if r.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:',r.stdout,re.I):raise RuntimeError(f'{name} failed; see log')
            return r.stdout
        run('xvlog',[v/'xvlog.bat','--nolog','--sv','--relax',*[snap(s) for s in sv],out/'c64_turbo_exact_lane.sv'])
        run('xvhdl',[v/'xvhdl.bat','--nolog','--2008',*[out/'fpga64_sid_iec_observed.vhd' if s.name=='fpga64_sid_iec.vhd' else out/'cpu_6510_observed.vhd' if s.name=='cpu_6510.vhd' else snap(s) for s in vhdl]])
        run('xelab',[v/'xelab.bat','--nolog','--relax','--mt','off','--timescale','1ns/1ps','-s','seam_sim','tb_c64_turbo_seams'])
        log=run('simulate',[v/'xsim.bat','--nolog','seam_sim','--runall'])
        if 'TURBO_SEAMS PASS cases=40' not in log:raise RuntimeError('incomplete seam matrix')
        rows=list(csv.DictReader((out/'seams.csv').open()))
        expected={(str(v),str(t),str(c)) for v in range(2) for t in range(5) for c in range(4)}
        if {(r['ntsc'],r['target'],r['cause']) for r in rows}!=expected or len(rows)!=40:raise RuntimeError('seam inventory')
        trace=list(csv.DictReader((out/'seam_trace.csv').open()))
        mismatch=lambda row:row['reference']!=row['observed']
        if not trace or any(mismatch(row) for row in trace):raise RuntimeError('accepted CPU sequence differs')
        corrupt={**trace[len(trace)//2],'observed':f"{int(trace[len(trace)//2]['observed'],16)^0x200:07x}"}
        if not mismatch(corrupt):raise RuntimeError('negative trace mutation accepted')
        receipt.update(cases=rows,case_count=40,compared_bus_cycles=len(trace),negative_control='One accepted CPU address bit corrupted in an equal trace is rejected by the same comparison.',
            artifact_sha256={q.name:digest(q) for q in out.iterdir() if q.suffix in ('.csv','.hex') or q.name=='c64_turbo_exact_lane.sv' or q.name.endswith('_observed.vhd')})
        receipt['sources_unchanged']=all(digest(ROOT/k)==v for k,v in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:raise RuntimeError('source changed')
        receipt['limits']=['Actual production CPU/island/proxy/FIO/VIC/SID/CIA and adapter wiring, synthetic ROM/program.',
            'Three real turbo_regs cause inputs (guard, loader, cartridge) force exact while FIO is pending, then release before five selected exact bus accesses; exact reference is the same production CPU in forced-exact mode.',
            'Screen/sprites disabled isolate policy transitions; this does not cover BA-low last-cycle seams, reset-window expiration at every access, or NMI transition fuzz.',
            'Cause inputs are driven directly: actual IEC guard detection, loader memory writes, cartridge mapping and REU-triggered DMA ownership are separate and not claimed.',
            'No physical board acceptance claim.']
        receipt['status']='PASS'
    except Exception as e:receipt['error']=repr(e)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({k:v for k,v in receipt.items() if k not in ('source_sha256','cases','artifact_sha256')},indent=2));return 0 if receipt['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
