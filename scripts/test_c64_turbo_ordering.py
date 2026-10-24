#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare accepted production CPU bus order at all 16 acceleration indices."""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,subprocess,sys
import test_c64_machine_schedule as base
import test_c64_turbo_exact as exact
ROOT=Path(__file__).resolve().parents[1]

def make_lane(io2=False):
    lane=exact.make_lane()
    lane=lane.replace('input wire clk,fastclk,','input wire [31:0] test_config,\n     input wire clk,fastclk,',1)
    constants="assign policy_turbo=0; assign policy_blocked=0; assign policy_badline=1;\n    assign policy_index=0; assign policy_c64u_top=0;"
    if lane.count(constants)!=1:raise ValueError('exact policy anchor')
    adapter=(ROOT/'hdl/core/c64/multitini_c64_adapter.sv').read_text()
    policy=adapter[adapter.index('    multitini_c64_turbo_regs #'):adapter.index('    assign cart_external_select')]
    policy=policy.replace('.CORE_CLOCK_HZ(CORE_CLOCK_HZ)', '.CORE_CLOCK_HZ(32000000), .RESET_WINDOW_CYCLES(256)')
    for old,new in {'core_clk':'clk','cpu_config':'test_config','cpu_turbo_enable':"1'b0",'cart_loaded':"1'b0",
        'content_load_active || rom_load_active || cart_loading':"1'b0",'core_paused':'paused','cpu_turbo_active':"1'b1",
        '.guard_cause(guard_cause)':".guard_cause(2'b00)",'cpu_bus_valid':'ext_cpu_bus_valid','cpu_bus_write':'ext_cpu_we',
        'cpu_bus_addr':'ext_cpu_addr','cpu_bus_wdata':'ext_cpu_do'}.items():policy=re.sub(r'(?<![A-Za-z0-9_])'+re.escape(old)+r'(?![A-Za-z0-9_])',lambda _:new,policy)
    lane=lane.replace(constants,'wire [31:0] cpu_status;\n'+policy)
    lane=lane.replace('assign proxy_native_data = ext_cpu_di;','assign proxy_native_data = ext_cs_vic && native_rd_override ? native_rd_data : ext_cpu_di;')
    lane=lane.replace('assign island_fio_data = fio_rd_data;','assign island_fio_data = !fio_rd_sid && fast_rd_override ? fast_rd_data : fio_rd_data;')
    if io2:
        lane=lane.replace('input wire [31:0] test_config,','input wire [31:0] test_config, input wire test_ua_enable,',1)
        modules='''
    wire test_iof;
    wire [15:0] test_addr=EXTERNAL?ext_cpu_addr:dbg_cpu_addr;
    wire [7:0] test_wdata=EXTERNAL?ext_cpu_do:dbg_cpu_do;
    wire test_we=EXTERNAL?ext_cpu_we:dbg_cpu_we;
    wire test_ua_select=test_ua_enable&&test_iof&&test_addr[7:5]!=0;
    wire test_reu_select=test_iof&&(!test_ua_enable||test_addr[7:5]==0);
    wire [7:0] test_ua_data,test_reu_data;
    wire test_ua_irq,test_reu_irq;
    multitini_c64_reu test_reu(
        .clk(clk),.reset(ext_reset),.enable(1'b1),
        .io_valid(ext_cpu_bus_valid&&test_reu_select),.io_write(test_we),
        .io_addr(test_addr[4:0]),.io_wdata(test_wdata),.io_rdata(test_reu_data),
        .ff00_write(1'b0),.dma_req(),.dma_valid(),.dma_idle(),.dma_peek(),.dma_last_write(),
        .dma_addr(),.dma_write(),.dma_wdata(),.dma_ack(1'b0),.dma_rdata(8'd0),
        .memory_addr(),.memory_write(),.memory_wdata(),.memory_wstrb(),.memory_valid(),
        .memory_ready(1'b1),.memory_rdata(64'd0),.memory_rvalid(1'b0),.memory_fault(1'b0),
        .memory_hint_read(),.memory_hint_addr(),.memory_waiting(),.irq(test_reu_irq),.busy(),.fault());
    multitini_c64_ua #(.CORE_CLOCK_HZ(32000000)) test_ua(
        .clk(clk),.hard_reset(!reset_n),.guest_reset(ext_reset),.enable(test_ua_enable),.paused(paused),.hold(1'b0),
        .reg_write(ext_cpu_bus_valid&&test_we&&test_ua_select),.reg_addr(test_addr[7:0]),.reg_wdata(test_wdata),.reg_rdata(test_ua_data),
        .diagnostics_armed(1'b0),.audio_tick(1'b0),.board_clip(1'b0),
        .cache_pending_addr(84'd0),.cache_pending_valid(4'd0),.cache_inflight_write_addr(21'd0),.cache_inflight_write_valid(1'b0),
        .cache_write_done(1'b0),.cache_write_done_addr(21'd0),.cache_write_snoop_valid(1'b0),.cache_write_snoop_addr(21'd0),
        .reu_memory_busy(1'b0),.irq(test_ua_irq),.held(),.status(),.fetch_count(),.audio_l(),.audio_r(),
        .memory_addr(),.memory_valid(),.memory_busy(),.memory_ready(1'b1),.memory_rdata(64'd0),.memory_rvalid(1'b0),.memory_fault(1'b0));
'''
        lane=lane.replace('    generate if(EXTERNAL)',modules+'\n    generate if(EXTERNAL)',1)
        lane=lane.replace(".io_ext(1'b0), .io_data(8'hFF)",".io_ext(test_ua_select||test_reu_select), .io_data(test_ua_select?test_ua_data:test_reu_data)")
        lane=lane.replace('.IOE(), .IOF(),','.IOE(), .IOF(test_iof),')
        lane=lane.replace(".irq_ext_n(1'b1)",'.irq_ext_n(!(test_ua_irq||test_reu_irq))')
    return lane

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/c64-turbo-ordering')
    parser.add_argument('--profile',choices=['raw','io2'],default='raw')
    args=parser.parse_args();out=args.build_dir.resolve()
    if not out.is_relative_to(ROOT/'build') or out==ROOT/'build' or out.exists():raise ValueError('Require fresh dedicated build directory')
    out.mkdir(parents=True)
    io2=args.profile=='io2'
    (out/'c64_turbo_exact_lane.sv').write_text(make_lane(io2))
    cp=ROOT/'hdl/core/c64'
    core=(cp/'ports/fpga64_sid_iec.vhd').read_text()
    observed=base.instrument(core).replace('ext_cpu_di <= cpuDi;','ext_cpu_di <= cpuDi;\nexternal_observation: if EXTERNAL_CPU generate observedCpuRead <= cpuDi; end generate;')
    (out/'fpga64_sid_iec_observed.vhd').write_text(observed)
    (out/'cpu_6510_observed.vhd').write_text(base.instrument_wrapper((cp/'ports/cpu_6510.vhd').read_text()))
    island=(cp/'multitini_c64_cpu_island.sv').read_text()
    declaration='    output wire [7:0] observe_cpu_di,\n'
    connection='        .debug_read(observe_cpu_di),\n'
    instrumented=island.replace('    output wire nmi_ack,',declaration+'    output wire nmi_ack,',1).replace('    cpu_6510 cpu_i (','    cpu_6510 cpu_i (\n'+connection,1)
    if instrumented.replace(declaration,'',1).replace('\n'+connection,'',1)!=island:raise ValueError('island observation changed body')
    (out/'island_observed.sv').write_text(instrumented)
    memory=bytearray(base.synthetic_memory())
    # The imported collision latches have no reset. Prime them with genuine
    # native read/clear cycles inside the stock reset window, as boot code does.
    prefix=bytes.fromhex('AD 1E D0 AD 1F D0')
    original_end=memory.index(bytes.fromhex('A9 A5 8D 05 21'),0x800)+5
    memory[0x800:original_end+len(prefix)]=prefix+memory[0x800:original_end]
    stop=memory.index(bytes.fromhex('A9 A5 8D 05 21'),0x800)
    program=bytearray()
    for mirror in range(16):
        address=0xd020+64*mirror
        program+=bytes([0xa9,mirror,0x8d,address&255,address>>8,0xad,address&255,address>>8,
                        0x8d,0x20+mirror,0x21])
    program+=bytes.fromhex(
        'EE 20 D0 AD 20 D0 8D 30 21 '
        'A9 00 8D 1A D0 A9 0F 8D 19 D0 AD 19 D0 8D 31 21 '
        'A9 5A 8D 05 D4 AD 1D D4 8D 32 21 '
        'AD 1B D4 AD 1D D4 8D 33 21 '
        'AD 1E D0 AD 1E D0 AD 1F D0 AD 1F D0 '
        'A0 01 A9 44 99 1E D0 A2 02 A9 55 9D FF D4 '
        'A9 0F 8D 18 D4 AD 0D DC 8D 34 21 '
        'A9 A5 8D 05 21')
    end=stop+len(program);program+=bytes([0x4c,end&255,end>>8])
    memory[stop:stop+len(program)]=program
    if io2:
        memory=bytearray([0xea]*65536)
        boot=bytes.fromhex('78 D8 A9 2F 85 00 A9 37 85 01 A9 1B 8D 11 D0 4C 00 08')
        memory[0x700:0x700+len(boot)]=boot
        program=bytearray.fromhex('A2 00')
        for address in range(256):
            program+=bytes([0x8e,address,0xdf,0xad,address,0xdf])
        program+=bytes.fromhex('A9 A5 8D 05 21')
        end=0x800+len(program);program+=bytes([0x4c,end&255,end>>8])
        memory[0x800:0x800+len(program)]=program
    (out/'machine_memory.hex').write_text('\n'.join(f'{b:02x}' for b in memory)+'\n')
    vhdl=[];section=''
    for raw in (ROOT/'hdl/hdl_sources.txt').read_text().splitlines():
        line=raw.strip()
        if line.startswith('['):section=line
        elif section=='[c64]' and line and not line.startswith('#'):
            p=(ROOT/'hdl'/line.split('|')[0].strip()).resolve()
            if p.suffix=='.vhd':
                if p.name=='fpga64_sid_iec.vhd':p=out/'fpga64_sid_iec_observed.vhd'
                elif p.name=='cpu_6510.vhd':p=out/'cpu_6510_observed.vhd'
                vhdl.append(p)
    sv=[ROOT/p for p in ('third_party/c64/upstream/rtl/mos6526.v','hdl/core/c64/ports/sid_dac.sv','hdl/core/c64/ports/sid_filter.sv','third_party/c64/upstream/rtl/sid/sid_envelope.sv','hdl/core/c64/ports/sid_tables.sv','third_party/c64/upstream/rtl/sid/sid_voice.sv','hdl/core/c64/ports/sid_top.sv','hdl/core/c64/multitini_c64_local_ram.sv','hdl/core/c64/multitini_c64_fio_port.sv','hdl/sim/tb_c64_turbo_ordering.sv')]
    sv += [cp/name for name in ('multitini_c64_cpu_proxy.sv','multitini_c64_island_governor.sv','multitini_c64_island_map.sv','multitini_c64_island_memory.sv','multitini_c64_nmi_delivery.sv','multitini_c64_turbo_regs.sv')]
    sv += [out/'island_observed.sv',out/'c64_turbo_exact_lane.sv']
    if io2:
        sv.remove(ROOT/'hdl/sim/tb_c64_turbo_ordering.sv')
        sv.append(ROOT/'hdl/sim/tb_c64_turbo_io2.sv')
        sv += [cp/name for name in ('multitini_c64_reu.sv','multitini_c64_ua.sv','multitini_c64_ua_voice.sv','multitini_c64_ua_fetch.sv','multitini_c64_ua_mix.sv')]
    sources=[*sv,*vhdl,Path(__file__).resolve(),ROOT/'scripts/test_c64_machine_schedule.py',ROOT/'scripts/test_c64_turbo_exact.py',ROOT/'scripts/run_native_tool.py',cp/'ports/fpga64_sid_iec.vhd',cp/'ports/cpu_6510.vhd',cp/'multitini_c64_adapter.sv',cp/'multitini_c64_cpu_island.sv']
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    before={str(p.relative_to(ROOT)):digest(p) for p in sources}
    receipt={'status':'RUNNING','source_sha256':before}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['PATH']='C:/msys64/ucrt64/bin;'+os.environ.get('PATH',os.environ.get('Path',''))
    def run(name,args):
        result=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*map(str,args)],cwd=out,env=env,capture_output=True,text=True,timeout=600)
        text=result.stdout+result.stderr;(out/(name+'.log')).write_text(text)
        if result.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:|^MISMATCH ',text,re.I|re.M):raise RuntimeError(name+'\n'+text[-8000:])
        return text
    v=Path('C:/Xilinx/2025.2/Vivado/bin')
    try:
        run('xvlog',[v/'xvlog.bat','--nolog','--sv','--relax',*sv])
        run('xvhdl',[v/'xvhdl.bat','--nolog','--2008',*vhdl])
        run('xelab',[v/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','--snapshot','ordering_sim','tb_c64_turbo_io2' if io2 else 'tb_c64_turbo_ordering'])
        text=run('simulate',[v/'xsim.bat','--nolog','ordering_sim','--runall'])
        if 'PASS: accelerated machine ordering' not in text:raise RuntimeError('missing PASS')
        receipt['sources_unchanged']=before=={str(p.relative_to(ROOT)):digest(p) for p in sources}
        if not receipt['sources_unchanged']:raise RuntimeError('sources changed')
        rows=list(csv.DictReader((out/'ordering_trace.csv').open(newline='')))
        mismatch=lambda row: row['native']!=row['accelerated']
        cases={(row['ntsc'],row['badline'],row['index'],row.get('ua','0')) for row in rows}
        required=128 if io2 else 64
        if len(cases)!=required or len(rows)<required*100 or any(mismatch(row) for row in rows):raise RuntimeError('incomplete or unequal traces')
        mutant=dict(rows[len(rows)//2]);mutant['accelerated']=f"{int(mutant['accelerated'],16)^1:07x}"
        if not mismatch(mutant):raise RuntimeError('mutation negative control accepted')
        receipt.update(status='PASS',profile=args.profile,cases=required,trace_rows=len(rows),trace_sha256=digest(out/'ordering_trace.csv'),memory_sha256=digest(out/'machine_memory.hex'),negative_control='PASS: changed accelerated data rejected')
        receipt['checks']=[line for line in text.splitlines() if line.startswith('ORDERING_SUMMARY ')]
        if len(receipt['checks'])!=required:raise RuntimeError('summary count')
        receipt['limits']=('All 256 IO2 addresses each written and read through real REU/UA register decoding, at all indices, PAL/NTSC, BA policies and UA enable settings. Every IO2 transaction consumes a native frame; BA-disabled IO-only ownership is exercised. UA playback and IRQ, REU transfers, mode switches and board acceptance are separate gates.' if io2 else 'Bounded full-machine synthetic instruction stream, all indices and PAL/NTSC badline-policy settings; colour high-nibble masked. This is T3 RAW ordering, not the complete IRQ/collision/UA/REU stress matrix or board acceptance.')
        print(text[-10000:])
    except Exception as error:receipt.update(status='FAIL',error=str(error));raise
    finally:(out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        print('ERROR: '+str(error),file=sys.stderr);raise SystemExit(1)
