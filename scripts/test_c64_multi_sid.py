#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Real eight-SID engines, portable resolver, atomic map, mixer and detection.

Synthetic stimuli only. MAME/Ultimate waveform equality and board audio are
not asserted. Uses a fresh ignored build folder and records every source hash.
"""
from __future__ import annotations
import argparse,hashlib,json,os,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
LEAVES=['third_party/c64/upstream/rtl/sid/sid_envelope.sv','hdl/core/c64/ports/sid_voice.sv',
        'hdl/core/c64/ports/sid_dac.sv','hdl/core/c64/ports/sid_filter.sv','hdl/core/c64/ports/sid_tables.sv','hdl/core/c64/ports/sid_top.sv',
        'hdl/core/c64/multitini_c64_sid_array.sv']
SOURCES=LEAVES+['hdl/core/c64/multitini_c64_sid_map.sv','hdl/core/c64/multitini_c64_sid_mix.sv','hdl/core/c64/multitini_c64_turbo_regs.sv',
 'hdl/core/c64/multitini_c64_sid_transport.sv','hdl/common/multitini_async_mailbox.sv','hdl/common/multitini_word_mailbox.sv','third_party/appletini/hdl/cdc_bit_sync.sv','hdl/sim/tb_c64_sid_transport.sv',
 'hdl/sim/tb_c64_sid_array.sv','hdl/sim/tb_c64_sid_map_mix.sv','hdl/sim/tb_c64_island_regs.sv',
 'firmware/common/c64_sid.c','firmware/include/multitini_c64_sid.h','tests/host/test_c64_sid.c',
 'tests/host/c64_sid_ultimate_oracle.cpp','third_party/ultimate_firmware_reference/software/u64/u64_config.cc',
 'scripts/test_c64_multi_sid.py','scripts/run_native_tool.py','scripts/port_c64_sid.py',
 'third_party/c64/upstream/rtl/sid/sid_voice.sv','third_party/c64/patches/sid_voice.sv.patch']
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build-dir',type=Path,required=True);a=ap.parse_args()
    out=a.build_dir.resolve()
    if not out.is_relative_to(ROOT/'build') or out==ROOT/'build' or out.exists():ap.error('Choose a fresh dedicated build/ child')
    out.mkdir(parents=True);before={n:digest(ROOT/n) for n in SOURCES}
    receipt={'status':'FAIL','source_sha256':before,'commands':[],'scope':['Real eight independent SID engine instances; synthetic register stimuli.','Firmware addressing/overlay/codec; atomic tables; signed mixing/DC and average; detection reads.','No analog waveform equivalence, routed timing, external IO priority, or board acceptance claim.']}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['PATH']='C:/msys64/ucrt64/bin;'+os.environ.get('PATH',os.environ.get('Path',''))
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--'];vendor=Path('C:/Xilinx/2025.2/Vivado/bin');native=Path('C:/msys64/ucrt64/bin')
    def run(name,args,marker=None,expected_failure=None):
        command=guard+list(map(str,args));p=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,errors='replace',timeout=600)
        log=out/(name+'.log');log.write_text(p.stdout+p.stderr)
        receipt['commands'].append({'argv':command,'returncode':p.returncode,'log':log.relative_to(ROOT).as_posix(),'sha256':digest(log)})
        if expected_failure:
            if expected_failure not in p.stdout+p.stderr or 'PASS SID ARRAY' in p.stdout:
                raise RuntimeError('Negative control did not fail at its intended assertion: '+name)
            return p.stdout
        if p.returncode or re.search(r'\b(?:ERROR|FATAL)\s*:|\bFatal:',p.stdout+p.stderr,re.I):raise RuntimeError(name+': '+(p.stdout+p.stderr)[-5000:])
        if marker and len(re.findall(marker,p.stdout))!=1:raise RuntimeError('Missing unique pass marker '+name)
        return p.stdout
    try:
        reference=ROOT/'third_party/ultimate_firmware_reference/software/u64/u64_config.cc'
        if digest(reference)!='07fb46dec3a58d81a511a7ca55b31cb2a8dc493e1725f109116c513732a87313':raise ValueError('Pinned Ultimate SID decoder changed')
        source=reference.read_text();bodies=[]
        for method in ('auto_mirror','fix_splits'):
            start=source.index('void U64Config :: '+method+'(');opening=source.index('{',start);end=opening+1;depth=1
            while depth:
                if source[end]=='{':depth+=1
                elif source[end]=='}':depth-=1
                end+=1
            bodies.append(source[start:end])
        (out/'ultimate_sid.inc').write_text('// Verbatim GPL Ultimate firmware d43546fe decoder bodies.\n'+'\n'.join(bodies)+'\n')
        receipt['reference_commit']='d43546fe25b26cb332f43f00219d33153dfe44f8';receipt['reference_generated_sha256']=digest(out/'ultimate_sid.inc')
        run('resolver-object',[native/'gcc.exe','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'firmware/include'),'-c',ROOT/'firmware/common/c64_sid.c','-o','sid.o'])
        run('oracle-build',[native/'g++.exe','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'firmware/include'),'-I'+str(out),ROOT/'tests/host/c64_sid_ultimate_oracle.cpp','sid.o','-o','oracle.exe'])
        text=run('oracle',[out/'oracle.exe'],r'PASS U64 SID decode checks=\d+')
        receipt['ultimate_decode_checks']=int(re.search(r'checks=(\d+)',text)[1])
        run('host-build',[native/'gcc.exe','-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'firmware/include'),ROOT/'firmware/common/c64_sid.c',ROOT/'tests/host/test_c64_sid.c','-o','host.exe'])
        text=run('host',[out/'host.exe',out/'table.hex'],r'PASS SID firmware checks=\d+')
        receipt['host_checks']=int(re.search(r'checks=(\d+)',text)[1])
        for top,bench,leaves in [('tb_c64_sid_map_mix','tb_c64_sid_map_mix.sv',['multitini_c64_sid_map.sv','multitini_c64_sid_mix.sv']),('tb_island_regs','tb_c64_island_regs.sv',['multitini_c64_turbo_regs.sv'])]:
            run(top+'-compile',[native/'iverilog.exe','-g2012','-s',top,'-o',top+'.vvp',*[ROOT/'hdl/core/c64'/n for n in leaves],ROOT/'hdl/sim'/bench])
            run(top,[native/'vvp.exe',top+'.vvp'],r'(?:PASS SID MAP MIX|REGS PASS) checks=\d+')
        run('xvlog',[vendor/'xvlog.bat','--nolog','--sv','--relax',*[ROOT/n for n in LEAVES],ROOT/'hdl/sim/tb_c64_sid_array.sv'])
        run('xelab',[vendor/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','--snapshot','sid_array','tb_c64_sid_array'])
        run('array',[vendor/'xsim.bat','--nolog','sid_array','--runall'],r'PASS SID ARRAY checks=\d+')
        xpm=vendor.parent/'data/ip/xpm/xpm_cdc/hdl/xpm_cdc.sv'
        receipt['vendor_xpm_sha256']=digest(xpm)
        run('transport-compile',[vendor/'xvlog.bat','--nolog','--sv',xpm,vendor.parent/'data/verilog/src/glbl.v',ROOT/'third_party/appletini/hdl/cdc_bit_sync.sv',ROOT/'hdl/common/multitini_async_mailbox.sv',ROOT/'hdl/common/multitini_word_mailbox.sv',ROOT/'hdl/core/c64/multitini_c64_sid_map.sv',ROOT/'hdl/core/c64/multitini_c64_sid_transport.sv',ROOT/'hdl/sim/tb_c64_sid_transport.sv'])
        run('transport-elaborate',[vendor/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','--snapshot','sid_transport','tb_c64_sid_transport','glbl'])
        run('transport',[vendor/'xsim.bat','--nolog','sid_transport','--runall'],r'PASS SID TRANSPORT checks=\d+')
        local_wrapper=out/'sid_transport_local_top.sv'
        local_wrapper.write_text('module sid_transport_local_top; tb_c64_sid_transport #(.SHARED_CORE_RUNNING(0)) dut(); endmodule\n')
        receipt['local_transport_wrapper_sha256']=digest(local_wrapper)
        run('transport-local-compile',[vendor/'xvlog.bat','--nolog','--sv',local_wrapper])
        run('transport-local-elaborate',[vendor/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps',
            '--snapshot','sid_transport_local','sid_transport_local_top','glbl'])
        run('transport-local',[vendor/'xsim.bat','--nolog','sid_transport_local','--runall'],r'PASS SID TRANSPORT checks=\d+')
        # Corrupt only the production fast owner selection. The unchanged
        # actual-engine bench must reject the wrong chip's value.
        original=(ROOT/'hdl/core/c64/multitini_c64_sid_array.sv').read_text()
        anchor='wire [2:0] fast_owner=owners[fast_rd_addr[9:5]*3+:3];'
        if original.count(anchor)!=1:raise RuntimeError('Negative-control anchor changed')
        mutant=out/'sid_array_wrong_owner.sv'
        mutant.write_text(original.replace(anchor,"wire [2:0] fast_owner=3'd0;"))
        receipt['negative_control_source_sha256']=digest(mutant)
        run('negative-compile',[vendor/'xvlog.bat','--nolog','--sv',mutant])
        run('negative-elaborate',[vendor/'xelab.bat','--nolog','--relax','--timescale','1ns/1ps','--snapshot','sid_wrong_owner','tb_c64_sid_array'])
        run('negative-control',[vendor/'xsim.bat','--nolog','sid_wrong_owner','--runall'],expected_failure='SID ARRAY initial map write before first engine CE')
        receipt['negative_control']='PASS: wrong fast-read owner rejected by unchanged real-engine bench'
        receipt['sources_unchanged']=before=={n:digest(ROOT/n) for n in SOURCES}
        if not receipt['sources_unchanged']:raise RuntimeError('Source changed during qualification')
        receipt['status']='PASS'
    except Exception as e:receipt['error']=str(e)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n');print(receipt['status'],receipt.get('error',''))
    return receipt['status']!='PASS'
if __name__=='__main__':raise SystemExit(main())
