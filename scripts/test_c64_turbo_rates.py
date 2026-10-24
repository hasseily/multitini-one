#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure production whole-machine CPU/FIO/forward rates at all 16 indices."""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,shutil,subprocess,sys
import test_c64_machine_schedule as base
import test_c64_turbo_ordering as ordering
ROOT=Path(__file__).resolve().parents[1]
RATES=[1,2,3,4,6,8,10,12,14,16,20,24,32,40,48,64]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()

def forward_cadence_ok(row):
    interval=(4 if row['index']==0 else 2 if row['index']<=3 else 1)*64
    return row['de_reads']==32 and row['de_min']==interval and row['de_max']==interval and row['de_span']==31*interval
def fixture():
    mem=bytearray([0xea]*65536)
    boot=bytes.fromhex('78 D8 A2 FF 9A A9 2F 85 00 A9 37 85 01 A9 00 8D 11 D0 8D 15 D0 8D 1A D0 AD 1E D0 AD 1F D0 A0 00 A2 FF CA D0 FD 4C 00 08')
    mem[0x700:0x700+len(boot)]=boot;mem[0x20]=0;mem[0x2000]=0;mem[0x30:0x32]=bytes([0,0x20])
    program=bytearray();groups=[]
    for name,operation,cycles in [('ram',bytes.fromhex('AD 00 20'),4),('vic',bytes.fromhex('AD 12 D0'),4),
        ('potx',bytes.fromhex('AD 19 D4'),4),('poty',bytes.fromhex('AD 1A D4'),4),
        ('collision_zero',bytes.fromhex('AD 1E D0'),4),('forward',bytes.fromhex('AD 80 DE'),4),
        ('ram_rmw',bytes.fromhex('EE 00 20'),6),('zp_rmw',bytes.fromhex('E6 20'),5),
        ('ram_move',bytes.fromhex('AD 00 20 8D 00 30'),8),('indirect_y',bytes.fromhex('B1 30'),5)]:
        start=0x800+len(program);program+=operation*32;groups.append({'name':name,'start':start,'end':0x800+len(program),'cpu_cycles':cycles*32})
    program+=bytes.fromhex('A9 A5 8D 05 21');end=0x800+len(program);program+=bytes([0x4c,end&255,end>>8]);mem[0x800:0x800+len(program)]=program
    return mem,groups

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',type=Path,required=True);a=p.parse_args();out=a.build_dir.resolve()
    if out==ROOT/'build' or not out.is_relative_to(ROOT/'build') or out.exists():p.error('fresh build child required')
    out.mkdir(parents=True);cp=ROOT/'hdl/core/c64';receipt={'schema':1,'status':'FAIL','source_sha256':{},'sources_unchanged':False}
    try:
        (out/'c64_turbo_exact_lane.sv').write_text(ordering.make_lane())
        observed=base.instrument((cp/'ports/fpga64_sid_iec.vhd').read_text()).replace('ext_cpu_di <= cpuDi;','ext_cpu_di <= cpuDi;\nexternal_observation: if EXTERNAL_CPU generate observedCpuRead <= cpuDi; end generate;')
        (out/'fpga64_sid_iec_observed.vhd').write_text(observed)
        (out/'cpu_6510_observed.vhd').write_text(base.instrument_wrapper((cp/'ports/cpu_6510.vhd').read_text()))
        mem,groups=fixture();(out/'rate_memory.hex').write_text(''.join(f'{b:02x}\n' for b in mem))
        (out/'rate_groups.txt').write_text(str(len(groups))+'\n'+''.join(f"{g['name']} {g['start']:04x} {g['end']:04x}\n" for g in groups))
        vhdl=[];section=''
        for raw in (ROOT/'hdl/hdl_sources.txt').read_text().splitlines():
            line=raw.strip()
            if line.startswith('['):section=line
            elif section=='[c64]' and line and not line.startswith('#'):
                src=(ROOT/'hdl'/line.split('|')[0].strip()).resolve()
                if src.suffix=='.vhd':vhdl.append(src)
        sv=[ROOT/p for p in ('third_party/c64/upstream/rtl/mos6526.v','hdl/core/c64/ports/sid_dac.sv','hdl/core/c64/ports/sid_filter.sv','third_party/c64/upstream/rtl/sid/sid_envelope.sv','hdl/core/c64/ports/sid_tables.sv','third_party/c64/upstream/rtl/sid/sid_voice.sv','hdl/core/c64/ports/sid_top.sv','hdl/core/c64/multitini_c64_local_ram.sv','hdl/core/c64/multitini_c64_fio_port.sv','hdl/sim/tb_c64_turbo_rates.sv')]
        sv += [cp/f'multitini_c64_{n}.sv' for n in ('cpu_island','cpu_proxy','island_governor','island_map','island_memory','nmi_delivery','turbo_regs')]
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
        run('xelab',[v/'xelab.bat','--nolog','--relax','--mt','off','--timescale','1ns/1ps','-s','rate_sim','tb_c64_turbo_rates'])
        log=run('simulate',[v/'xsim.bat','--nolog','rate_sim','--runall'])
        if 'TURBO_RATES PASS cases=32 groups=10' not in log:raise RuntimeError('incomplete rate run')
        rows=[]
        for row in csv.DictReader((out/'rates.csv').open()):rows.append({k:v.strip() if k=='group' else int(v) for k,v in row.items()})
        expected_inventory={(video,index,g['name']) for video in range(2) for index in range(16) for g in groups}
        if len(rows)!=320 or {(r['ntsc'],r['index'],r['group']) for r in rows}!=expected_inventory:raise RuntimeError('rate inventory')
        failures=[];spec={g['name']:g for g in groups};data={(r['ntsc'],r['index'],r['group']):r for r in rows}
        for r in rows:
            index=r['index'];name=r['group'];cycles=spec[name]['cpu_cycles'];n=RATES[index]
            if r['cpu_enables']!=cycles:failures.append(f'{r}: wrong delivered NMOS cycle count')
            if name in ('ram','ram_rmw','zp_rmw','ram_move','indirect_y'):
                expected=cycles*64/n
                if abs(r['fast_clocks']-expected)>1:failures.append(f'{r}: RAM grant rate expected {expected}')
            if name in ('vic','potx','poty','collision_zero'):
                if index<=12 and abs(r['fast_clocks']-data[(r['ntsc'],index,'ram')]['fast_clocks'])>1:failures.append(f'{r}: fast read must equal RAM at <=32 slots')
                if index==15 and abs(r['fast_clocks']/32/64-0.078125)>0.003:failures.append(f'{r}: IO2 read target .078125phi2')
            if name=='forward':
                expected=(4 if index==0 else 2 if index<=3 else 1)*64
                if not forward_cadence_ok(r):failures.append(f'{r}: forward cadence expected {expected}')
            r['phi2_per_operation']=r['fast_clocks']/32/64
        receipt.update(cases=rows,case_count=32,measured_groups=320,failed=failures,groups=groups,artifact_sha256={q.name:digest(q) for q in out.iterdir() if q.suffix in ('.csv','.hex') or q.name in ('rate_groups.txt','c64_turbo_exact_lane.sv') or q.name.endswith('_observed.vhd')})
        receipt['sources_unchanged']=all(digest(ROOT/k)==v for k,v in receipt['source_sha256'].items())
        if failures:raise RuntimeError(f'{len(failures)} rate gates failed: {failures[:3]}')
        if not receipt['sources_unchanged']:raise RuntimeError('source changed')
        # Start with a passing measured row and corrupt only its cadence.
        baseline=data[(0,0,'forward')]
        corrupted={**baseline,'de_min':3*64,'de_max':3*64,'de_span':31*3*64}
        if not forward_cadence_ok(baseline) or forward_cadence_ok(corrupted):raise RuntimeError('index0 negative cadence not rejected')
        receipt['negative_control']={'status':'PASS','baseline':baseline,'corrupted':corrupted,'result':'Measured index0 DE80 baseline passes; replacing its 4-phi2 cadence by 3-phi2 is rejected by the same predicate.'}
        receipt['limits']=['Actual production CPU/island/proxy/FIO/VIC/SID/CIA and adapter wiring, synthetic ROM/program.','Badline timing disabled and screen/sprites off isolate delivered rates; BA timing is covered separately.','32 unrolled operations per class; ten classes, all16 indices and PAL/NTSC.','Does not include nonzero collision-read rates, C64U top rates, measured external Doom/irqtest loops, or broad instruction conformance.','No physical board performance claim.']
        receipt['status']='PASS'
    except Exception as e:receipt['error']=repr(e)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps({k:v for k,v in receipt.items() if k not in ('source_sha256','cases','artifact_sha256')},indent=2));return 0 if receipt['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
