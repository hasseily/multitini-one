#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure #203 border pixels from real T65/island/FIO/VIC at the top rows."""
from pathlib import Path
from collections import Counter
import argparse,csv,hashlib,json,os,re,subprocess,sys
import test_c64_machine_schedule as native
import test_c64_turbo_ordering as ordering
ROOT=ordering.ROOT

def bench():
    source=(ROOT/'hdl/sim/tb_c64_turbo_ordering.sv').read_text()
    prefix=source[:source.index('    reg [24:0] bus_trace')]
    prefix=prefix.replace('module tb_c64_turbo_ordering','module tb_c64_turbo_pixels')
    prefix=prefix.replace('for(genvar g=0;g<2;g=g+1)','for(genvar g=1;g<2;g=g+1)')
    prefix=prefix.replace('reg [3:0] rate=0;reg badline=1;','reg [3:0] rate=14;reg badline=0;reg u64=0;wire [3:0] observed_color;')
    prefix=prefix.replace("32'h410 | {28'd0,rate}","32'h410 | (u64?32'h80:0) | {28'd0,rate}")
    prefix=prefix.replace('.clk(clk),.fastclk(fastclk),','.observe_color(observed_color),.clk(clk),.fastclk(fastclk),',1)
    loader=source[source.index('    task automatic load_all;'):source.index('\n    initial begin\n        $readmemh')]
    loader=loader.replace("a==14'h3ffd ? 8'h08","a==14'h3ffd ? 8'h07")
    return prefix+loader+'''
    integer fd,native_tick=0,landings=0,completed=0,case_count=0;
    always @(posedge clk)begin
        native_tick++;
        if(comparing&&lane[1].dut.fio_u64_wr&&lane[1].dut.fio_u64_a==10'h020)begin
            landings++;
            $fdisplay(fd,"%0d,%0d,%0d,L,%0d,%0d",ntsc,rate,u64,native_tick,lane[1].dut.fio_u64_d);
        end
        if(comparing&&dbg_native_ce[1][4]&&landings>=20&&landings<1200)begin
            #1;$fdisplay(fd,"%0d,%0d,%0d,P,%0d,%0d",ntsc,rate,u64,native_tick,observed_color);
        end
    end
    always @(posedge fastclk)if(comparing&&lane[1].dut.accelerated.cpu_island_i.cpu_enable&&
        lane[1].dut.accelerated.cpu_island_i.cpu_write&&lane[1].dut.accelerated.cpu_island_i.cpu_address==16'h2105)
        completed=1;
    initial begin
        $readmemh("memory.hex",initial_ram);
        fd=$fopen("pixels.csv","w");if(!fd)$fatal(1,"pixel trace open");
        $fdisplay(fd,"ntsc,index,u64,kind,tick,value");
        for(integer standard=0;standard<2;standard++)begin
            ntsc=standard;reset_n=0;pause=1;load_all();
            for(integer row=0;row<4;row++)begin
                comparing=0;reset_n=0;pause=1;rate=row<2?14:15;u64=row[0];
                repeat(160)@(negedge clk);
                native_tick=0;landings=0;completed=0;comparing=1;reset_n=1;pause=0;
                repeat(40000)begin @(negedge clk);if(completed)break;end
                if(!completed||landings!=1200)$fatal(1,"pixel coverage done=%0d landings=%0d",completed,landings);
                comparing=0;pause=1;case_count++;
                $display("PIXEL_SUMMARY ntsc=%0d index=%0d u64=%0d landings=%0d",ntsc,rate,u64,landings);
            end
        end
        $fclose(fd);if(case_count!=8)$fatal(1,"pixel cases");
        $display("PASS pixel trace cases=8");$finish;
    end
    initial begin #50000000;$fatal(1,"pixel timeout");end
endmodule
'''

def compare(path):
    groups={}
    for r in csv.DictReader(path.open()):groups.setdefault((r['ntsc'],r['index'],r['u64']),[]).append(r)
    if len(groups)!=8:raise ValueError('Incomplete pixel cases')
    results=[]
    for key,rows in groups.items():
        lands=[(int(r['tick']),int(r['value'])) for r in rows if r['kind']=='L']
        pixels=[(int(r['tick']),int(r['value'])) for r in rows if r['kind']=='P']
        if len(lands)!=1200 or len(pixels)<800:raise ValueError('Pixel/landing coverage')
        if [v for _,v in lands]!=[1,2]*600:raise ValueError('Lost/reordered border writes')
        cursor=0;current=0;runs=[];segment=[];checked=0
        def close():
            if len(segment)>2:
                lengths=[];length=1
                for a,b in zip(segment,segment[1:]):
                    if a==b:length+=1
                    else:lengths.append(length);length=1
                # Discard both boundary runs where blanking cuts the pattern.
                runs.extend(lengths[1:])
            segment.clear()
        for tick,value in pixels:
            while cursor<len(lands) and lands[cursor][0]<tick:current=lands[cursor][1];cursor+=1
            if not value:close();continue
            if value!=current:raise ValueError(f'Pixel landing latency {key} tick{tick}: {value}!={current}')
            checked+=1;segment.append(value)
        close();hist=Counter(runs)
        if checked<600 or not hist or max(hist)>2:raise ValueError('Pixel run coverage/range')
        index=int(key[1]);u64=int(key[2])
        if index==14 and not u64 and set(hist)!={1}:raise ValueError('48-row must have only single-pixel runs')
        if index==14 and u64 and (hist[2]<10 or not 38<hist[1]/hist[2]<55):raise ValueError('47-row double-pixel cadence')
        if index==15 and (hist[1]<100 or hist[2]<100 or not .7<hist[1]/hist[2]<1.4):raise ValueError('64/63-row skipped-colour pattern')
        results.append({'ntsc':int(key[0]),'index':index,'c64u_top':u64,'landings':len(lands),'pixels_checked':checked,'run_lengths':dict(sorted(hist.items()))})
    return results

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build-dir',type=Path,required=True)
    args=ap.parse_args();out=args.build_dir.resolve()
    if out.exists() or out==ROOT/'build' or not out.is_relative_to(ROOT/'build'):raise ValueError('Fresh dedicated build directory required')
    out.mkdir(parents=True);cp=ROOT/'hdl/core/c64'
    lane=ordering.make_lane().replace('input wire [31:0] test_config,','output wire[3:0] observe_color,input wire [31:0] test_config,',1)
    marker=lane.index('    fpga64_sid_iec #')
    lane=lane[:marker]+lane[marker:].replace('.ext_cpu_addr(ext_cpu_addr)', '.observe_color(observe_color),.ext_cpu_addr(ext_cpu_addr)',1)
    (out/'lane.sv').write_text(lane);(out/'bench.sv').write_text(bench())
    source=(cp/'ports/fpga64_sid_iec.vhd').read_text();observed=native.instrument(source)
    observed=observed.replace('port(\n','port(\n    observe_color : out unsigned(3 downto 0);\n',1)
    observed=observed.replace('\nbegin\n','\nbegin\n    observe_color <= vicColorIndex;\n',1)
    (out/'fpga64_observed.vhd').write_text(observed)
    (out/'cpu_observed.vhd').write_text(native.instrument_wrapper((cp/'ports/cpu_6510.vhd').read_text()))
    memory=bytearray([0xea]*65536)
    boot=bytes.fromhex('78 D8 A9 2F 85 00 A9 37 85 01 A9 0B 8D 11 D0 A2 FF CA D0 FD 4C 00 08')
    memory[0x700:0x700+len(boot)]=boot
    code=bytes.fromhex('A9 01 8D 20 D0 A9 02 8D 20 D0')*600+bytes.fromhex('A9 A5 8D 05 21 4C 75 1F')
    memory[0x800:0x800+len(code)]=code
    (out/'memory.hex').write_text('\n'.join(f'{b:02x}' for b in memory)+'\n')
    vhdl=[];section=''
    for raw in (ROOT/'hdl/hdl_sources.txt').read_text().splitlines():
        line=raw.strip()
        if line.startswith('['):section=line
        elif section=='[c64]' and line and not line.startswith('#'):
            p=(ROOT/'hdl'/line.split('|')[0].strip()).resolve()
            if p.suffix=='.vhd':
                if p.name=='fpga64_sid_iec.vhd':p=out/'fpga64_observed.vhd'
                elif p.name=='cpu_6510.vhd':p=out/'cpu_observed.vhd'
                vhdl.append(p)
    sv=[ROOT/p for p in ('third_party/c64/upstream/rtl/mos6526.v','hdl/core/c64/ports/sid_dac.sv','hdl/core/c64/ports/sid_filter.sv','third_party/c64/upstream/rtl/sid/sid_envelope.sv','hdl/core/c64/ports/sid_tables.sv','third_party/c64/upstream/rtl/sid/sid_voice.sv','hdl/core/c64/ports/sid_top.sv','hdl/core/c64/multitini_c64_local_ram.sv','hdl/core/c64/multitini_c64_fio_port.sv')]
    sv += [cp/name for name in ('multitini_c64_cpu_island.sv','multitini_c64_cpu_proxy.sv','multitini_c64_island_governor.sv','multitini_c64_island_map.sv','multitini_c64_island_memory.sv','multitini_c64_nmi_delivery.sv','multitini_c64_turbo_regs.sv')]
    sv += [out/'lane.sv',out/'bench.sv']
    paths=[*sv,*vhdl,Path(__file__).resolve(),ROOT/'scripts/test_c64_turbo_ordering.py',ROOT/'scripts/test_c64_turbo_exact.py',ROOT/'scripts/test_c64_machine_schedule.py',ROOT/'scripts/run_native_tool.py',ROOT/'hdl/sim/tb_c64_turbo_ordering.sv',cp/'multitini_c64_adapter.sv',cp/'ports/fpga64_sid_iec.vhd',cp/'ports/cpu_6510.vhd']
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();hashes=lambda:{p.relative_to(ROOT).as_posix():digest(p) for p in paths}
    initial=hashes();receipt={'status':'FAIL','source_sha256':initial}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['PATH']='C:/msys64/ucrt64/bin;'+os.environ.get('PATH',os.environ.get('Path',''))
    def run(name,args):
        r=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*map(str,args)],cwd=out,env=env,capture_output=True,text=True,timeout=600)
        text=r.stdout+r.stderr;(out/(name+'.log')).write_text(text)
        if r.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:',text,re.I):raise RuntimeError(name+'\n'+text[-4000:])
        return text
    try:
        v=Path('C:/Xilinx/2025.2/Vivado/bin')
        run('xvlog',[v/'xvlog.bat','--nolog','--sv','--relax',*sv]);run('xvhdl',[v/'xvhdl.bat','--nolog','--2008',*vhdl])
        run('xelab',[v/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','--snapshot','pixel_sim','tb_c64_turbo_pixels'])
        text=run('simulate',[v/'xsim.bat','--nolog','pixel_sim','--runall'])
        if 'PASS pixel trace cases=8' not in text:raise ValueError('Missing complete pixel run')
        cases=compare(out/'pixels.csv')
        if hashes()!=initial:raise ValueError('Sources changed')
        receipt.update(status='PASS',cases=cases,sources_unchanged=True,trace_sha256=digest(out/'pixels.csv'),limits='Synthetic unrolled #203 instruction pattern through real CPU/FIO/VIC; blanked pixels excluded and partial boundary runs dropped. No C64U measurement or board acceptance claim.')
        print(json.dumps(cases))
    except Exception as e:receipt['error']=str(e);raise
    finally:(out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as e:print('ERROR: '+str(e),file=sys.stderr);raise SystemExit(1)
