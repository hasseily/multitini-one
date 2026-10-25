#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual adapter CRT/UCI byte routing and mapped/unmapped SID IO regression."""
from pathlib import Path
import argparse,hashlib,json,re,shutil,subprocess,sys
from test_c64_uci_machine import kernal
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build-dir',type=Path,required=True);a=ap.parse_args()
    out=a.build_dir.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'build') or out==ROOT/'build':ap.error('fresh dedicated build/ child required')
    out.mkdir(parents=True)
    sources=[ROOT/'hdl/generated/multitini_regs_pkg.sv',ROOT/'third_party/appletini/hdl/cdc_bit_sync.sv',ROOT/'hdl/common/multitini_async_mailbox.sv'];section=''
    for raw in (ROOT/'hdl/hdl_sources.txt').read_text().splitlines():
        line=raw.strip()
        if line.startswith('['):section=line
        elif section=='[c64]' and line and not line.startswith('#'):
            p=(ROOT/'hdl'/line.split('|')[0].strip()).resolve()
            if p.suffix in ('.sv','.v','.vhd'):sources.append(p)
    sources.append(ROOT/'hdl/sim/tb_c64_adapter_load_sid.sv')
    evidence=sources+[Path(__file__),ROOT/'scripts/run_native_tool.py',ROOT/'hdl/hdl_sources.txt',ROOT/'scripts/test_c64_uci_machine.py']
    receipt={'status':'FAIL','source_sha256':{p.relative_to(ROOT).as_posix():sha(p) for p in evidence},'cases':[],'negative_controls':[]}
    for p in sources:
        target=out/'sources'/p.relative_to(ROOT);target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
    snap=lambda p:out/'sources'/p.relative_to(ROOT)
    v=Path('C:/Xilinx/2025.2/Vivado/bin')
    def run(folder,name,argv,failure=None):
        p=subprocess.run([sys.executable,ROOT/'scripts/run_native_tool.py','--cwd',folder,'--',*argv],capture_output=True,text=True,errors='replace',timeout=600)
        text=p.stdout+p.stderr;(folder/(name+'.log')).write_text(text)
        bad=p.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:',text,re.I)
        if failure:
            if not bad or failure not in text:raise RuntimeError(name+' negative control not rejected: '+text[-2000:])
        elif bad:raise RuntimeError(name+': '+text[-5000:])
        return text
    try:
        for ntsc in (0,1):
            folder=out/str(ntsc);folder.mkdir();wrapper=folder/'profile.sv'
            (folder/'kernal.hex').write_text(''.join(f'{b:02x}\n' for b in kernal(False)))
            wrapper.write_text(f'module profile; tb_c64_adapter_load_sid #(.NTSC({ntsc})) test(); endmodule\n')
            # The local SID port must compile strictly; upstream compatibility
            # exceptions remain restricted to unchanged imported sources.
            sv=[p for p in sources if p.suffix!='.vhd']
            run(folder,'imported',[v/'xvlog.bat','--nolog','--sv','--relax',*[snap(p) for p in sv if 'third_party' in p.parts]])
            run(folder,'local',[v/'xvlog.bat','--nolog','--sv',*[snap(p) for p in sv if 'third_party' not in p.parts],wrapper])
            run(folder,'vhdl',[v/'xvhdl.bat','--nolog','--2008',*[snap(p) for p in sources if p.suffix=='.vhd']])
            run(folder,'elaborate',[v/'xelab.bat','--nolog','--relax','--mt','off','--timescale','1ns/1ps','-s','adapter_sim','profile'])
            text=run(folder,'positive',[v/'xsim.bat','--nolog','adapter_sim','--runall'])
            match=re.findall(r'PASS ADAPTER LOAD SID checks=(\d+) CRTbytes=8192 UCIbytes=4 slots=16 NTSC='+str(ntsc),text)
            if len(match)!=1:raise RuntimeError('Missing exact positive marker')
            receipt['cases'].append({'ntsc':ntsc,'checks':int(match[0]),'crt_bytes':8192,'uci_bytes':4,'io_slots':16,'sid_count':8})
        folder=out/'0';adapter=snap(ROOT/'hdl/core/c64/multitini_c64_adapter.sv');original=adapter.read_text()
        for name,old,new,failure in (
            ('wrong_kind','multitini_regs_pkg::MT_C64_UCI_LOAD_KIND[7:0]',"8'd17",'ADAPTER LOAD SID loader handshake bounded'),
            ('empty_slot',' && (|sid_io_masks[cpu_bus_addr[8:5]*8 +: 8])','', 'ADAPTER LOAD SID unmapped SID slot does not own IO')):
            if old not in original:raise RuntimeError('Mutation anchor missing')
            mutant=folder/(name+'.sv');mutant.write_text(original.replace(old,new))
            run(folder,name+'-compile',[v/'xvlog.bat','--nolog','--sv',mutant])
            run(folder,name+'-elaborate',[v/'xelab.bat','--nolog','--relax','--mt','off','--timescale','1ns/1ps','-s',name,'profile'])
            run(folder,name+'-negative',[v/'xsim.bat','--nolog',name,'--runall'],failure)
            receipt['negative_controls'].append({'name':name,'sha256':sha(mutant),'rejected':True})
        receipt['sources_unchanged']=all(sha(ROOT/k)==h for k,h in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:raise RuntimeError('Sources changed')
        receipt['limits']=['Actual eight-SID adapter and PLA with synthetic ROM and native-pin bus injection; no loaded CRT execution claim.', 'CRT memory is a delayed abstract backend; no PSRAM/DDR electrical or board claim.']
        receipt['status']='PASS'
    except Exception as e:receipt['error']=str(e)
    receipt['artifact_sha256']={p.relative_to(out).as_posix():sha(p) for p in out.rglob('*') if p.is_file() and p.suffix in ('.log','.sv')}
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n');print(receipt['status'],receipt.get('error',''));return receipt['status']!='PASS'
if __name__=='__main__':raise SystemExit(main())
