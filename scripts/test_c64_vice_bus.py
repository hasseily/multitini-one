#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare production REU/arbitration native transactions and release to VICE 3.10."""
from __future__ import annotations
import argparse,csv,json,os,re,subprocess,sys
from pathlib import Path
from c64_vice_oracle import ROOT,REFERENCE,COMMIT,digest,verify_reference
from test_c64_vice_reu import extract

def production_slice():
    source=(ROOT/'hdl/core/c64/ports/fpga64_sid_iec.vhd').read_text()
    enum=source[source.index('type sysCycleDef is ('):source.index('signal sysCycle')]
    declarations=source[source.index('signal dma_active'):source.index('signal bus_access_valid')]
    start=source.index('-- PHI0/2-clock emulation')
    body=source[start:source.index('\nend process;',start)+len('\nend process;')]
    ack=re.findall(r'^dma_ack <=.*?;',source,re.M|re.S)
    access=re.findall(r'^bus_access_valid <=.*?;',source,re.M|re.S)
    peek='\n'.join(re.findall(r'^dma_peek_(?:issue|retire|io) <=.*?;',source,re.M|re.S))
    clock_assign='\n'.join(re.findall(r'^(?:cpu_cyc|enableCpu) <=.*?;',source,re.M|re.S))
    if len(ack)!=1 or len(access)!=1:raise ValueError('Production extraction anchors changed')
    return '''library ieee;use ieee.std_logic_1164.all;use ieee.numeric_std.all;
entity production_arbiter is port(clk32,reset:in std_logic;phase:in unsigned(4 downto 0);
baLoc,ba_reu,dma_req,dma_valid,dma_idle,dma_peek,dma_last_write,dma_we:in std_logic;
dma_ack,owned,active,access_valid:out std_logic);end;
architecture rtl of production_arbiter is
'''+enum+declarations+'''signal sysCycle:sysCycleDef;
signal memory_wait_q:std_logic:='0';signal cpuWe_pre:std_logic:='0';
signal phi0_cpu,cpuHasBus,enableCpu,bus_access_valid,cpu_cyc,scheduledCpuRequest,scheduledCpuRetire:std_logic;
begin
sysCycle<=sysCycleDef'val(to_integer(phase));
scheduler:entity work.multitini_c64_cpu_scheduler port map(clk32,reset,'0',phase,'1','1',"000",'0','0',scheduledCpuRequest,scheduledCpuRetire);
owned<=cpuHasBus;active<=dma_active;access_valid<=bus_access_valid;
'''+body+'\n'+ack[0]+'\n'+access[0]+'\n'+peek+'\n'+clock_assign+'\nend;\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/c64-vice-bus')
    args=parser.parse_args();out=args.build_dir.resolve()
    if out==ROOT/'build' or not out.is_relative_to(ROOT/'build'):raise ValueError('Dedicated build/ child required')
    out.mkdir(parents=True,exist_ok=True);verify_reference()
    ref=REFERENCE/'upstream/vice/src/c64/cart/reu.c'
    inputs=[Path(__file__),ROOT/'scripts/test_c64_vice_reu.py',ROOT/'scripts/c64_vice_oracle.py',ROOT/'scripts/run_native_tool.py',
      ref,ROOT/'tests/c64_vice/reu_harness.cpp',ROOT/'tests/c64_vice/bus_reference.cpp',ROOT/'tests/c64_vice/bus_timing.sv',ROOT/'tests/c64_vice/bus_vic_peek.sv',ROOT/'scripts/port_c64_vic.py',ROOT/'hdl/core/c64/ports/video_vicII_656x.vhd',ROOT/'hdl/core/c64/ports/fpga64_sid_iec.vhd',ROOT/'hdl/core/c64/multitini_c64_reu.sv',ROOT/'hdl/core/c64/multitini_c64_cpu_scheduler.vhd']
    snapshot=lambda:{p.relative_to(ROOT).as_posix():digest(p) for p in inputs}
    before=snapshot();result={'status':'FAIL','reference_commit':COMMIT,'source_sha256':before,
      'scope':'Production REU and verbatim wrapper PHI arbitration with independently prescribed BA; unchanged VICE REU routines. Access and END cycles compared without per-case shifting.',
      'limits':['No full VIC pixel or physical DDR proof; production VIC BA and bounded DDR/cache path are separate regressions.']}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['PATH']=os.environ.get('PATH',os.environ.get('Path',''))
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--'];vb=Path('C:/Xilinx/2025.2/Vivado/bin')
    def run(name,cmd):
        p=subprocess.run(guard+list(map(str,cmd)),cwd=out,env=env,capture_output=True,text=True,errors='replace',timeout=240)
        output=p.stdout+p.stderr;(out/(name+'.log')).write_text(output)
        if p.returncode or re.search(r'\b(?:error|fatal):',output,re.I):raise RuntimeError(name+': '+output[-5000:])
    try:
        src=ref.read_text();names=['set_reu_size','reu_ba_register','reu_reset','nonsc_reu_clk_inc_pre','reu_clk_inc_post_write','reu_clk_inc_post_read','reu_read_without_sideeffects','reu_store_without_sideeffects','reu_io2_read','reu_io2_store','increment_reu_with_wrap_around','store_to_reu','read_from_reu','reu_dma_update_regs','reu_dma_host_to_reu','reu_dma_reu_to_host','reu_dma_swap','reu_dma_compare','reu_dma','reu_dma_start']
        (out/'vice_reu_declarations.inc').write_text(src[src.index('enum {'):src.index('static io_source_t reu_io2_device =')])
        (out/'vice_reu_methods.inc').write_text('\n\n'.join(extract(src,n)[0] for n in names))
        (out/'production_arbiter.vhd').write_text(production_slice())
        run('compile-reference',['C:/msys64/ucrt64/bin/g++.exe','-std=c++17','-O2','-I',out,ROOT/'tests/c64_vice/bus_reference.cpp','-o','reference.exe'])
        run('reference',[out/'reference.exe','vice.csv'])
        run('analyze',[vb/'xvhdl.bat','--nolog','--2008',ROOT/'hdl/core/c64/multitini_c64_cpu_scheduler.vhd','production_arbiter.vhd'])
        run('compile',[vb/'xvlog.bat','--nolog','--sv',ROOT/'hdl/core/c64/multitini_c64_reu.sv',ROOT/'tests/c64_vice/bus_timing.sv'])
        run('elaborate',[vb/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','--snapshot','bus','bus_timing'])
        run('simulate',[vb/'xsim.bat','--nolog','bus','--runall'])
        with (out/'vice.csv').open() as f:expected=list(csv.DictReader(f))
        with (out/'rtl.csv').open() as f:actual=list(csv.DictReader(f))
        differences=[{'row':i,'vice':a,'rtl':b} for i,(a,b) in enumerate(zip(expected,actual)) if a!=b]
        (out/'differences.json').write_text(json.dumps(differences,indent=2)+'\n')
        if len(expected)!=len(actual) or differences:raise RuntimeError(f'Bus trace differs: {len(expected)}/{len(actual)} events, {len(differences)} differences')
        (out/'rtl-qualified.csv').write_bytes((out/'rtl.csv').read_bytes())
        run('delayed-peek',[vb/'xsim.bat','--nolog','bus','--runall','--testplusarg','SLOW_TAIL'])
        if 'PASS: delayed verify tail no duplicate ACK or bus access' not in (out/'delayed-peek.log').read_text():raise RuntimeError('Missing delayed-peek transport control')
        (out/'delayed-peek.csv').write_bytes((out/'rtl.csv').read_bytes());(out/'rtl.csv').write_bytes((out/'rtl-qualified.csv').read_bytes())
        run('vic-analyze',[vb/'xvhdl.bat','--nolog','--2008',ROOT/'hdl/core/c64/ports/video_vicII_656x.vhd'])
        run('vic-compile',[vb/'xvlog.bat','--nolog','--sv',ROOT/'tests/c64_vice/bus_vic_peek.sv'])
        run('vic-elaborate',[vb/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','--snapshot','vic_peek','bus_vic_peek'])
        run('vic-simulate',[vb/'xsim.bat','--nolog','vic_peek','--runall'])
        if 'PASS: VIC early peek' not in (out/'vic-simulate.log').read_text():raise RuntimeError('Missing VIC read/clear completion')
        # Remove write BA grace in the actual extracted RTL, then require the
        # same VICE trace comparator to reject its transaction/END timing.
        control=production_slice().replace("elsif dma_write_delay = '0' then", "elsif false then")
        if control==production_slice():raise RuntimeError('Write-grace negative-control anchor changed')
        (out/'negative_arbiter.vhd').write_text(control)
        run('negative-analyze',[vb/'xvhdl.bat','--nolog','--2008','negative_arbiter.vhd'])
        run('negative-elaborate',[vb/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','--snapshot','bus_negative','bus_timing'])
        run('negative-simulate',[vb/'xsim.bat','--nolog','bus_negative','--runall'])
        with (out/'rtl.csv').open() as f:negative=list(csv.DictReader(f))
        (out/'negative.csv').write_bytes((out/'rtl.csv').read_bytes());(out/'rtl.csv').write_bytes((out/'rtl-qualified.csv').read_bytes())
        if negative==expected:raise RuntimeError('Missing write-grace RTL was not rejected')
        negative_differences=sum(a!=b for a,b in zip(negative,expected))
        if snapshot()!=before:raise RuntimeError('Source changed during validation')
        result.update(status='PASS',events=len(actual),cases=len({row['case'] for row in actual}),sources_unchanged=True,vic_peek='5 selected mapped reads plus unselected-strobe control; collision data3 then0; no pixel or data enable added',delayed_peek='40-clock logical response after terminal peek is outside native cadence; it remains held without duplicate ACK/access',negative_control={'removed_write_grace_rejected':True,'differing_events':negative_differences})
    except Exception as error:result['error']=str(error);raise
    finally:
        result['evidence_sha256']={p.name:digest(p) for p in out.iterdir() if p.is_file() and p.suffix in ('.log','.csv','.json','.inc','.vhd') and p.name!='validation.json'}
        (out/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS VICE bus timing:',result['cases'],'cases,',result['events'],'events')
if __name__=='__main__':main()
