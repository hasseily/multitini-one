#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run actual REU-triggered DMA through the production accelerated CPU and native machine."""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,shutil,subprocess,sys
import test_c64_machine_schedule as base
import test_c64_turbo_ordering as ordering
ROOT=Path(__file__).resolve().parents[1]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def fixture(fetch,deferred):
    mem=bytearray([0xea]*65536)
    boot=bytes.fromhex('78 D8 A2 FF 9A A9 2F 85 00 A9 37 85 01 A9 1B 8D 11 D0 A9 00 8D 15 D0 8D 1A D0 AD 1E D0 AD 1F D0 A2 FF CA D0 FD 4C 00 08')
    mem[0x700:0x700+len(boot)]=boot
    code=bytearray()
    def store(address,value):code.extend([0xa9,value,0x8d,address&255,address>>8])
    for address,value in [(0xdf02,0),(0xdf03,0x30 if fetch else 0x31),(0xdf04,0),(0xdf05,0 if fetch else 1),(0xdf06,0),(0xdf07,64),(0xdf08,0),(0xdf09,0),(0xdf0a,0)]:store(address,value)
    command=0x80+(1 if fetch else 0)+(0 if deferred else 0x10)
    if deferred:store(0xdf01,command)
    # Wait for a complete raster-zero boundary, then line50. All cases
    # therefore give DEN a preceding line48 latch before transfer start.
    # Check raster bit8 before and after low-byte zero: D012 alone also
    # reports zero on line256, followed by low-byte50 on PAL line306.
    code+=bytes.fromhex('AD 11 D0 30 FB AD 12 D0 D0 F6 AD 11 D0 30 F1 AD 12 D0 C9 32 D0 F9')
    store(0xff00,0xa5) if deferred else store(0xdf01,command)
    store(0x2106,0x5a)
    code+=bytes.fromhex('AD 00 DF 8D 07 21')
    for i in range(64):code+=bytes([0xad,i,0x30 if fetch else 0x31,0x8d,i,0x22])
    store(0x2105,0xa5);end=0x800+len(code);code+=bytes([0x4c,end&255,end>>8]);mem[0x800:0x800+len(code)]=code
    for i in range(64):mem[0x3000+i]=0x10+i;mem[0x3100+i]=0x40+i
    return mem


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args();out=a.build_dir.resolve()
    if out==ROOT/'build' or not out.is_relative_to(ROOT/'build') or out.exists():p.error('fresh build child required')
    out.mkdir(parents=True);cp=ROOT/'hdl/core/c64';receipt={'schema':1,'status':'FAIL','source_sha256':{},'sources_unchanged':False}
    try:
        lane=ordering.make_lane(io2=True)
        lane=lane.replace(".ff00_write(1'b0)",".ff00_write(ext_cpu_bus_valid && ext_cpu_we && ext_cpu_addr==16'hff00)",1)
        lane=lane.replace('wire test_iof;',"""wire test_iof;
    wire reu_dma_req,reu_dma_valid,reu_dma_idle,reu_dma_peek,reu_dma_last_write,reu_dma_we,reu_dma_ack;
    wire [15:0]reu_dma_addr; wire [7:0]reu_dma_data,reu_dma_result;
    wire [23:0]reu_memory_addr; wire reu_memory_write,reu_memory_valid;
    wire [63:0]reu_memory_wdata; wire [7:0]reu_memory_wstrb;
    wire reu_memory_ready; reg reu_memory_rvalid=0; reg [63:0]reu_memory_rdata=0;
    reg [7:0]reu_memory[0:511];
    reg pending=0; integer delay_count=0,requests=0,write_bytes=0;
    reg [23:0]pending_addr;reg pending_write;reg [63:0]pending_data;reg [7:0]pending_strb;
    assign reu_memory_ready=!pending&&!ext_reset;
    always @(posedge clk)begin
        reu_memory_rvalid<=0;
        if(ext_reset)begin pending<=0;requests<=0;write_bytes<=0;end
        else begin
            if(reu_memory_valid&&reu_memory_ready)begin
                if(pending||reu_memory_addr>504)$fatal(1,"REU bounded-store protocol");
                pending<=1;pending_addr<=reu_memory_addr;pending_write<=reu_memory_write;pending_data<=reu_memory_wdata;pending_strb<=reu_memory_wstrb;
                delay_count<=45+(requests%3)*17;requests<=requests+1;
            end
            if(pending)begin
                if(delay_count>0)delay_count<=delay_count-1;
                else begin
                    for(integer b=0;b<8;b++)begin
                        if(pending_write&&pending_strb[b])begin reu_memory[pending_addr+b]=pending_data[b*8+:8];write_bytes++;end
                        reu_memory_rdata[b*8+:8]<=reu_memory[pending_addr+b];
                    end
                    pending<=0;reu_memory_rvalid<=1;
                end
            end
        end
    end
""",1)
        lane=lane.replace('wire test_reu_select=test_iof&&','wire test_reu_select=test_iof&&!reu_dma_req&&',1)
        lane=lane.replace('.dma_req(),.dma_valid(),.dma_idle(),.dma_peek(),.dma_last_write(),','.dma_req(reu_dma_req),.dma_valid(reu_dma_valid),.dma_idle(reu_dma_idle),.dma_peek(reu_dma_peek),.dma_last_write(reu_dma_last_write),',1)
        lane=lane.replace(".dma_addr(),.dma_write(),.dma_wdata(),.dma_ack(1'b0),.dma_rdata(8'd0)",'.dma_addr(reu_dma_addr),.dma_write(reu_dma_we),.dma_wdata(reu_dma_data),.dma_ack(reu_dma_ack),.dma_rdata(reu_dma_result)',1)
        lane=lane.replace(".memory_addr(),.memory_write(),.memory_wdata(),.memory_wstrb(),.memory_valid(),\n        .memory_ready(1'b1),.memory_rdata(64'd0),.memory_rvalid(1'b0)",'.memory_addr(reu_memory_addr),.memory_write(reu_memory_write),.memory_wdata(reu_memory_wdata),.memory_wstrb(reu_memory_wstrb),.memory_valid(reu_memory_valid),\n        .memory_ready(reu_memory_ready),.memory_rdata(reu_memory_rdata),.memory_rvalid(reu_memory_rvalid)',1)
        lane=lane.replace('.dma_req(dma_req), .dma_cycle(dma_cycle), .dma_addr(dma_addr),','.dma_valid(reu_dma_valid),.dma_idle(reu_dma_idle),.dma_peek(reu_dma_peek),.dma_last_write(reu_dma_last_write),.dma_ack(reu_dma_ack),\n        .dma_req(reu_dma_req), .dma_cycle(dma_cycle), .dma_addr(reu_dma_addr),',1)
        lane=lane.replace('.dma_dout(dma_data), .dma_din(dma_result), .dma_we(dma_we)','.dma_dout(reu_dma_data), .dma_din(reu_dma_result), .dma_we(reu_dma_we)',1)
        (out/'c64_turbo_exact_lane.sv').write_text(lane)
        observed=base.instrument((cp/'ports/fpga64_sid_iec.vhd').read_text()).replace('ext_cpu_di <= cpuDi;','ext_cpu_di <= cpuDi;\nexternal_observation: if EXTERNAL_CPU generate observedCpuRead <= cpuDi; end generate;')
        (out/'fpga64_sid_iec_observed.vhd').write_text(observed)
        (out/'cpu_6510_observed.vhd').write_text(base.instrument_wrapper((cp/'ports/cpu_6510.vhd').read_text()))
        for fetch in range(2):
            for deferred in range(2):(out/f'dma-{fetch}-{deferred}.hex').write_text(''.join(f'{b:02x}\n' for b in fixture(fetch,deferred)))
        vhdl=[];section=''
        for raw in (ROOT/'hdl/hdl_sources.txt').read_text().splitlines():
            line=raw.strip()
            if line.startswith('['):section=line
            elif section=='[c64]' and line and not line.startswith('#'):
                src=(ROOT/'hdl'/line.split('|')[0].strip()).resolve()
                if src.suffix=='.vhd':vhdl.append(src)
        sv=[ROOT/p for p in ('third_party/c64/upstream/rtl/mos6526.v','hdl/core/c64/ports/sid_dac.sv','hdl/core/c64/ports/sid_filter.sv','third_party/c64/upstream/rtl/sid/sid_envelope.sv','hdl/core/c64/ports/sid_tables.sv','third_party/c64/upstream/rtl/sid/sid_voice.sv','hdl/core/c64/ports/sid_top.sv','hdl/core/c64/multitini_c64_local_ram.sv','hdl/core/c64/multitini_c64_fio_port.sv','hdl/sim/tb_c64_turbo_dma.sv')]
        sv += [cp/f'multitini_c64_{n}.sv' for n in ('cpu_island','cpu_proxy','island_governor','island_map','island_memory','nmi_delivery','turbo_regs','reu','ua','ua_voice','ua_fetch','ua_mix')]
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
        run('xelab',[v/'xelab.bat','--nolog','--relax','--mt','off','--timescale','1ns/1ps','-s','dma_sim','tb_c64_turbo_dma'])
        log=run('simulate',[v/'xsim.bat','--nolog','dma_sim','--runall'])
        if 'TURBO_DMA PASS cases=24' not in log:raise RuntimeError('incomplete DMA matrix')
        rows=list(csv.DictReader((out/'dma_summary.csv').open()));traces=list(csv.DictReader((out/'dma_trace.csv').open()))
        expected={(str(v),str(m),str(f),str(d)) for v in range(2) for m in range(3) for f in range(2) for d in range(2)}
        if len(rows)!=24 or {(r['ntsc'],r['mode'],r['fetch'],r['deferred']) for r in rows}!=expected:raise RuntimeError('DMA matrix inventory')
        if len(traces)!=1536:raise RuntimeError('DMA byte trace count')
        def trace_ok(r):
            offset=int(r['byte']);fetch=int(r['fetch'])
            return int(r['addr'],16)==(0x3000 if fetch else 0x3100)+offset and int(r['data'],16)==(0x80 if fetch else 0x40)+offset and int(r['write'])==fetch
        if not all(trace_ok(r) for r in traces):raise RuntimeError('DMA trace diverges from exact fixture bytes')
        corrupted={**traces[100],'data':f"{int(traces[100]['data'],16)^1:02x}"}
        if trace_ok(corrupted):raise RuntimeError('wrong-byte negative control accepted')
        receipt.update(cases=rows,case_count=24,dma_bytes=1536,negative_control='One bit flipped in an equal DMA byte is rejected by the identical byte/address/direction predicate.',
            artifact_sha256={q.name:digest(q) for q in out.iterdir() if q.suffix in ('.csv','.hex') or q.name=='c64_turbo_exact_lane.sv' or q.name.endswith('_observed.vhd')})
        receipt['sources_unchanged']=all(digest(ROOT/k)==v for k,v in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:raise RuntimeError('source changed')
        receipt['limits']=['Actual production REU/T65/island/proxy/FIO/VIC/SID/CIA and adapter-derived wiring, synthetic ROM/program.',
            '64-byte fetch and stash, immediate command and deferred FF00 trigger; exact plus index15 with both BA policies, PAL/NTSC. Actual VIC BA steals and native clock cadence checked.',
            'Acknowledged single-outstanding backing store deliberately injects45-79 native-clock response delays; actual DDR/cache throughput and hard/soft-reset drain are separate tests.',
            'Not exhaustive E-BA3: no swap/verify, transfer wrap, all bank combinations or NMI during DMA in this focused suite. No physical board claim.']
        receipt['status']='PASS'
    except Exception as e:receipt['error']=repr(e)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({k:v for k,v in receipt.items() if k not in ('source_sha256','cases','artifact_sha256')},indent=2));return 0 if receipt['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
