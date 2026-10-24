#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Full-frame fast VIC replay against native register timing and sticky events."""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,subprocess,sys
from test_c64_turbo_native_ports import BASELINE,VIC,observed_vic
ROOT=Path(__file__).resolve().parents[1]

def observe(source):
    return observed_vic(source).replace('        observe_state <= s;',
        "        s(296) := observe_bit(lastLineFlag);\n        s(297) := lightPenHit;\n        s(311 downto 298) := std_logic_vector(vicAddrLoc);\n        observe_state <= s;")

def batch_reference(stock):
    """Batch independent registers, retaining native per-field write strobes.

    Several values of ONE register collapse before this transformation. This
    is deliberately an oracle-only stimulus expansion, not production RTL.
    """
    stock=observe(stock)
    stock=stock.replace('\tport (\n','\tport (\n\t\treplay_mask : in std_logic_vector(63 downto 0);\n\t\treplay_data : in std_logic_vector(511 downto 0);\n\t\treplay_den_enable, replay_den : in std_logic;\n',1)
    begin=stock.index('writeRegisters: process(clk)');end=stock.index('end process;',begin)
    body=stock[begin:end]
    body=body.replace('process(clk)\n','process(clk)\n    variable replay_byte : unsigned(7 downto 0);\n',1)
    count=0
    def expand(match):
        nonlocal count
        count+=1
        cases=match[2].replace('diRegisters','replay_byte').replace('di_r','replay_byte')
        return 'for j in 0 to 63 loop\nif replay_mask(j)=\'1\' then\nreplay_byte := unsigned(replay_data(j*8+7 downto j*8));\ncase to_unsigned(j,6) is'+cases+'end case;\nend if;\nend loop;'
    body=re.sub(r'case (aRegisters|addr_r) is(.*?)end case;',expand,body,flags=re.S)
    if count!=5:raise ValueError(f'expected 5 stock register cases, found {count}')
    stock=stock[:begin]+body+stock[end:]
    anchor="\t\t\tif (rasterY = 248) then\n\t\t\t\trasterEnable <= '0';\n\t\t\tend if;"
    if stock.count(anchor)!=1:raise ValueError('stock DEN anchor')
    stock=stock.replace(anchor,anchor+"\n            if replay_den_enable='1' then rasterEnable<=replay_den; end if;",1)
    return stock.replace('video_vicii_656x','video_vicii_656x_reference')

def read_bits(text):
    text=text.lower()
    value=int(text.replace('x','0').replace('z','0'),16)
    unknown=int(''.join('f' if c in 'xz' else '0' for c in text),16)
    return value,unknown

def make_mapping(directory):
    events={int(row['tick']):row for row in csv.DictReader((directory/'events.csv').open())}
    words=[0]*65536;den=0;cmp=0;enabled=0;irst=0;done=0;ack=0;ilp=0;hit=0
    sticky_errors=[];samples=0;frames=0;last_y=0;hist=[];den_pulses=0
    with (directory/'state.csv').open() as handle:
        for row in csv.DictReader(handle):
            tick=int(row['tick']);phase=int(row['phase']);y=int(row['y']);state,unknown=read_bits(row['dut'])
            if y<last_y:frames+=1
            last_y=y
            if frames>=1:
                samples+=1
                actual=tuple(int(row[name]) for name in ('irst','ilp','den','done'))
                expected=(irst,ilp,enabled,done)
                if actual!=expected and len(sticky_errors)<30:sticky_errors.append({'tick':tick,'actual':actual,'expected':expected})
            # State observed above is pre-edge. Native processes read the old
            # register values; this edge's landing becomes visible next edge.
            if y==48 and den:enabled=1
            if y==248:enabled=0
            if ack&1:irst=0
            if y!=cmp:done=0
            elif not done:done=1;irst=1
            if ack&8:ilp=0
            if row['last_line']=='1':hit=0
            elif not hit and row['lp']=='0':hit=1;ilp=1
            ack=0
            event=events.get(tick)
            if event:
                address=int(event['address']);data=int(event['data'])
                slot=tick//32+(1 if phase>=15 else 0)
                words[slot] |= 1 << (512+address)
                words[slot] &= ~(255 << (address*8))
                words[slot] |= data << (address*8)
                if address==0x11:den=(data>>4)&1;cmp=(cmp&255)|((data&128)<<1);den_pulses+=int(y==48 and den)
                if address==0x12:cmp=(cmp&256)|data
                if address==0x19:ack=data
            if phase==15:
                # At pre-e15, all legal early D011 landings have settled.
                if enabled:words[tick//32] |= 1<<576
                hist.append({'tick':tick,'den':enabled})
    (directory/'sticky-validation.json').write_text(json.dumps({'status':'FAIL' if sticky_errors else 'PASS','samples':samples,'errors':sticky_errors,'den_pulses':den_pulses},indent=2)+'\n')
    if sticky_errors:raise RuntimeError(f'per-event sticky scoreboard mismatch {sticky_errors[:3]}')
    (directory/'mapped.hex').write_text('\n'.join(f'{word:0145x}' for word in words)+'\n')
    return {'sticky_samples':samples,'events':len(events),'den_pulses':den_pulses}

def compare(directory):
    # Class-(b) fields only. Pixel/latch state has a separate event contract.
    fields={'cycle':(0,4),'raster':(4,19),'VC':(23,10),'VCBASE':(33,10),'RC':(43,3),'idle':(46,1),
        'rasterEnable':(47,1),'MDMA':(48,8),'MActive':(56,8),'MCBase':(64,48),'MCnt':(112,48),
        'MYE_ff':(160,8),'MPtr':(168,8),'fetch_modes':(176,2),'BA':(178,1),'baCnt':(179,3),'sprite':(229,3)}
    errors=[];total=0;checks=0;frames=0;last_y=0;counts={name:0 for name in fields}
    fetch_checks=0;allowed_fetches=0;sprite_data_checks=0;aec_checks=0
    d018={}
    for event in csv.DictReader((directory/'events.csv').open()):
        if int(event['address'])==0x18:d018.setdefault(int(event['tick'])//32,[]).append(int(event['phase']))
    with (directory/'state.csv').open() as handle:
        for row in csv.DictReader(handle):
            y=int(row['y']);frames+=int(y<last_y);last_y=y
            if frames<1:continue
            a,ax=read_bits(row['dut']);b,bx=read_bits(row['reference'])
            if row['phase'] in ('0','16'):
                sprite_data_checks+=1
                mask=65535<<213
                if ((a^b)|(ax^bx))&mask:
                    total+=1
                    if len(errors)<50:errors.append({'tick':int(row['tick']),'field':'MPixelStore','dut':(a>>213)&65535,'reference':(b>>213)&65535})
            if row['phase']=='27':
                aec_checks+=1
                if row['aec_dut']!=row['aec_reference']:
                    total+=1
                    if len(errors)<50:errors.append({'tick':int(row['tick']),'field':'aec','dut':row['aec_dut'],'reference':row['aec_reference']})
            if row['phase'] in ('11','27'):
                fetch_checks+=1;phase=int(row['phase']);slot=int(row['tick'])//32
                allow=any((11<=p<=14 if phase==11 else 15<=p<=26) for p in d018.get(slot,[]))
                mask=((1<<14)-1)<<298
                if ((a^b)|(ax^bx))&mask:
                    if allow:allowed_fetches+=1
                    else:
                        total+=1
                        if len(errors)<50:errors.append({'tick':int(row['tick']),'field':'phi1_address' if phase==11 else 'phi2_address','dut':(a>>298)&16383,'reference':(b>>298)&16383})
            if row['phase']!='15':continue
            checks+=1
            for name,(shift,width) in fields.items():
                mask=((1<<width)-1)<<shift
                if ((a^b)|(ax^bx))&mask:
                    total+=1;counts[name]+=1
                    if len(errors)<50:errors.append({'tick':int(row['tick']),'field':name,'dut':(a&mask)>>shift,'reference':(b&mask)>>shift,'unknown_dut':(ax&mask)>>shift,'unknown_reference':(bx&mask)>>shift})
            if row['ba_reu']!=row['ba_reference']:
                total+=1
                if len(errors)<50:errors.append({'tick':int(row['tick']),'field':'ba_reu','dut':row['ba_reu'],'reference':row['ba_reference']})
    result={'status':'FAIL' if total else 'PASS','native_samples':checks,'fetch_samples':fetch_checks,'aec_samples':aec_checks,'sprite_data_samples':sprite_data_checks,'allowed_d018_fetches':allowed_fetches,'mismatches':total,'by_field':counts,'first_mismatches':errors}
    (directory/'comparison.json').write_text(json.dumps(result,indent=2)+'\n')
    return result

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build-dir',type=Path,required=True)
    parser.add_argument('--modes',nargs='+',choices=['pal','ntsc'],default=['pal','ntsc'])
    parser.add_argument('--mutants',nargs='+',type=int,choices=[0,1,2,3,4],default=[0,1,2,3,4])
    args=parser.parse_args();out=args.build_dir.resolve()
    if not out.is_relative_to(ROOT/'build') or out.exists() or out==ROOT/'build' or len(set(args.modes))!=len(args.modes) or len(set(args.mutants))!=len(args.mutants):raise ValueError('fresh dedicated build directory and unique modes/mutants required')
    out.mkdir(parents=True)
    env={k:v for k,v in os.environ.items() if k.upper()!='PATH'};env['PATH']='C:/msys64/ucrt64/bin;'+os.environ.get('PATH',os.environ.get('Path',''))
    sources=[Path(__file__).resolve(),ROOT/'scripts/test_c64_turbo_native_ports.py',ROOT/'scripts/run_native_tool.py',ROOT/VIC,ROOT/'hdl/core/c64/multitini_c64_fio_port.sv',ROOT/'hdl/sim/tb_c64_vic_fast_equivalence.sv']
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    before={str(p.relative_to(ROOT)):digest(p) for p in sources};receipt={'status':'RUNNING','source_sha256':before,'stock_commit':BASELINE,'cases':[]}
    def run(directory,name,args):
        result=subprocess.run([sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--',*map(str,args)],cwd=directory,env=env,capture_output=True,text=True,timeout=1200)
        text=result.stdout+result.stderr;(directory/(name+'.log')).write_text(text)
        if result.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:',text,re.I):raise RuntimeError(name+'\n'+text[-5000:])
        return text
    try:
        stock=subprocess.check_output(['git','show',BASELINE+':'+VIC],cwd=ROOT,env=env,text=True)
        (out/'native-reference-original.vhd').write_text(stock)
        (out/'vic_observed.vhd').write_text(observe((ROOT/VIC).read_text()))
        (out/'vic_reference.vhd').write_text(batch_reference(stock))
        v=Path('C:/Xilinx/2025.2/Vivado/bin')
        run(out,'xvlog',[v/'xvlog.bat','--nolog','--sv',ROOT/'hdl/core/c64/multitini_c64_fio_port.sv',ROOT/'hdl/sim/tb_c64_vic_fast_equivalence.sv'])
        run(out,'xvhdl',[v/'xvhdl.bat','--nolog','--2008',out/'vic_observed.vhd',out/'vic_reference.vhd'])
        run(out,'xelab',[v/'xelab.bat','--nolog','--relax','--snapshot','vic_equivalence','tb_c64_vic_fast_equivalence'])
        for mode,mutant in ((mode,mutant) for mode in args.modes for mutant in args.mutants):
            directory=out/f'{mode}-mutant{mutant}';directory.mkdir()
            for stage in (0,1):
                (out/'case.hex').write_text(f'{int(mode=="ntsc"):02x}\n{stage:02x}\n{mutant:02x}\n00\n')
                text=run(out,f'{mode}-{mutant}-{stage}',[v/'xsim.bat','--nolog','vic_equivalence','--runall'])
                if 'PASS VIC trace' not in text:raise RuntimeError('missing explicit trace pass')
                for name in ('state.csv','events.csv'):(out/name).replace(directory/(('capture-' if not stage else '')+name))
                if stage==0:
                    (directory/'capture-state.csv').rename(directory/'state.csv');(directory/'capture-events.csv').rename(directory/'events.csv')
                    summary=make_mapping(directory)
                    (out/'mapped.hex').write_bytes((directory/'mapped.hex').read_bytes())
                    (directory/'state.csv').rename(directory/'capture-state.csv');(directory/'events.csv').rename(directory/'capture-events.csv')
                else:
                    result=compare(directory);receipt['cases'].append({'mode':mode,'mutant':mutant,**summary,**result})
                    if mutant==0 and result['status']!='PASS':raise RuntimeError(f'{mode} native comparison {result["mismatches"]} differences: {result["first_mismatches"][:3]}')
                    if mutant!=0 and result['status']!='FAIL':raise RuntimeError(f'{mode} mutant {mutant} was not rejected')
            print(f'PASS {mode} mutant={mutant} '+('equivalent' if mutant==0 else 'rejected'),flush=True)
        receipt['sources_unchanged']=before=={str(p.relative_to(ROOT)):digest(p) for p in sources}
        if not receipt['sources_unchanged']:raise RuntimeError('sources changed')
        receipt['status']='PASS'
    except Exception as error:receipt.update(status='FAIL',error=str(error));raise
    finally:(out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return 0

if __name__=='__main__':
    try:raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:print('ERROR: '+str(error),file=sys.stderr);raise SystemExit(1)
