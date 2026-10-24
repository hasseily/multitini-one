#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pinned VICE pin schedules through production island, proxy and synchronous memory."""
from __future__ import annotations
import argparse, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path
from test_c64_vice_cpu import compare, read_reference
from c64_vice_oracle import COMMIT, verify_reference
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,required=True)
    p.add_argument('--reference-dir',type=Path,help='Existing reference-only generation with matching source hashes')
    a=p.parse_args();out=a.build_dir.resolve()
    if out==ROOT/'build' or not out.is_relative_to(ROOT/'build') or out.exists():p.error('fresh build child required')
    out.mkdir(parents=True)
    receipt={'schema':1,'status':'FAIL','reference_commit':COMMIT,'source_sha256':{},'sources_unchanged':False,'cases':[]}
    vhdl=[ROOT/'third_party/c64/upstream/rtl/t65'/f for f in ('T65_Pack.vhd','T65_ALU.vhd','T65_MCode.vhd')]
    vhdl += [ROOT/f for f in ('hdl/core/c64/ports/T65.vhd','hdl/core/c64/multitini_c64_6510_port.vhd','hdl/core/c64/ports/cpu_6510.vhd')]
    sv=[ROOT/'hdl/core/c64'/f'multitini_c64_{n}.sv' for n in ('cpu_island','cpu_proxy','island_governor','island_map','island_memory','nmi_delivery')]
    sv += [ROOT/'hdl/sim/tb_c64_island_vice.sv']
    sources=[Path(__file__),ROOT/'scripts/test_c64_vice_cpu.py',ROOT/'scripts/run_native_tool.py',*vhdl,*sv]
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--cwd',str(out),'--']
    def run(name,cmd,timeout=600):
        r=subprocess.run([str(x) for x in cmd],cwd=out,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=timeout)
        (out/f'{name}.log').write_text(r.stdout,encoding='utf-8')
        if r.returncode or re.search(r'\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b',r.stdout,re.I):raise RuntimeError(f'{name} failed ({r.returncode})')
        return r.stdout
    try:
        verify_reference()
        for src in sources:
            key=src.relative_to(ROOT).as_posix();receipt['source_sha256'][key]=digest(src)
            dst=out/'sources'/key;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
        reference=a.reference_dir.resolve() if a.reference_dir else out/'reference'
        if not a.reference_dir:
            run('reference-generation',[sys.executable,ROOT/'scripts/test_c64_vice_cpu.py','--build-dir',reference,'--reference-only'])
        gold=json.loads((reference/'validation.json').read_text())
        if gold['status']!='PASS' or gold['reference_commit']!=COMMIT or gold['case_count']!=344:raise RuntimeError('invalid VICE reference receipt')
        if not all(digest(ROOT/k)==v for k,v in gold['source_sha256'].items()):raise RuntimeError('reference source changed')
        receipt['source_sha256'].update(gold['source_sha256'])
        receipt['reference_receipt_sha256']=digest(reference/'validation.json')
        receipt['reference_receipt']=reference.relative_to(ROOT).as_posix()+'/validation.json'
        mem=(reference/'memory.bin').read_bytes()
        if hashlib.sha256(mem).hexdigest()!=gold['memory_sha256']:raise RuntimeError('reference memory changed')
        for name,data in {'low':mem[:32768],'high':mem[32768:],'rom':mem[0xa000:0xc000]+mem[0xe000:],'char':mem[0xd000:0xe000]}.items():
            (out/f'{name}.hex').write_text(''.join(f'{b:02x}\n' for b in data))
        cases=gold['cases']
        if [c['id'] for c in cases] != list(range(344)):
            raise RuntimeError('reference case identities are not unique and contiguous')
        expected_case_file=str(len(cases))+'\n'+''.join(
            f"{c['id']} {c['cadence']} memory.hex {c['stem']}.hex\n" for c in cases)
        if (reference/'cases.txt').read_text()!=expected_case_file:
            raise RuntimeError('reference case list does not match its receipt')
        for case in cases:
            for ext,h in case['artifact_sha256'].items():
                src=reference/(case['stem']+ext)
                if digest(src)!=h:raise RuntimeError('reference artifact changed')
                shutil.copyfile(src,out/src.name)
        shutil.copyfile(reference/'cases.txt',out/'cases.txt')
        shutil.copyfile(reference/'case_definitions.json',out/'case_definitions.json')
        wrapper_source=vhdl[-1].read_text();declaration='\t\tdoIO    : out unsigned(7 downto 0)'
        ports=';\n\t\ttest_data : out unsigned(7 downto 0);\n\t\ttest_sync : out std_logic'
        connection='\t\tSync => test_sync,\n';assignment='test_data <= unsigned(localDi);\n'
        assert wrapper_source.count(declaration)==1 and wrapper_source.count('\t\tR_W_n   => localWe,\n')==1
        observed=wrapper_source.replace(declaration,declaration+ports).replace('\t\tR_W_n   => localWe,\n','\t\tR_W_n   => localWe,\n'+connection).replace('end architecture;',assignment+'end architecture;')
        assert observed.replace(ports,'').replace(connection,'').replace(assignment,'')==wrapper_source
        wrapper=out/'observed_cpu_6510.vhd';wrapper.write_text(observed)
        island_source=sv[0].read_text();ports='output wire [7:0] test_data, output wire test_sync,\n    ';connection='.test_data(test_data),.test_sync(test_sync),'
        assert island_source.count('input wire clk_cpu,')==1 and island_source.count('.di(cpu_di),')==1
        observed=island_source.replace('input wire clk_cpu,',ports+'input wire clk_cpu,').replace('.di(cpu_di),',connection+'.di(cpu_di),')
        assert observed.replace(ports,'').replace(connection,'')==island_source
        island=out/'observed_island.sv';island.write_text(observed)
        receipt['observation_sha256']={q.name:digest(q) for q in (wrapper,island)}
        vivado=Path(os.environ.get('XILINX_VIVADO','C:/Xilinx/2025.2/Vivado'))/'bin';suffix='.bat' if os.name=='nt' else ''
        def native(name,args):return run(name,[*guard,vivado/(args[0]+suffix),*args[1:]])
        snap=lambda s:out/'sources'/s.relative_to(ROOT)
        native('xvlog',['xvlog','--nolog','--sv',*[island if s==sv[0] else snap(s) for s in sv]])
        native('xvhdl',['xvhdl','--nolog','--2008',*[wrapper if s==vhdl[-1] else snap(s) for s in vhdl]])
        native('xelab',['xelab','--nolog','--timescale','1ns/1ps','-s','island_vice','tb_c64_island_vice'])
        failures=[];total_accesses=0;total_holds=0
        for video in range(2):
            (out/'video.txt').write_text(str(video)+'\n')
            log=native(f'simulate-{video}',['xsim','island_vice','--runall'])
            if f'ISLAND_VICE PASS video={video} cases=344' not in log:raise RuntimeError('matrix incomplete')
            traces={c['id']:[] for c in cases}
            for m in re.finditer(r'^BOUNDARY_BUS,(\d+),(\d+),(\d+),([0-9a-f]+),([RW]),([0-9a-f]+),([01])$',log,re.M|re.I):
                cid,sequence,tick,address,rw,data,sync=m.groups();rows=traces[int(cid)]
                if int(sequence)!=len(rows):raise RuntimeError('non-contiguous trace')
                rows.append({'tick':int(tick),'address':address.lower(),'rw':rw,'data':data.lower(),'sync':int(sync)})
            summaries={int(m[0]):list(map(int,m[1:])) for m in re.findall(r'^BOUNDARY_SUMMARY,(\d+),(\d+),(\d+),(\d+),(\d+),(\d+)$',log,re.M)}
            for c in cases:
                expected=read_reference(out/(c['stem']+'.csv'));actual=traces[c['id']];result=compare(actual,expected);summary=summaries.get(c['id'],[])
                ok=result['equal'] and summary and summary[0]==1
                path=out/f"video-{video}-{c['stem']}-rtl.json";path.write_text(json.dumps(actual,indent=2)+'\n')
                item={'video':'NTSC' if video else 'PAL','name':c['name'],'id':c['id'],'status':'PASS' if ok else 'FAIL',**result,'rtl_summary':summary,'trace':path.name,'trace_sha256':digest(path)}
                receipt['cases'].append(item);total_accesses+=len(actual);total_holds+=summary[3] if summary else 0
                if not ok:failures.append(item)
            if video==0:
                baseline=traces[0];expected=read_reference(out/(cases[0]['stem']+'.csv'))
                if not compare(baseline,expected)['equal']:raise RuntimeError('baseline unequal before corruption test')
                corrupted=[dict(row) for row in baseline];i=next(i for i,r in enumerate(corrupted) if r['address']=='fffe');corrupted[i]['address']='fffa'
                negative=compare(corrupted,expected)
                if negative['equal']:raise RuntimeError('wrong-vector negative control passed')
                receipt['negative_control']={'status':'PASS','corruption':'FFFE changed to FFFA in an otherwise equal trace','comparison':negative}
        receipt.update(case_count=len(receipt['cases']),unique_pin_schedules=172,spacing_variants=2,video_standards=2,total_accesses=total_accesses,total_held_attempts=total_holds,failed=[{'video':c['video'],'name':c['name']} for c in failures])
        receipt['fixture_sha256']={q.name:digest(q) for q in out.iterdir()
            if q.suffix in ('.hex','.csv','.events') or q.name in ('cases.txt','case_definitions.json')}
        receipt['sources_unchanged']=all(digest(ROOT/k)==v for k,v in receipt['source_sha256'].items())
        receipt['limits']=['Exact-mode production island/proxy/BRAM with flat native RAM/ROM model; not a whole-VIC/CIA test.','Original VICE tick/address/data/RW/SYNC expectations remain unchanged; RDY-held native attempts included.','Alternate native frame waits vary wall-clock spacing without inserting CPU attempts.','Accelerated BA halt and NMI delivery are separate tests; this receipt does not equate halted turbo with native RDY attempts.','No physical board claim.']
        if failures:raise RuntimeError(f'{len(failures)} exact VICE comparisons failed')
        if not receipt['sources_unchanged']:raise RuntimeError('source changed during matrix')
        receipt['status']='PASS'
    except Exception as e:receipt['error']=str(e)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps({k:v for k,v in receipt.items() if k not in ('cases','source_sha256')},indent=2))
    return 0 if receipt['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
