#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the documented T-h exception with real FIO/VIC and native replay."""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,subprocess,sys
import test_c64_vic_fast_equivalence as base

ROOT=base.ROOT

def fixture():
    source=(ROOT/'hdl/sim/tb_c64_vic_fast_equivalence.sv').read_text()
    start=source.index('            if(frames>=1&&y>=40&&y<240)begin')
    end=source.index('\n        end\n    end\n    initial',start)
    source=source[:start]+'''            if(frames>=1&&y==59&&state_dut[3:0]==7&&state_dut[22:16]==10)begin
                if(phase==4)enqueue('h11,'h1c);
                if(phase==6)enqueue('h11,'h1b);
            end'''+source[end:]
    source=source.replace("enqueue('h11,'h0b)","enqueue('h11,'h1b)")
    source=source.replace('wait(frames>=3)','wait(frames>=2)')
    source=source.replace('events<100||cycle<30000','events<13||cycle<30000')
    return source

def compare(directory):
    events=list(csv.DictReader((directory/'events.csv').open()))
    pulses=[int(r['tick']) for r in events if r['address']=='17' and r['data']=='28']
    if len(pulses)!=1:raise ValueError(f'Expected exactly one off/on pulse, got {pulses}')
    tick=pulses[0];slot=tick//32
    rows=list(csv.DictReader((directory/'state.csv').open()))
    native=[r for r in rows if r['phase']=='15' and slot<=int(r['tick'])//32<slot+5]
    phi2=[r for r in rows if r['phase']=='27' and slot<=int(r['tick'])//32<slot+5]
    if len(native)!=5 or len(phi2)!=5:raise ValueError('Incomplete pulse samples')
    counts=[(base.read_bits(r['dut'])[0]>>179)&7 for r in native]
    refs=[(base.read_bits(r['reference'])[0]>>179)&7 for r in native]
    aec=[int(r['aec_dut']) for r in phi2];aec_ref=[int(r['aec_reference']) for r in phi2]
    result={'pulse_tick':tick,'ba_count':counts,'reference_ba_count':refs,'aec_phi2':aec,'reference_aec_phi2':aec_ref}
    # The temporary YSCROLL mismatch raises BA between two legal landings.
    # Its falling edge restarts the three-cycle AEC grace period. The mapped
    # native sequence retains only the final YSCROLL and cannot do this.
    if counts!=[0,1,2,3,4] or refs!=[4]*5 or aec!=[0,0,0,1,1] or aec_ref!=[1]*5:
        raise ValueError('T-h boundary differs: '+str(result))
    ignore=(7<<179)
    control_mask=(1<<182)-1
    for r in native:
        a,ax=base.read_bits(r['dut']);b,bx=base.read_bits(r['reference'])
        if ((a^b)|(ax^bx))&control_mask&~ignore:raise ValueError('T-h changed another native control field')
    # Negative control: claiming that last-value collapse preserves BA/AEC
    # must fail for exactly this fixture.
    if counts==refs or aec==aec_ref:raise ValueError('Collapsed-reference negative control accepted')
    return result

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--build-dir',type=Path,required=True)
    args=ap.parse_args();out=args.build_dir.resolve()
    if out.exists() or out==ROOT/'build' or not out.is_relative_to(ROOT/'build'):raise ValueError('Fresh dedicated build directory required')
    out.mkdir(parents=True)
    paths=[Path(__file__).resolve(),ROOT/'scripts/test_c64_vic_fast_equivalence.py',ROOT/'scripts/test_c64_turbo_native_ports.py',ROOT/'scripts/run_native_tool.py',ROOT/base.VIC,ROOT/'hdl/core/c64/multitini_c64_fio_port.sv',ROOT/'hdl/sim/tb_c64_vic_fast_equivalence.sv']
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    hashes=lambda:{p.relative_to(ROOT).as_posix():digest(p) for p in paths}
    initial=hashes();receipt={'status':'FAIL','source_sha256':initial,'stock_commit':base.BASELINE,'cases':[]}
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['PATH']='C:/msys64/ucrt64/bin;'+os.environ.get('PATH',os.environ.get('Path',''))
    def run(name,args):
        r=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*map(str,args)],cwd=out,env=env,capture_output=True,text=True,timeout=600)
        text=r.stdout+r.stderr;(out/(name+'.log')).write_text(text)
        if r.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:',text,re.I):raise RuntimeError(name+'\n'+text[-4000:])
        return text
    try:
        stock=subprocess.check_output(['git','show',base.BASELINE+':'+base.VIC],cwd=ROOT,env=env,text=True)
        (out/'native-reference-original.vhd').write_text(stock)
        (out/'vic_observed.vhd').write_text(base.observe((ROOT/base.VIC).read_text()))
        (out/'vic_reference.vhd').write_text(base.batch_reference(stock))
        (out/'tb_badline.sv').write_text(fixture())
        v=Path('C:/Xilinx/2025.2/Vivado/bin')
        run('xvlog',[v/'xvlog.bat','--nolog','--sv',ROOT/'hdl/core/c64/multitini_c64_fio_port.sv',out/'tb_badline.sv'])
        run('xvhdl',[v/'xvhdl.bat','--nolog','--2008',out/'vic_observed.vhd',out/'vic_reference.vhd'])
        run('xelab',[v/'xelab.bat','--nolog','--relax','--snapshot','badline_sim','tb_c64_vic_fast_equivalence'])
        for mode in ('pal','ntsc'):
            directory=out/mode;directory.mkdir()
            for stage in (0,1):
                (out/'case.hex').write_text(f'{int(mode=="ntsc"):02x}\n{stage:02x}\n00\n00\n')
                text=run(f'{mode}-{stage}',[v/'xsim.bat','--nolog','badline_sim','--runall'])
                if 'PASS VIC trace' not in text:raise ValueError('Trace completion missing')
                for name in ('state.csv','events.csv'):(out/name).replace(directory/name)
                if not stage:
                    base.make_mapping(directory);(out/'mapped.hex').write_bytes((directory/'mapped.hex').read_bytes())
                    for name in ('state.csv','events.csv'):(directory/name).rename(directory/('capture-'+name))
            result=compare(directory);receipt['cases'].append({'mode':mode,**result})
            print('PASS T-h '+mode+' '+str(result),flush=True)
        if hashes()!=initial:raise ValueError('Sources changed')
        receipt.update(status='PASS',sources_unchanged=True,negative_control='PASS: last-value native collapse differs in exactly the BA grace sequence',limits='Directed legal fast-write exception, synthetic constant RAM and FF floating bus; no board claim.')
    except Exception as e:receipt['error']=str(e);raise
    finally:(out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')

if __name__=='__main__':
    try:main()
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as e:print('ERROR: '+str(e),file=sys.stderr);raise SystemExit(1)
