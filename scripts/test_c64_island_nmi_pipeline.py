#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""RTL NMI pipeline versus the history-checked project model, including seams."""
from __future__ import annotations
import argparse,hashlib,importlib.util,json,os,random,re,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()

def rows(model,native,init,en,requests,mode0):
    reg=model.registered(native,init)
    isl=model.Island('rearm',init,'carry')
    mode=want=mode0;start=64 if mode=='exact' else 0
    enables=[];out=[]
    for t,v in enumerate(native):
        want=requests.get(t,want)
        fall=t>0 and reg[t-1] and not reg[t]
        b0=native[(t//64)*64-1] if t>=64 else init
        old_mode=mode;enabled=False;shown=0
        if mode=='turbo':
            if t>=start and t in en:
                enabled=True;shown=isl.shown_turbo(fall,reg[t]);isl.sample(t,shown)
                if want=='exact':
                    mode='exact';isl.carry=True;isl.carry_n=0
                    start=(t//64+(1 if t%64<28 else 2))*64
            else: isl.idle(fall)
        elif t>=start and t%64==0:
            enabled=True;shown=isl.shown_exact(fall,b0);isl.sample(t,shown)
            if want=='turbo':mode='turbo';isl.carry=False;start=t+5
        elif isl.carry:isl.idle(fall)
        else:isl.fell=isl.force=0
        if enabled:enables.append(t)
        out.append(int(v)|(int(b0)<<1)|(int(old_mode=='turbo')<<2)|(int(enabled)<<3)|(int(shown)<<4))
    return out,isl,model.check(isl.edges,enables,reg)

def write_case(f,init,values):
    f.write(f'0 {init:x}\n')
    old=values[0];count=0
    for v in values:
        if v!=old:f.write(f'{count} {old:x}\n');count=0;old=v
        count+=1
    f.write(f'{count} {old:x}\n')

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build-dir',type=Path,required=True)
    a=ap.parse_args();out=a.build_dir.resolve()
    if out.exists() or out==ROOT/'build' or not out.is_relative_to(ROOT/'build'):ap.error('fresh build child required')
    out.mkdir(parents=True)
    sources=[Path(__file__),ROOT/'scripts/run_native_tool.py',ROOT/'tests/c64_turbo/nmi_reference.py',
             ROOT/'hdl/core/c64/multitini_c64_nmi_delivery.sv',ROOT/'hdl/sim/tb_c64_island_nmi_pipeline.sv']
    receipt={'status':'FAIL','scope':'production NMI delivery plus actual native-update/fast-register lag; history-checked synthetic waves, not CIA2 or full-machine validation',
             'source_sha256':{},'sources_unchanged':False,'directed':[],'seeds':{}}
    try:
        for src in sources:
            key=src.relative_to(ROOT).as_posix();receipt['source_sha256'][key]=digest(src)
            dst=out/'sources'/key;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst)
        spec=importlib.util.spec_from_file_location('nmi_reference',out/'sources/tests/c64_turbo/nmi_reference.py')
        model=importlib.util.module_from_spec(spec);spec.loader.exec_module(model)
        directed=out/'directed.txt';all_vectors=out/'vectors.txt'
        with directed.open('w') as d,all_vectors.open('w') as f:
            # S11 with the second event at e30/e31; B0 and live must agree
            # on the eligible enable, without a registered falling pulse.
            for edge in (30,31):
                for seam in ('carry','adopt-turbo'):
                    T=58*64;init=0 if seam=='carry' else 1
                    if seam=='carry':
                        changes=[(15*64+2,1),(30*64+2,0),(31*64+2,1),(51*64+2*(edge+1),0)]
                        en={t for t in range(T) if not 10*64<t<50*64};req={20*64:'exact'};mode='turbo'
                    else:
                        changes=[(3*64+2*(edge+1),0)];en=set(range(T));req={3*64:'turbo'};mode='exact'
                    vals,isl,history=rows(model,model.wave(changes,init,T),init,en,req,mode)
                    # e31 changes after the coincident R sample and is seen
                    # by the next eligible enable; e30 is the same-enable fall.
                    if history[0] or history[1]:raise RuntimeError(f'directed reference history failed {seam}/{edge}: {history}')
                    for target in (d,f):write_case(target,init,vals)
                    receipt['directed'].append({'name':f'{seam}-e{edge}','edges':len(isl.edges),'carry_max':isl.carry_max,'history':history})
            for seed in (5,7,9):
                rng=random.Random(seed);summary={'cases':2000,'falls':0,'edges':0,'later':0,'carry_max':0}
                for case in range(2000):
                    T=60*64;init=rng.randint(0,1);v=init;changes=[]
                    for c in sorted(rng.sample(range(1,55),rng.randint(1,8))):v^=1;changes.append((c*64+2,v))
                    mode=rng.choice(('exact','turbo'));m=mode;req={}
                    for t in sorted(rng.sample(range(64,T-4*64),rng.randint(1,4))):m='turbo' if m=='exact' else 'exact';req[t]=m
                    en=model.random_enables(rng,T)
                    vals,isl,history=rows(model,model.wave(changes,init,T),init,en,req,mode)
                    if history[0] or history[1]:raise RuntimeError(f'bad reference history seed={seed} case={case}: {history}')
                    write_case(f,init,vals);summary['falls']+=history[3];summary['edges']+=len(isl.edges);summary['later']+=history[2]
                    summary['carry_max']=max(summary['carry_max'],isl.carry_max)
                receipt['seeds'][str(seed)]=summary
            if max(s['carry_max'] for s in receipt['seeds'].values())<3:
                raise RuntimeError('seeded waves did not exercise three-enable carry')
        runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'));suffix='.exe' if os.name=='nt' else ''
        guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--cwd',str(out),'--']
        snap=lambda p:out/'sources'/p.relative_to(ROOT)
        def run(name,args,expect=0):
            proc=subprocess.run(guard+[str(runtime/(args[0]+suffix)),*map(str,args[1:])],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',timeout=300)
            (out/f'{name}.log').write_text(proc.stdout,encoding='utf-8')
            if (proc.returncode==0)!=(expect==0):raise RuntimeError(f'{name}: unexpected exit {proc.returncode}')
            return proc.stdout
        for delay in (0,1):
            exe=out/f'pipeline-{delay}.vvp'
            run(f'compile-{delay}',['iverilog','-g2012','-s','tb_c64_island_nmi_pipeline','-P',f'tb_c64_island_nmi_pipeline.EXTRA_LAG={delay}',
                '-o',exe,snap(sources[-2]),snap(sources[-1])])
            log=run(f'simulate-{delay}',['vvp',exe,f'+VECTORS={directed if delay else all_vectors}'],expect=delay)
            if delay:
                if 'NMI_PIPELINE mismatch' not in log:raise RuntimeError('negative control did not reject an NMI sample mismatch')
                receipt['negative_control']={'status':'PASS','mutation':'one additional fast register before falling-edge detection','rejected':True,'log':'simulate-1.log'}
            else:
                m=re.search(r'NMI_PIPELINE PASS cases=(\d+) checks=(\d+) edges=(\d+)',log)
                if not m or int(m[1])!=6004:raise RuntimeError('positive completion missing')
                receipt['rtl']={'cases':int(m[1]),'checks':int(m[2]),'edges':int(m[3])}
        receipt['sources_unchanged']=all(digest(ROOT/k)==v for k,v in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:raise RuntimeError('sources changed')
        receipt['artifacts']={p.name:{'bytes':p.stat().st_size,'sha256':digest(p)} for p in out.iterdir() if p.is_file() and p.suffix in ('.txt','.log')}
        receipt['status']='PASS'
    except Exception as e:receipt['error']=str(e)
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n');print(json.dumps(receipt,indent=2))
    return 0 if receipt['status']=='PASS' else 1
if __name__=='__main__':raise SystemExit(main())
