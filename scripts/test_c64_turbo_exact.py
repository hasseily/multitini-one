#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare native and external-island C64 machines edge for edge in exact mode."""
from pathlib import Path
import argparse,csv,os,subprocess,sys,re,json,hashlib
import test_c64_machine_schedule as base
ROOT=Path(__file__).resolve().parents[1]

def make_lane() -> str:
    adapter=(ROOT/'hdl/core/c64/multitini_c64_adapter.sv').read_text()
    old=(ROOT/'hdl/sim/tb_c64_machine_schedule.sv').read_text()
    decl=adapter[adapter.index('    localparam integer FIO_DEPTH'):adapter.index('    wire ua_audio_tick')]
    memory=adapter[adapter.index('    multitini_c64_island_memory main_ram_i'):adapter.index('    assign proxy_native_data')]
    links=adapter[adapter.index('    assign proxy_native_data'):adapter.index('    multitini_c64_turbo_regs #')]
    links=links.replace('.diag_armed(cpu_config[19])','.diag_armed(1\'b0)')
    links=links.replace('ext_cs_vic && native_rd_override ? native_rd_data : ext_cpu_di','ext_cpu_di')
    links=links.replace('!fio_rd_sid && fast_rd_override ? fast_rd_data : fio_rd_data','fio_rd_data')
    memory=memory.replace('cart_mem_we && !cart_external_select',"1'b1")
    memory=memory.replace('peek_request && debug_enable',"1'b0").replace('.machine_paused(core_paused)',".machine_paused(1'b0)")
    memory=memory.replace('.peek_addr(peek_address)',".peek_addr(16'd0)").replace('.peek_valid(peek_valid)', '.peek_valid()').replace('.peek_data(peek_data)', '.peek_data()')
    memory=memory.replace('.machine_q(main_ram_din)', '.machine_q(ram_q)')
    memory=memory.replace('prg_ram_wr','loader_we').replace('prg_ram_addr','loader_addr').replace('prg_ram_data','loader_data')
    memory=memory.replace('ram_dout','ram_data').replace('rom_wr','rom_we').replace('rom_bank','bios[0]')
    links=links.replace('prg_install_pause || pause_request','pause').replace('.cart_present(cart_loaded)',".cart_present(1'b0)")
    links=links.replace('.c64_aresetn(!reset)', '.c64_aresetn(reset_n)')
    links=links.replace('core_clk','clk').replace('cpu_clk','fastclk')
    memory=memory.replace('core_clk','clk').replace('cpu_clk','fastclk')
    core=old[old.index('    fpga64_sid_iec dut ('):old.index('    task automatic check')]
    newports=adapter[adapter.index('        .ext_cpu_addr(ext_cpu_addr)',adapter.index('    fpga64_sid_iec #')):adapter.index('        .clk32(core_clk)',adapter.index('    fpga64_sid_iec #'))]
    core=core.replace('fpga64_sid_iec dut (','fpga64_sid_iec #(.EXTERNAL_CPU(EXTERNAL), .FAST_IO(EXTERNAL)) dut (\n'+newports)
    core=core.replace('.turbo_mode(turbo_mode)',".turbo_mode(2'b00)").replace('.turbo_speed(turbo_speed)',".turbo_speed(2'b00)").replace('.local_turbo_allowed(local_turbo_allowed)',".local_turbo_allowed(1'b0)")
    core=core.replace('.nmi_n(1\'b1)', '.nmi_n(nmi_n)').replace('.irq_n(1\'b1)', '.irq_n(irq_n)')
    core=core.replace('.hsync(), .vsync()', '.hsync(hsync), .vsync(vsync)').replace('.r(), .g(), .b()', '.r(red), .g(green), .b(blue)')
    core=core.replace('.audio_l(), .audio_r()', '.audio_l(audio_l), .audio_r(audio_r)')
    core=core.replace('.iec_data_o(),', '.iec_data_o(iec_data),').replace('.iec_clk_o(),','.iec_clk_o(iec_clk),').replace('.iec_atn_o(),','.iec_atn_o(iec_atn),')
    core=core.replace('.pb_o(),','.pb_o(pb_o),').replace('.pb_oe_o(),','.pb_oe_o(pb_oe),')
    ports='''module c64_turbo_exact_lane #(parameter EXTERNAL=0)(
     input wire clk,fastclk,reset_n,pause,memory_wait,ntsc,irq_n,nmi_n,
     input wire [1:0] bios, input wire rom_we,loader_we,
     input wire [13:0] rom_addr,input wire [7:0] rom_data,loader_data,
     input wire [15:0] loader_addr,
     input wire dma_req,dma_we,input wire[15:0] dma_addr,input wire[7:0] dma_data,
     output wire [7:0] dma_result, output wire dma_cycle,
     output wire [15:0] ram_addr, output wire [7:0] ram_data,ram_q,
     output wire ram_ce,ram_we,paused, output wire[4:0] dbg_phase,
     output wire[15:0] dbg_cpu_addr,
     output wire[7:0] dbg_cpu_di,dbg_cpu_do,dbg_cpu_port,dbg_vic_bus,
     output wire dbg_cpu_we,dbg_request,dbg_retire,dbg_reset,dbg_hold,
     output wire dbg_rdy,dbg_cpu_owned,dbg_dma,
     output wire[5:0] dbg_native_ce,output wire[4:0]dbg_io_cs,output wire dbg_io_write,
     output wire hsync,vsync, output wire[7:0]red,green,blue,
     output wire[17:0]audio_l,audio_r,output wire iec_data,iec_clk,iec_atn,
     output wire[7:0]pb_o,pb_oe
    );
    '''
    native=old[old.index('    multitini_c64_local_ram memory_i'):old.index('    fpga64_sid_iec dut')]
    body=ports+decl+'''\nassign policy_turbo=0; assign policy_blocked=0; assign policy_badline=1;
    assign policy_index=0; assign policy_c64u_top=0;
    generate if(EXTERNAL) begin: accelerated
    '''+memory+links+'''end else begin: stock
    assign ext_cpu_addr=0;assign ext_cpu_do=0;assign ext_cpu_io=0;
    assign ext_cpu_we=0;assign ext_cpu_ioonly=0;assign ext_nmi_ack=0;
    assign ext_color_di=0;assign fio_entries=0;assign fio_wp=0;
    assign fio_rd_addr=0;assign fio_rd_sid=0;
    '''+native+'''end endgenerate
    '''+core+'endmodule\n'
    return "// SPDX-License-Identifier: GPL-3.0-only\n`timescale 1ns/1ps\n"+body

def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/c64-turbo-exact')
    args=parser.parse_args()
    OUT=args.build_dir.resolve()
    if not OUT.is_relative_to(ROOT/'build') or OUT==ROOT/'build' or OUT.exists():
        raise ValueError('Require a fresh dedicated directory beneath repository build/')
    OUT.mkdir()
    PREP=OUT
    (PREP/'c64_turbo_exact_lane.sv').write_text(make_lane())
    CP=ROOT/'hdl/core/c64'
    source=(ROOT/'hdl/core/c64/ports/fpga64_sid_iec.vhd').read_text()
    observed=base.instrument(source)
    # Observation-only default external read: the real 6510 port is in the island.
    observed=observed.replace('external_cpu : if EXTERNAL_CPU generate', 'external_cpu : if EXTERNAL_CPU generate')
    marker='ext_cpu_di <= cpuDi;'
    if source.count(marker)!=1:raise RuntimeError('observation anchor')
    observed=observed.replace(marker,marker+"\nexternal_observation: if EXTERNAL_CPU generate observedCpuRead <= cpuDi; end generate;")
    (OUT/'fpga64_sid_iec_observed.vhd').write_text(observed)
    (OUT/'cpu_6510_observed.vhd').write_text(base.instrument_wrapper((ROOT/'hdl/core/c64/ports/cpu_6510.vhd').read_text()))
    memory=bytearray(base.synthetic_memory())
    stop=memory.index(bytes.fromhex('A9 A5 8D 05 21'),0x800)+5
    extra=bytes.fromhex('A2 00 A9 01 9D 00 D8 9D 00 D9 9D 00 DA 9D 00 DB E8 D0 F1 '
        'A9 1B 8D 11 D0 A9 03 8D 15 D0 A9 50 8D 01 D0 8D 03 D0 '
        'A9 40 8D 00 D0 8D 02 D0 A9 00 8D 10 D0 A9 14 8D 18 D0 '
        'A9 0F 8D 18 D4 A9 21 8D 04 D4 A9 F0 8D 06 D4 58 A9 5A 8D 09 21')
    loop=stop+len(extra)
    extra+=bytes.fromhex('EE 20 D0 EE 02 20')+bytes([0x4c,loop&255,loop>>8])
    memory[stop:stop+len(extra)]=extra
    memory[0xb00:0xb04]=bytes.fromhex('EE 00 22 40')
    (OUT/'machine_memory.hex').write_text('\n'.join(f'{b:02x}' for b in memory)+'\n')
    vhdl=[];section=''
    for raw in (ROOT/'hdl/hdl_sources.txt').read_text().splitlines():
        line=raw.strip()
        if line.startswith('['):section=line
        elif section=='[c64]' and line and not line.startswith('#'):
            p=(ROOT/'hdl'/line.split('|')[0].strip()).resolve()
            if p.suffix=='.vhd':
                if p.name=='fpga64_sid_iec.vhd':p=OUT/'fpga64_sid_iec_observed.vhd'
                elif p.name=='cpu_6510.vhd':p=OUT/'cpu_6510_observed.vhd'
                vhdl.append(p)
    sv=[ROOT/p for p in ('third_party/c64/upstream/rtl/mos6526.v','hdl/core/c64/ports/sid_dac.sv','hdl/core/c64/ports/sid_filter.sv','third_party/c64/upstream/rtl/sid/sid_envelope.sv','hdl/core/c64/ports/sid_tables.sv','third_party/c64/upstream/rtl/sid/sid_voice.sv','hdl/core/c64/ports/sid_top.sv','hdl/core/c64/multitini_c64_local_ram.sv','hdl/core/c64/multitini_c64_fio_port.sv','hdl/sim/tb_c64_turbo_exact.sv')]
    sv += [CP/name for name in ('multitini_c64_cpu_island.sv','multitini_c64_cpu_proxy.sv','multitini_c64_island_governor.sv','multitini_c64_island_map.sv','multitini_c64_island_memory.sv','multitini_c64_nmi_delivery.sv')]
    sv += [PREP/'c64_turbo_exact_lane.sv']
    sources=[*sv,*vhdl,Path(__file__).resolve(),ROOT/'scripts/test_c64_machine_schedule.py',ROOT/'scripts/run_native_tool.py',ROOT/'hdl/core/c64/ports/fpga64_sid_iec.vhd',ROOT/'hdl/core/c64/multitini_c64_adapter.sv']
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    before={str(p.relative_to(ROOT)):digest(p) for p in sources}
    receipt={'status':'RUNNING','source_sha256':before}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['PATH']='C:/msys64/ucrt64/bin;'+os.environ.get('PATH',os.environ.get('Path',''))
    def run(name,args):
        p=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*map(str,args)],cwd=OUT,env=env,capture_output=True,text=True,timeout=600)
        text=p.stdout+p.stderr;(OUT/(name+'.log')).write_text(text)
        if p.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:|^MISMATCH ',text,re.I|re.M):raise RuntimeError(name+'\n'+text[-8000:])
        return text
    v=Path('C:/Xilinx/2025.2/Vivado/bin')
    try:
        run('xvlog',[v/'xvlog.bat','--nolog','--sv','--relax',*sv])
        run('xvhdl',[v/'xvhdl.bat','--nolog','--2008',*vhdl])
        run('xelab',[v/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','--snapshot','exact_sim','tb_c64_turbo_exact'])
        text=run('simulate',[v/'xsim.bat','--nolog','exact_sim','--runall'])
        if 'PASS: exact external CPU full-machine' not in text:raise RuntimeError('missing PASS')
        receipt['sources_unchanged']=before=={str(p.relative_to(ROOT)):digest(p) for p in sources}
        if not receipt['sources_unchanged']:raise RuntimeError('sources changed')
        rows=list(csv.DictReader((OUT/'exact_trace.csv').open(newline='')))
        def mismatch(row):
            if row['native_addr']!=row['external_addr'] or row['native_we']!=row['external_we']:
                return True
            return (int(row['native_addr'],16)>1 or row['native_we']=='1') and row['native_data']!=row['external_data']
        if len(rows)<1000 or set(row['ntsc'] for row in rows)!={'0','1'} or any(mismatch(row) for row in rows):
            raise RuntimeError('Incomplete or unequal accepted bus trace')
        mutant=dict(rows[len(rows)//2]);mutant['external_addr']=f"{int(mutant['external_addr'],16)^1:04x}"
        if not mismatch(mutant):raise RuntimeError('Trace mutation negative control was accepted')
        receipt['trace_rows']=len(rows);receipt['trace_sha256']=digest(OUT/'exact_trace.csv')
        receipt['memory_sha256']=digest(OUT/'machine_memory.hex')
        receipt['checks']=[line for line in text.splitlines() if line.startswith('EXACT_SUMMARY ')]
        if len(receipt['checks'])!=2:raise RuntimeError('Missing PAL/NTSC summaries')
        receipt['negative_control']='PASS: one-bit external address corruption rejected'
        receipt['limits']='Exact policy only, actual CPU/VIC/SID/CIA machine plus island/proxy/memory wiring extracted from production adapter; synthetic ROM/program, no drive/CRT/REU controller or physical-board claim.'
        receipt['status']='PASS';print(text[-5000:])
    except Exception as e:
        receipt['status']='FAIL';receipt['error']=str(e);raise
    finally:
        (OUT/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        print('ERROR: '+str(error),file=sys.stderr);raise SystemExit(1)
