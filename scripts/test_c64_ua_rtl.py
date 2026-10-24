#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise production Ultimate Audio with independent arithmetic/state models."""
from __future__ import annotations
import argparse,csv,hashlib,json,os,re,subprocess,sys
from pathlib import Path
from ua_model import Voice,mix
ROOT=Path(__file__).resolve().parents[1]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,default=ROOT/'build/c64-ua-rtl')
    p.add_argument('--vivado',type=Path,default=Path('C:/Xilinx/2025.2/Vivado'))
    p.add_argument('--only',action='append',help='Run named tb_c64_ua_* bench; default runs every gate')
    args=p.parse_args();build=args.build_dir.resolve()
    if build==(ROOT/'build').resolve() or not build.is_relative_to((ROOT/'build').resolve()):p.error('Use dedicated build child')
    build.mkdir(parents=True,exist_ok=True)
    names=['voice','mix','fetch','top','cache','period','mix_stream']
    benches=[ROOT/f'hdl/sim/tb_c64_ua_{n}.sv' for n in names if (ROOT/f'hdl/sim/tb_c64_ua_{n}.sv').exists()]
    sources=[ROOT/'hdl/core/c64'/f'multitini_c64_ua{s}.sv' for s in ('','_voice','_fetch','_mix')]
    sources += [ROOT/'hdl/core/c64/multitini_c64_reu_cache.sv']
    inputs=[Path(__file__),ROOT/'scripts/ua_model.py',*sources,*benches]
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    hashes=lambda:{str(f.relative_to(ROOT)).replace('\\','/'):sha(f) for f in inputs}
    before=hashes();result={'status':'FAIL','source_sha256':before,'checks':[],
       'limits':'Production modules under synthetic stimulus and acknowledged DDR model. No physical board or physical HP3 latency proof.'}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'}
    env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+os.environ.get('PATH',os.environ.get('Path',''))
    def run(label,tool,*arguments,expect_failure=False):
        child=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',str(args.vivado/'bin'/(tool+'.bat')),*map(str,arguments)],
            cwd=build,env=env,capture_output=True,text=True,errors='replace',timeout=300,creationflags=subprocess.CREATE_NO_WINDOW)
        output=child.stdout+child.stderr;(build/(label+'.log')).write_text(output)
        failed=bool(child.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal\b',output,re.I))
        if failed and not expect_failure:raise RuntimeError(output[-12000:])
        if expect_failure and not failed:raise RuntimeError('Negative control was accepted: '+label)
        return output
    try:
        stream_expected=[]
        if not args.only or 'tb_c64_ua_mix_stream' in args.only:
            state=0x64310a5
            with (build/'mix_stream_vectors.txt').open('w') as out:
                for frame in range(250000):
                    s=[];v=[];a=[]
                    for _ in range(7):
                        state=(state*1664525+1013904223)&0xffffffff
                        s.append((state&65535)-32768);v.append((state>>16)&63);a.append((state>>24)&15)
                    packed=lambda seq,bits:sum((val&((1<<bits)-1))<<(idx*bits) for idx,val in enumerate(seq))
                    l,r=mix(s,v,a);stream_expected.append((l,r))
                    out.write(f'{packed(s,16):028x} {packed(v,6):011x} {packed(a,4):07x} {l} {r}\n')
        with (build/'mix_vectors.txt').open('w') as out:
            vectors=[]
            for sample in (-32768,-123,0,111,32767):
                for vol in range(64):
                    for pan in range(16):vectors.append(([sample]*7,[vol]*7,[pan]*7))
            for i in range(1000):
                vectors.append(([((i*7351+j*6731)&65535)-32768 for j in range(7)],[(i+j*13)&63 for j in range(7)],[(i+j*7)&15 for j in range(7)]))
            for s,v,a in vectors:
                packed=lambda seq,bits:sum((val&((1<<bits)-1))<<(idx*bits) for idx,val in enumerate(seq))
                l,r=mix(s,v,a)
                out.write(f'{packed(s,16):028x} {packed(v,6):011x} {packed(a,4):07x} {l} {r}\n')
        run('compile','xvlog','--nolog','--sv',*sources,*benches)
        available=[b.stem for b in benches]+['tb_c64_ua_period_ntsc','tb_c64_ua_period_pal_release','tb_c64_ua_period_ntsc_release','tb_c64_ua_stress','tb_c64_ua_yield']
        selected=args.only or available
        for name in selected:
            if name not in available:raise ValueError('Unknown bench '+name)
            snapshot=name+'_sim'
            run(name+'-elaborate','xelab','--nolog','--timescale','1ns/1ps','--snapshot',snapshot,name)
            output=run(name+'-simulate','xsim','--nolog',snapshot,'--runall')
            passes=[line for line in output.splitlines() if line.startswith('PASS: UA ')]
            if not passes:raise RuntimeError('Missing completion '+name)
            result['checks']+=passes
            if name in ('tb_c64_ua_top','tb_c64_ua_stress','tb_c64_ua_yield'):
                (build/(name+'-aggregate.csv')).write_bytes((build/'aggregate.csv').read_bytes())
            print('\n'.join(passes),flush=True)
        voice=Voice();rows=list(csv.DictReader((build/'voice_trace.csv').read_text().splitlines())) if 'tb_c64_ua_voice' in selected else []
        for row in rows:
            r={k:int(v) for k,v in row.items()}
            if r['reset']:voice=Voice();event=None
            else:
                voice.control=r['control'];voice.start=r['start'];voice.length=r['length'];voice.repeat_a=r['a'];voice.repeat_b=r['b'];voice.rate=r['rate']
                voice.next_sample=r['next_sample'] if r['ready'] else None
                event=voice.step(bool(r['half_tick']),bool(r['paused']))
            expected=(int(event is not None),voice.output,int(voice.state=='play'),int(voice.irq_set),int(voice.underrun),voice.remaining,voice.position,{'idle':0,'play':1,'finished':2}[voice.state])
            actual=tuple(r[k] for k in ('fetch','sample','playing','irq','underrun','remaining','position','state'))
            if expected!=actual or (event and event[0]!=r['address']):raise AssertionError(f'Voice cycle {r["cycle"]}: model {expected} RTL {actual}')
        if rows:result['checks'].append(f'Independent Python voice model matched {len(rows)} cycles')
        if 'tb_c64_ua_mix_stream' in selected:
            previous=0;normalized=(0,0);count=0
            for row in csv.DictReader((build/'mix_stream.csv').read_text().splitlines()):
                current=int(row['clock'])
                actual=(int(row['left']),int(row['right']))
                if actual!=normalized:raise AssertionError(f'Boxcar stream clock {row["clock"]}: expected {normalized}, got {actual}')
                frames=[stream_expected[(clock-9)//8] if clock>=9 else (0,0) for clock in range(previous+1,current+1)]
                normalized=tuple(sum(f[channel] for f in frames)//len(frames) for channel in (0,1))
                previous=current;count+=1
            result['checks'].append(f'Independent exact boxcar model matched {count} stereo outputs over 2000000 clocks')
        fetch_source=ROOT/'hdl/core/c64/multitini_c64_ua_fetch.sv'
        original=fetch_source.read_text()
        controls={
            'defer_off':('if(POST_B_HOLD>0 && cache_write_done && cache_write_done_addr==tag) blocked=1;',
                         'if(POST_B_HOLD>0 && cache_write_done && cache_write_done_addr==tag) blocked=1; blocked=0;'),
            'snoop_off':('snoop_valid<=cache_write_snoop_valid;','snoop_valid<=0;'),
            'resident_snoop_off':('!(snoop_valid && snoop_tag==tag);','1\'b1;'),
            'hold_zero':('parameter integer POST_B_HOLD=16','parameter integer POST_B_HOLD=0'),
        }
        for label,(needle,replacement) in (controls.items() if 'tb_c64_ua_cache' in selected else []):
            if original.count(needle)!=1:raise RuntimeError('Negative-control anchor changed: '+label)
            mutant=build/(label+'.sv');mutant.write_text(original.replace(needle,replacement))
            run(label+'-compile','xvlog','--nolog','--sv',mutant)
            run(label+'-elaborate','xelab','--nolog','--timescale','1ns/1ps','--snapshot',label,'tb_c64_ua_cache')
            run(label+'-simulate','xsim','--nolog',label,'--runall',expect_failure=True)
            result['checks'].append('Rejected coherence negative control: '+label)
        # Keep this build directory's work library bound to production RTL.
        run('restore-production','xvlog','--nolog','--sv',fetch_source)
        if hashes()!=before:raise RuntimeError('Source changed during run')
        result.update(status='PASS',sources_unchanged=True,selected_benches=selected)
    except Exception as error:result['error']=str(error);raise
    finally:(build/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    return 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except (AssertionError,OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        print('ERROR: '+str(error),file=sys.stderr);raise SystemExit(1)
