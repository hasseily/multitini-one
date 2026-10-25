#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""B2/B3/B5/B6: real multi-SID decode, legacy equality, isolation and edges."""
import argparse,hashlib,json,re,subprocess,sys
from pathlib import Path
from test_c64_multi_sid import LEAVES
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build-dir',type=Path,required=True);a=ap.parse_args();out=a.build_dir.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'build') or out==ROOT/'build':ap.error('fresh dedicated build/ child required')
    out.mkdir(parents=True)
    benches={'decode':'PASS SID DECODE','array_legacy':'PASS SID LEGACY','osc3_8':'PASS SID OSC8','fanout':'PASS SID FANOUT'}
    names=LEAVES+['hdl/core/c64/multitini_c64_sid_mix.sv','hdl/core/c64/ports/fpga64_sid_iec.vhd','scripts/test_c64_multi_sid.py','scripts/test_c64_sid_contracts.py','scripts/run_native_tool.py','firmware/common/c64_sid.c','firmware/include/multitini_c64_sid.h','tests/host/c64_sid_presets.c']+['hdl/sim/tb_c64_sid_'+b+'.sv' for b in benches]
    receipt={'status':'FAIL','source_sha256':{n:sha(ROOT/n) for n in names},'cases':{},'negative_controls':[]}
    v=Path('C:/Xilinx/2025.2/Vivado/bin')
    def run(name,args,marker=None,failure=None):
        p=subprocess.run([sys.executable,ROOT/'scripts/run_native_tool.py','--cwd',out,'--',*args],capture_output=True,text=True,errors='replace',timeout=1200)
        text=p.stdout+p.stderr;(out/(name+'.log')).write_text(text)
        bad=p.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:',text,re.I)
        if failure:
            if not bad or failure not in text:raise RuntimeError(name+' negative control not rejected: '+text[-2000:])
        elif bad:raise RuntimeError(name+': '+text[-5000:])
        if marker and text.count(marker)!=1:raise RuntimeError(name+' missing unique marker')
        return text
    try:
        run('presets-build',[Path('C:/msys64/ucrt64/bin/gcc.exe'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-I'+str(ROOT/'firmware/include'),ROOT/'firmware/common/c64_sid.c',ROOT/'tests/host/c64_sid_presets.c','-o',out/'presets.exe'])
        run('presets',[out/'presets.exe',out/'presets.hex'],'PASS SID presets tables=6')
        source=(ROOT/'hdl/core/c64/ports/fpga64_sid_iec.vhd').read_text()
        start=source.index('function fast_sid_select(');function=source[start:source.index('end function;',start)+len('end function;')]
        native='\n'.join(line for line in source.splitlines() if line.startswith(('sid_sel_l <=','sid_sel_r <=')))
        fast='\n'.join(line for line in source.splitlines() if line.startswith(('fastSidSelect <=','fastSidReadSelect <=','fastSidLatchSource <=')))
        if len(native.splitlines())!=2 or len(fast.splitlines())!=3:raise RuntimeError('Production SID decode extraction changed')
        wrapper='''library ieee;use ieee.std_logic_1164.all;use ieee.numeric_std.all;
entity c64_sid_decode_observed is port(sid_mode:in unsigned(2 downto 0);cpuAddr:in unsigned(15 downto 0);cs_sid,ioe_i,iof_i:in std_logic;
fastSidA,fio_rd_addr:in unsigned(9 downto 0);fastSidWr,fastSidLatch,fio_rd_sid:in std_logic;
sid_sel_l,sid_sel_r:out std_logic;fastSidSelect,fastSidReadSelect,fastSidLatchSource:out std_logic_vector(1 downto 0));end;
architecture observed of c64_sid_decode_observed is
'''+function+'\nbegin\n'+native+'\n'+fast+'\nend;\n'
        generated=out/'decode_observed.vhd';generated.write_text(wrapper);receipt['generated_decode_sha256']=sha(generated)
        run('imported',[v/'xvlog.bat','--nolog','--sv','--relax',*[ROOT/n for n in LEAVES if n.startswith('third_party/')]])
        run('local',[v/'xvlog.bat','--nolog','--sv',*[ROOT/n for n in LEAVES if not n.startswith('third_party/')],ROOT/'hdl/core/c64/multitini_c64_sid_mix.sv',*[ROOT/'hdl/sim'/('tb_c64_sid_'+b+'.sv') for b in benches]])
        run('decode-vhdl',[v/'xvhdl.bat','--nolog','--2008',generated])
        for bench,marker in benches.items():
            run(bench+'-elaborate',[v/'xelab.bat','--nolog','--relax','--mt','off','--timescale','1ns/1ps','-s',bench,'tb_c64_sid_'+bench])
            text=run(bench,[v/'xsim.bat','--nolog',bench,'--runall'],marker)
            receipt['cases'][bench]=next(line for line in text.splitlines() if marker in line)
            print(receipt['cases'][bench],flush=True)
        top=ROOT/'hdl/core/c64/ports/sid_top.sv';old_top=top.read_text()
        if old_top.count('.reset(reset)')!=3:raise RuntimeError('Voice reset mutation anchor changed')
        mutant=out/'lost_first_gate.sv';mutant.write_text(old_top.replace('.reset(reset)','.reset(time_reset)'))
        run('lost-gate-compile',[v/'xvlog.bat','--nolog','--sv',mutant])
        run('lost-gate-elaborate',[v/'xelab.bat','--nolog','--relax','--mt','off','--timescale','1ns/1ps','-s','lost_gate','tb_c64_sid_osc3_8'])
        run('lost-gate-negative',[v/'xsim.bat','--nolog','lost_gate','--runall'],failure='SID OSC8 only target envelope advances')
        receipt['negative_controls'].append({'name':'lost_first_gate','sha256':sha(mutant),'rejected':True})
        run('restore-top',[v/'xvlog.bat','--nolog','--sv',top])
        original=(ROOT/'hdl/core/c64/multitini_c64_sid_array.sv').read_text()
        for name,anchor,replacement,bench,failure in [
            ('owner','wire [2:0] fast_owner=owners[fast_rd_addr[9:5]*3+:3];',"wire [2:0] fast_owner=3'd7;",'decode','SID DECODE lowest selected chip read mux'),
            ('latch_owner','wire [2:0] latch_owner=owners[fio_addr[9:5]*3+:3];',"wire [2:0] latch_owner=3'd7;",'fanout','SID FANOUT read latch owner/fanout isolation')]:
            if original.count(anchor)!=1:raise RuntimeError('mutation anchor changed')
            mutant=out/(name+'.sv');mutant.write_text(original.replace(anchor,replacement))
            run(name+'-compile',[v/'xvlog.bat','--nolog','--sv',mutant])
            run(name+'-elaborate',[v/'xelab.bat','--nolog','--relax','--mt','off','--timescale','1ns/1ps','-s','bad_'+name,'tb_c64_sid_'+bench])
            run(name+'-negative',[v/'xsim.bat','--nolog','bad_'+name,'--runall'],failure=failure)
            receipt['negative_controls'].append({'name':name,'sha256':sha(mutant),'rejected':True})
        receipt['sources_unchanged']=all(sha(ROOT/n)==h for n,h in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:raise RuntimeError('Sources changed during qualification')
        receipt['limits']=['Synthetic real-engine simulation; no analog waveform or board proof.','Native/DMA write qualification shares the same selected native port; real machine DMA is covered by separate stock golden and REU machine suites.']
        receipt['status']='PASS'
    except Exception as e:receipt['error']=str(e)
    receipt['artifact_sha256']={p.name:sha(p) for p in out.iterdir() if p.suffix in ('.log','.hex','.sv','.vhd')}
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n');print(receipt['status'],receipt.get('error',''));return receipt['status']!='PASS'
if __name__=='__main__':raise SystemExit(main())
