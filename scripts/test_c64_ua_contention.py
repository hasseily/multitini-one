#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare real REU/cache/HP2 native traces with simultaneous UA/HP3 traffic."""
from __future__ import annotations
import argparse,csv,hashlib,json,os,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,default=ROOT/'build/c64-ua-contention')
    args=p.parse_args();build=args.build_dir.resolve()
    if not build.is_relative_to(ROOT/'build') or build==ROOT/'build':p.error('Use dedicated build child')
    build.mkdir(parents=True,exist_ok=True)
    original=ROOT/'hdl/sim/tb_c64_reu_native.sv';fragment=ROOT/'hdl/sim/c64_ua_reu_contention.inc'
    sources=[ROOT/'third_party/appletini/hdl/cdc_bit_sync.sv']+[
        ROOT/'hdl/core/c64'/f'multitini_c64_{n}.sv' for n in ('reu','reu_cache','ddr_path','ua','ua_voice','ua_fetch','ua_mix')]
    inputs=[Path(__file__),original,fragment,*sources]
    sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    hashes=lambda:{f.relative_to(ROOT).as_posix():sha(f) for f in inputs}
    before=hashes();result={'status':'FAIL','source_sha256':before,'checks':[],
        'limits':['Synthetic exclusive DDRC service with waiting HP2 priority and an already admitted HP3 read allowed to finish.',
                  'Both production DDR bridges and actual REU/cache/UA are present. No video or PS master, physical latency or board proof.',
                  'HP2 service delay reduced by ten clocks from the original isolated test; all contention wait still counts against the unchanged 64-clock request-to-last-response limit.',
                  'Native slot eligibility is prescribed by the original 57-case fixture; this does not replace VIC BA arbitration tests.']}
    text=original.read_text()
    def replace(a,b):
        nonlocal text
        if text.count(a)!=1:raise RuntimeError('Native test anchor changed: '+a)
        text=text.replace(a,b)
    replace('parameter integer LONG_LATENCY=0','parameter integer LONG_LATENCY=0, parameter integer UA_TRAFFIC=0')
    replace('.memory_waiting(),.*','.memory_waiting(reu_waiting),.*')
    replace('.invalidate(1\'b0),','.invalidate(1\'b0), .pending_addr(ua_pending_addr),.pending_valid(ua_pending_valid),\n        .inflight_write_addr(ua_write_addr),.inflight_write_valid(ua_write_valid),\n        .write_done(ua_write_done),.write_done_addr(ua_done_addr),.write_snoop_valid(ua_snoop_valid),.write_snoop_addr(ua_snoop_addr),')
    for channel in ('ar','aw','w'):
        replace(f'.{channel}ready({channel}ready)',f'.{channel}ready(hp2_{channel}ready)')
        replace(f'if({channel}valid&&{channel}ready)',f'if({channel}valid&&hp2_{channel}ready)')
    replace('(LONG_LATENCY ? 66 : 35)','(LONG_LATENCY ? 66 : 25)')
    replace('(LONG_LATENCY ? 78 : 47)','(LONG_LATENCY ? 78 : 37)')
    replace('if(max_latency<(LONG_LATENCY ? 80 : 60))', 'if(max_latency<(LONG_LATENCY ? 80 : 50))')
    declarations,body=fragment.read_text().split('    wire [23:0] ua_address',1)
    replace('    multitini_c64_reu #',declarations+'\n    multitini_c64_reu #')
    replace('    always @(posedge clk)begin : axi_model','    wire [23:0] ua_address'+body+'\n    always @(posedge clk)begin : axi_model')
    replace('        $fclose(trace);$fclose(ddr_trace);$finish;',
        '        if(UA_TRAFFIC && (ua_accepted_total<100 || ua_blocked_hp2<100 || ua_playing_cycles<1000))$fatal(1,"UA load or real contention missing");\n'
        '        $display("PASS: UA contention enabled=%0d requests=%0d replies=%0d hp2_wait=%0d playing=%0d",UA_TRAFFIC,ua_accepted_total,ua_replies_total,ua_blocked_hp2,ua_playing_cycles);\n'
        '        $fclose(trace);$fclose(ddr_trace);$finish;')
    generated=build/'tb_shared.sv';generated.write_text(text)
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['PATH']=os.environ.get('PATH',os.environ.get('Path',''))
    tools=Path('C:/Xilinx/2025.2/Vivado/bin')
    def run(directory,label,command,expected_failure=False):
        r=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*map(str,command)],cwd=directory,env=env,capture_output=True,text=True,timeout=300)
        output=r.stdout+r.stderr;(directory/(label+'.log')).write_text(output)
        failed=bool(r.returncode or re.search(r'FATAL|Fatal|ERROR:',output))
        if failed and not expected_failure:raise RuntimeError(output[-10000:])
        if expected_failure and not failed:raise RuntimeError('Excessive contention was accepted')
        return output
    def trace(path):
        rows=list(csv.DictReader(path.read_text().splitlines()));first={};normalized=[]
        for r in rows:
            base=first.setdefault(r['case'],int(r['clock']))
            normalized.append((r['case'],int(r['clock'])-base,r['index'],r['address'],r['write'],r['data'],r['ba']))
        return normalized
    try:
        for ntsc in (0,1):
            baseline=None
            for enabled in (0,1):
                directory=build/f'{ntsc}-{enabled}';directory.mkdir(exist_ok=True)
                wrapper=directory/'wrapper.sv'
                wrapper.write_text(f'module tb_shared; tb_c64_reu_native #(.NTSC({ntsc}),.UA_TRAFFIC({enabled})) test(); endmodule\n')
                run(directory,'compile',[tools/'xvlog.bat','--nolog','--sv',*sources,generated,wrapper])
                run(directory,'elaborate',[tools/'xelab.bat','--nolog','--timescale','1ns/1ps','--snapshot','shared_sim','tb_shared'])
                output=run(directory,'simulate',[tools/'xsim.bat','--nolog','shared_sim','--runall'])
                lines=[line for line in output.splitlines() if line.startswith('PASS:')]
                if len(lines)!=2 or len(re.findall('^CASE:',output,re.M))!=57:raise RuntimeError('Missing shared test coverage')
                normalized=trace(directory/'native.csv')
                if baseline is None:baseline=normalized
                elif baseline!=normalized:raise RuntimeError('Native REU cycle/payload trace differs with UA on')
                result['checks'].append({'ntsc':ntsc,'ua':enabled,'summary':lines,'acks':len(normalized),'native_sha256':sha(directory/'native.csv')})
                print('\n'.join(lines),flush=True)
        directory=build/'negative-long-hp3';directory.mkdir(exist_ok=True)
        mutant=directory/'tb_long.sv';mutant.write_text(text.replace('ua_delay<=6;','ua_delay<=40;'))
        wrapper=directory/'wrapper.sv';wrapper.write_text('module tb_shared; tb_c64_reu_native #(.UA_TRAFFIC(1)) test(); endmodule\n')
        run(directory,'compile',[tools/'xvlog.bat','--nolog','--sv',*sources,mutant,wrapper])
        run(directory,'elaborate',[tools/'xelab.bat','--nolog','--timescale','1ns/1ps','--snapshot','shared_sim','tb_shared'])
        output=run(directory,'simulate',[tools/'xsim.bat','--nolog','shared_sim','--runall'],expected_failure=True)
        if 'DDR latency budget exceeded' not in output and 'native slot missing' not in output:raise RuntimeError('Wrong excessive-contention failure')
        result['checks'].append({'negative_control':'Rejected HP3 service exceeding the shared 64-clock REU envelope'})
        print('PASS: rejected excessive HP3 contention',flush=True)
        if hashes()!=before:raise RuntimeError('Inputs changed during run')
        result.update(status='PASS',sources_unchanged=True,normalized_native_traces_equal=True)
    except Exception as e:result['error']=str(e);raise
    finally:(build/'validation.json').write_text(json.dumps(result,indent=2)+'\n')

if __name__=='__main__':main()
