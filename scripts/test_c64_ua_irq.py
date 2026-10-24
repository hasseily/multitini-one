#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual UA END IRQ isolation with CIA/VIC through the production CPU paths."""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,shutil,subprocess,sys
import test_c64_machine_schedule as base
import test_c64_turbo_ordering as ordering
ROOT=Path(__file__).resolve().parents[1]

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def program(order):
    memory=bytearray([0xea]*65536)
    code=bytearray.fromhex('78 D8 A2 FF 9A A9 2F 85 00 A9 35 85 01')
    def store(address,value):code.extend([0xa9,value,0x8d,address&255,address>>8])
    for address,value in ((0xfffe,0),(0xffff,0x10),(0x2104,0),
        (0xdc0d,0x7f),(0xdd0d,0x7f),(0xdc0e,0),(0xdd0e,0),
        (0xd01a,0),(0xd019,15),(0xd011,0x1b),(0xd012,51),(0xd01a,1),
        (0xdf20,0),(0xdf3f,255),(0xdf29,0),(0xdf2a,0),(0xdf2b,1),
        (0xdf2e,1),(0xdf2f,25),(0xdf20,5),
        (0xdc04,16),(0xdc05,0),(0xdc0d,0x81),(0xdc0e,0x19)):
        store(address,value)
    # Reads do not acknowledge these two latches. CIA is already one-shot
    # pending by the time raster 51 arrives; the bench checks all three.
    code.extend(bytes.fromhex('AD 20 DF 29 01 F0 F9 AD 19 D0 29 01 F0 F9'))
    store(0x2110,7)
    code.extend(bytes.fromhex('58 EA AD 04 21 F0 FB'))
    store(0x2105,0xa5)
    end=0x800+len(code);code.extend([0x4c,end&255,end>>8])
    memory[0x800:0x800+len(code)]=code
    code=bytearray.fromhex('48 A9 01 8D 01 21')
    # These native IO2 reads carry the accelerated handler into the badline.
    code.extend(bytes.fromhex('AD 21 DF')*16)
    remaining=7
    for source in ((1,4,2),(2,1,4),(4,2,1))[order]:
        if source==1:store(0xdf3f,1)
        elif source==2:code.extend(bytes.fromhex('AD 0D DC'))
        else:store(0xd019,1)
        remaining &= ~source;store(0x2110,remaining)
    store(0x2104,1);code.extend(bytes.fromhex('68 40'))
    memory[0x1000:0x1000+len(code)]=code
    return memory

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,required=True)
    args=parser.parse_args();out=args.build_dir.resolve()
    if out==ROOT/'build' or not out.is_relative_to(ROOT/'build') or out.exists():parser.error('fresh build/ child required')
    out.mkdir(parents=True);cp=ROOT/'hdl/core/c64'
    receipt={'status':'FAIL','source_sha256':{},'sources_unchanged':False}
    try:
        lane=ordering.make_lane(io2=True)
        lane=lane.replace('input wire [31:0] test_config,','output wire e4_cia_pending,e4_vic_pending, input wire [31:0] test_config,',1)
        anchor='        .ext_cpu_addr(ext_cpu_addr), .ext_cpu_do(ext_cpu_do), .ext_cpu_we(ext_cpu_we),'
        assert lane.count(anchor)==1
        lane=lane.replace(anchor,'.e4_cia_pending(e4_cia_pending),.e4_vic_pending(e4_vic_pending),\n'+anchor,1)
        anchor='.memory_addr(),.memory_valid(),.memory_busy(),.memory_ready(1\'b1),.memory_rdata(64\'d0),.memory_rvalid(1\'b0)'
        assert lane.count(anchor)==1
        lane=lane.replace(anchor,".memory_addr(),.memory_valid(e4_memory_valid),.memory_busy(),.memory_ready(1'b1),.memory_rdata(64'h2121212121212121),.memory_rvalid(e4_memory_rvalid)",1)
        lane=lane.replace('    wire test_iof;',"    wire e4_memory_valid; reg e4_memory_rvalid=0;\n    always @(posedge clk) e4_memory_rvalid<=e4_memory_valid && !ext_reset;\n    wire test_iof;",1)
        lane=lane.replace(".diagnostics_armed(1'b0)",".diagnostics_armed(1'b1)",1)
        (out/'c64_ua_irq_lane.sv').write_text(lane)
        original=(cp/'ports/fpga64_sid_iec.vhd').read_text();observed=base.instrument(original)
        observed=observed.replace('ext_cpu_di <= cpuDi;','ext_cpu_di <= cpuDi;\nexternal_observation: if EXTERNAL_CPU generate observedCpuRead <= cpuDi; end generate;')
        ports='e4_cia_pending,e4_vic_pending : out std_logic;\n'
        assignments='e4_cia_pending <= not irq_cia1; e4_vic_pending <= not irq_vic;\n'
        before=observed;observed=observed.replace('port(\n','port(\n'+ports,1).replace('\nbegin\n','\nbegin\n'+assignments,1)
        assert observed.replace(ports,'',1).replace(assignments,'',1)==before
        (out/'fpga64_sid_iec_observed.vhd').write_text(observed)
        (out/'cpu_6510_observed.vhd').write_text(base.instrument_wrapper((cp/'ports/cpu_6510.vhd').read_text()))
        for order in range(3):(out/f'irq-{order}.hex').write_text(''.join(f'{byte:02x}\n' for byte in program(order)))
        vhdl=[];section=''
        for raw in (ROOT/'hdl/hdl_sources.txt').read_text().splitlines():
            line=raw.strip()
            if line.startswith('['):section=line
            elif section=='[c64]' and line and not line.startswith('#'):
                src=(ROOT/'hdl'/line.split('|')[0].strip()).resolve()
                if src.suffix=='.vhd':vhdl.append(src)
        sv=[ROOT/path for path in ('third_party/c64/upstream/rtl/mos6526.v','hdl/core/c64/ports/sid_dac.sv','hdl/core/c64/ports/sid_filter.sv','third_party/c64/upstream/rtl/sid/sid_envelope.sv','hdl/core/c64/ports/sid_tables.sv','third_party/c64/upstream/rtl/sid/sid_voice.sv','hdl/core/c64/ports/sid_top.sv','hdl/core/c64/multitini_c64_local_ram.sv','hdl/core/c64/multitini_c64_fio_port.sv','hdl/sim/tb_c64_ua_irq.sv')]
        sv += [cp/f'multitini_c64_{name}.sv' for name in ('cpu_island','cpu_proxy','island_governor','island_map','island_memory','nmi_delivery','turbo_regs','reu','ua','ua_voice','ua_fetch','ua_mix')]
        sources=[*sv,*vhdl,Path(__file__),ROOT/'scripts/test_c64_machine_schedule.py',ROOT/'scripts/test_c64_turbo_exact.py',ROOT/'scripts/test_c64_turbo_ordering.py',ROOT/'scripts/run_native_tool.py',ROOT/'hdl/hdl_sources.txt',cp/'multitini_c64_adapter.sv']
        for src in sources:
            key=src.relative_to(ROOT).as_posix();receipt['source_sha256'][key]=digest(src)
            dst=out/'sources'/key;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
        snap=lambda path:out/'sources'/path.relative_to(ROOT)
        vivado=Path(os.environ.get('XILINX_VIVADO','C:/Xilinx/2025.2/Vivado'))/'bin'
        def run(name,arguments,expected_failure=False):
            child=subprocess.run([sys.executable,ROOT/'scripts/run_native_tool.py','--cwd',out,'--',*arguments],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=900)
            (out/(name+'.log')).write_text(child.stdout)
            failed=child.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:',child.stdout,re.I)
            if expected_failure:
                if not failed or 'IRQ source isolation' not in child.stdout:raise RuntimeError('Disconnected-UA negative control did not fail for IRQ isolation')
            elif failed:raise RuntimeError(name+' failed; see log')
            return child.stdout
        run('xvlog',[vivado/'xvlog.bat','--nolog','--sv','--relax',*[snap(src) for src in sv],out/'c64_ua_irq_lane.sv'])
        run('xvhdl',[vivado/'xvhdl.bat','--nolog','--2008',*[out/'fpga64_sid_iec_observed.vhd' if src.name=='fpga64_sid_iec.vhd' else out/'cpu_6510_observed.vhd' if src.name=='cpu_6510.vhd' else snap(src) for src in vhdl]])
        run('xelab',[vivado/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','-s','ua_irq_sim','tb_c64_ua_irq'])
        log=run('simulate',[vivado/'xsim.bat','--nolog','ua_irq_sim','--runall'])
        if 'UA_IRQ PASS cases=18' not in log:raise RuntimeError('Incomplete IRQ matrix')
        rows=list(csv.DictReader((out/'irq_sources.csv').open()))
        if len(rows)!=72 or len({(row['ntsc'],row['mode'],row['order']) for row in rows})!=18:raise RuntimeError('Incomplete source trace')
        shutil.copyfile(out/'irq_sources.csv',out/'positive_irq_sources.csv')
        anchor='.irq_ext_n(!(test_ua_irq||test_reu_irq))'
        assert lane.count(anchor)==1
        (out/'c64_ua_irq_lane_disconnected.sv').write_text(lane.replace(anchor,'.irq_ext_n(!test_reu_irq)',1))
        run('negative-xvlog',[vivado/'xvlog.bat','--nolog','--sv','--relax',out/'c64_ua_irq_lane_disconnected.sv'])
        run('negative-xelab',[vivado/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','-s','ua_irq_negative_sim','tb_c64_ua_irq'])
        run('negative-simulate',[vivado/'xsim.bat','--nolog','ua_irq_negative_sim','--runall'],True)
        receipt['sources_unchanged']=all(digest(ROOT/key)==value for key,value in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:raise RuntimeError('Source changed during validation')
        receipt.update(status='PASS',cases=18,source_markers=72,checks=[line for line in log.splitlines() if line.startswith('UA_IRQ_SUMMARY ')],negative_control='Disconnected UA IRQ rejected by source isolation assertion',trace_sha256=digest(out/'positive_irq_sources.csv'))
        receipt['limits']='Production UA/T65/proxy/island/VIC/CIA with synthetic ROM/RAM and one-clock acknowledged UA memory. Exact and index15, PAL/NTSC and both BA policies; all three IRQ clear orders. Physical board and DDR timing remain separate.'
    except Exception as error:receipt['error']=repr(error)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({key:value for key,value in receipt.items() if key!='source_sha256'},indent=2))
    return 0 if receipt['status']=='PASS' else 1

if __name__=='__main__':raise SystemExit(main())
