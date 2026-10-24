#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real CIA/VIC/SID NMI and IRQ digi at 64x, including BA-low native acknowledgements."""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,shutil,subprocess,sys
import test_c64_machine_schedule as base
import test_c64_turbo_ordering as ordering
ROOT=Path(__file__).resolve().parents[1]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def program(ntsc,irq,badline):
    mem=bytearray([0xea]*65536);code=bytearray.fromhex('78 D8 A2 FF 9A A9 2F 85 00 A9 35 85 01')
    def store(a,v):code.extend([0xa9,v,0x8d,a&255,a>>8])
    for a,v in [(0xfffa,0),(0xfffb,0x10),(0xfffe,0),(0xffff,0x10),
                (0xdc0d,0x7f),(0xdd0d,0x7f),(0xdc0e,0),(0xdd0e,0),(0xd01a,0),(0xd019,15),
                (0xd011,0x1b),(0xd016,8),(0xd018,0x14),(0xd015,0xff),(0xd017,0xff),
                (0xd010,0),(0xd01b,0),(0xd01c,0),(0xd01d,0),(0xd031,0x0f if badline else 0x8f)]:store(a,v)
    for spr in range(8):
        store(0xd000+2*spr,40+24*spr);store(0xd001+2*spr,0x50);store(0x7f8+spr,0x30)
    timer=0xdc00 if irq else 0xdd00
    store(timer+4,127 if ntsc else 122);store(timer+5,0);store(timer+13,0x81)
    code.extend([0xad,13,timer>>8]);store(timer+14,0x11)
    code.extend(bytes.fromhex('58 A9 A5 8D 05 21 A9 0F'))
    loop=0x800+len(code);code.extend([0x4c,loop&255,loop>>8]);mem[0x800:0x800+len(code)]=code
    handler=bytes([0x48,0x8d,0x18,0xd4,0xad,0x0d,timer>>8,0x68,0x40]);mem[0x1000:0x1000+len(handler)]=handler
    mem[0xc00:0xc40]=bytes([0xff]*64)
    return mem

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args();out=a.build_dir.resolve()
    if out==ROOT/'build' or not out.is_relative_to(ROOT/'build') or out.exists():p.error('fresh build child required')
    out.mkdir(parents=True);cp=ROOT/'hdl/core/c64';receipt={'schema':1,'status':'FAIL','source_sha256':{},'sources_unchanged':False}
    try:
        lane=ordering.make_lane()
        declaration='output wire digi_sid_wr, output wire [9:0] digi_sid_a, output wire [7:0] digi_sid_d, output wire digi_badline, output wire [7:0] digi_sprite_dma,\n     '
        lane=lane.replace('input wire [31:0] test_config,',declaration+'input wire [31:0] test_config,',1)
        lane=lane.replace('    fpga64_sid_iec #','    fpga64_sid_iec #',1)
        anchor='        .ext_cpu_addr(ext_cpu_addr), .ext_cpu_do(ext_cpu_do), .ext_cpu_we(ext_cpu_we),'
        assert lane.count(anchor)==1
        lane=lane.replace(anchor,'.digi_sid_wr(digi_sid_wr),.digi_sid_a(digi_sid_a),.digi_sid_d(digi_sid_d),.digi_badline(digi_badline),.digi_sprite_dma(digi_sprite_dma),\n'+anchor,1)
        (out/'c64_turbo_exact_lane.sv').write_text(lane)
        original=(cp/'ports/fpga64_sid_iec.vhd').read_text();observed=base.instrument(original)
        observed=observed.replace('ext_cpu_di <= cpuDi;','ext_cpu_di <= cpuDi;\nexternal_observation: if EXTERNAL_CPU generate observedCpuRead <= cpuDi; end generate;')
        ports='digi_sid_wr : out std_logic; digi_sid_a : out unsigned(9 downto 0); digi_sid_d : out unsigned(7 downto 0); digi_badline : out std_logic; digi_sprite_dma : out unsigned(7 downto 0);\n'
        assignments='digi_sid_wr <= fastSidWr; digi_sid_a <= fastSidA; digi_sid_d <= fastSidD;\n'
        before=observed;observed=observed.replace('port(\n','port(\n'+ports,1).replace('\nbegin\n','\nbegin\n'+assignments,1)
        assert observed.replace(ports,'',1).replace(assignments,'',1)==before
        connection='digi_badline => digi_badline, digi_sprite_dma => digi_sprite_dma,\n'
        assert observed.count('\tba_dma => ba_dma,')==1
        before=observed;observed=observed.replace('\tba_dma => ba_dma,',connection+'\tba_dma => ba_dma,')
        assert observed.replace(connection,'',1)==before
        (out/'fpga64_sid_iec_observed.vhd').write_text(observed)
        original=(cp/'ports/video_vicII_656x.vhd').read_text()
        ports='digi_badline : out std_logic; digi_sprite_dma : out unsigned(7 downto 0);\n'
        assignments="digi_badline <= '1' when badLine else '0';\ndigi_observe_sprite: for n in 0 to 7 generate digi_sprite_dma(n) <= '1' when MDMA(n) else '0'; end generate;\n"
        assert original.count('\tport (\n')==1 and original.count('\nbegin\n-- -----------------------------------------------------------------------')==1
        observed=original.replace('\tport (\n','\tport (\n'+ports,1).replace('\nbegin\n','\nbegin\n'+assignments,1)
        assert observed.replace(ports,'',1).replace(assignments,'',1)==original
        (out/'video_vicII_656x_observed.vhd').write_text(observed)
        (out/'cpu_6510_observed.vhd').write_text(base.instrument_wrapper((cp/'ports/cpu_6510.vhd').read_text()))
        for ntsc in range(2):
            for irq in range(2):
                for bad in range(2):(out/f'digi-{ntsc}-{irq}-{bad}.hex').write_text(''.join(f'{b:02x}\n' for b in program(ntsc,irq,bad)))
        vhdl=[];section=''
        for raw in (ROOT/'hdl/hdl_sources.txt').read_text().splitlines():
            line=raw.strip()
            if line.startswith('['):section=line
            elif section=='[c64]' and line and not line.startswith('#'):
                src=(ROOT/'hdl'/line.split('|')[0].strip()).resolve()
                if src.suffix=='.vhd':vhdl.append(src)
        sv=[ROOT/p for p in ('third_party/c64/upstream/rtl/mos6526.v','hdl/core/c64/ports/sid_dac.sv','hdl/core/c64/ports/sid_filter.sv','third_party/c64/upstream/rtl/sid/sid_envelope.sv','hdl/core/c64/ports/sid_tables.sv','third_party/c64/upstream/rtl/sid/sid_voice.sv','hdl/core/c64/ports/sid_top.sv','hdl/core/c64/multitini_c64_local_ram.sv','hdl/core/c64/multitini_c64_fio_port.sv','hdl/sim/tb_c64_turbo_digi.sv')]
        sv += [cp/f'multitini_c64_{name}.sv' for name in ('cpu_island','cpu_proxy','island_governor','island_map','island_memory','nmi_delivery','turbo_regs')]
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
        run('xvhdl',[v/'xvhdl.bat','--nolog','--2008',*[out/'fpga64_sid_iec_observed.vhd' if s.name=='fpga64_sid_iec.vhd' else out/'cpu_6510_observed.vhd' if s.name=='cpu_6510.vhd' else out/'video_vicII_656x_observed.vhd' if s.name=='video_vicII_656x.vhd' else snap(s) for s in vhdl]])
        run('xelab',[v/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','-s','digi_sim','tb_c64_turbo_digi'])
        log=run('simulate',[v/'xsim.bat','--nolog','digi_sim','--runall'])
        if 'TURBO_DIGI PASS cases=8' not in log:raise RuntimeError('incomplete digi run')
        rows=[{k:int(v) for k,v in row.items()} for row in csv.DictReader((out/'digi_summary.csv').open())]
        if len(rows)!=8 or {(r['ntsc'],r['irq'],r['badline']) for r in rows}!={(v,i,b) for v in range(2) for i in range(2) for b in range(2)}:raise RuntimeError('digi case coverage')
        for r in rows:
            if r['variation']!=r['max_interval']-r['min_interval'] or r['samples']<200 or r['ba_low_ticks']<10000 or r['badline_ticks']<10000 or r['sprite_dma_seen']!=255:raise RuntimeError('invalid measured interval')
            if r['expected_period']!=(128 if r['ntsc'] else 123)*64 or abs(r['cia_acks']-r['samples'])>1 or abs(r['timer_edges']-r['samples'])>1:raise RuntimeError('timer event/ack relationship')
            if not r['badline'] and (abs(r['span_error'])>64 or r['variation']>64 or min(r['io_low_acks'],r['sid_low_writes'],r['cpu_low_enables'])<=0):raise RuntimeError('8F jitter/ownership gate')
            if r['badline'] and r['variation']<=64:raise RuntimeError('0F stall control not sensitive')
        receipt['sources_unchanged']=all(digest(ROOT/k)==v for k,v in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:raise RuntimeError('source changed')
        receipt.update(status='PASS',cases=rows,case_count=8,negative_control='D031=0F must expose greater than 64-fast-clock interval variation on both CIA and video variants',artifact_sha256={q.name:digest(q) for q in out.iterdir() if q.suffix in ('.csv','.hex') or q.name.endswith('_observed.vhd') or q.name=='c64_turbo_exact_lane.sv'})
        receipt['limits']=['Actual production T65/island/proxy, CIA timers, VIC badlines/eight sprites, FIO and SID write landing; original synthetic program/ROM.','Two native frames per case, PAL/NTSC NMI and IRQ at approximately 8 kHz (integer native timer divisors).','The 64-fast-clock bound is write-interval variation, not total interrupt handler duration.','Simulation gate only; board audio/growl and broad compatibility remain separate.']
    except Exception as e:receipt['error']=repr(e)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({k:v for k,v in receipt.items() if k!='source_sha256'},indent=2));return 0 if receipt['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
