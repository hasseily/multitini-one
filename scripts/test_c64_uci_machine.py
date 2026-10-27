#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Full-adapter UCI CPU request/IRQ/freeze and HOLD during actual REU/cache DMA."""
from pathlib import Path
import argparse,csv,hashlib,json,os,re,shutil,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def kernal(stash):
    rom=bytearray([0xea]*8192)
    code=bytearray.fromhex('78 D8 A2 FF 9A A9 2F 85 00 A9 37 85 01')
    def store(address,value):code.extend([0xa9,value,0x8d,address&255,address>>8])
    for address,value in [(0xdc0d,0x7f),(0xdd0d,0x7f),(0xdc0e,0),(0xdd0e,0),
                          (0xd01a,0),(0xd019,15),(0xd011,0x1b),(0xd015,0),
                          (0x2100,0),(0x2101,0),(0x2105,0)]:store(address,value)
    code.extend(bytes.fromhex("AD 1B DF 8D 06 21"))
    # More native ticks than the fixture's shortened startup policy counter.
    code.extend(bytes.fromhex('A2 FF CA D0 FD'))
    for stage in range(1,4):
        for address,value in [(0xdf1d,4),(0xdf1d,1),(0xdf1c,0xa1 if stage==1 else 0x21)]:store(address,value)
        code.extend(bytes.fromhex('58 EA AD 01 21 C9')+bytes([stage])+bytes.fromhex('D0 F9 78'))
    if stash:
        code.extend(bytes.fromhex('A2 00 8A 18 69 40 9D 00 31 E8 E0 40 D0 F4'))
    reu_registers=[(0xdf02,0),(0xdf03,0x31 if stash else 0x30),(0xdf04,0),
                   (0xdf05,1 if stash else 0),(0xdf06,0),(0xdf07,64),
                   (0xdf08,0),(0xdf09,0),(0xdf0a,0)]
    for address,value in reu_registers:store(address,value)
    # Align after a complete raster-zero boundary, with DEN latched at48.
    code.extend(bytes.fromhex('AD 11 D0 30 FB AD 12 D0 D0 F6 AD 11 D0 30 F1 AD 12 D0 C9 32 D0 F9'))
    # Both peripherals observe the same accepted FF00 write. UCI trigger61
    # allows execution until FF00, then freezes after the real REU drains.
    store(0xdf01,0x80 if stash else 0x81)
    for address,value in [(0xdf1d,4),(0xdf1d,1),(0xdf1c,0x61),(0x2102,0xa6),(0xff00,0xa5)]:store(address,value)
    code.extend(bytes.fromhex('58 EA AD 01 21 C9 04 D0 F9 78'))
    for address,value in reu_registers:store(address,value)
    store(0xdf01,0x90 if stash else 0x91)
    code.extend(bytes([0xa2,0,0xbd,0,0x31 if stash else 0x30,0x9d,0,0x22,0xe8,0xe0,64,0xd0,0xf5]))
    store(0x2105,0xa5);end=0xe000+len(code);code.extend([0x4c,end&255,end>>8]);rom[:len(code)]=code
    code=bytearray.fromhex('48 AD 1D DF 8D 10 21 AD 01 21 C9 00');labels={};fixups=[]
    def branch(op,label):code.extend([op,0]);fixups.append((len(code)-1,label,False))
    def jump(label):code.extend([0x4c,0,0]);fixups.append((len(code)-2,label,True))
    branch(0xf0,'data');code.extend([0xc9,1]);branch(0xf0,'status');jump('clear')
    labels['data']=len(code);code.extend(bytes.fromhex('AD 1E DF 8D 00 21'));jump('clear')
    labels['status']=len(code);code.extend(bytes.fromhex('AD 1F DF 8D 00 21'))
    labels['clear']=len(code);store(0xdf1c,2)
    code.extend(bytes.fromhex('AD 1D DF 8D 11 21 AD 01 21 18 69 01 8D 01 21 68 40'))
    for offset,label,absolute in fixups:
        target=labels[label]
        if absolute:code[offset:offset+2]=bytes([(0xe800+target)&255,(0xe800+target)>>8])
        else:
            relative=target-(offset+1);assert -128<=relative<=127;code[offset]=relative&255
    rom[0x800:0x800+len(code)]=code;rom[0x900]=0x40
    rom[0x1ffa:]=bytes.fromhex('00 E9 00 E0 00 E8')
    return rom

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',required=True,type=Path)
    parser.add_argument('--skip-negatives',action='store_true')
    args=parser.parse_args();out=args.build_dir.resolve()
    if out.exists() or not out.is_relative_to(ROOT/'build') or out==ROOT/'build':parser.error('fresh build/ child required')
    out.mkdir(parents=True)
    sources=[ROOT/'hdl/generated/multitini_regs_pkg.sv',ROOT/'third_party/appletini/hdl/cdc_bit_sync.sv',
             ROOT/'hdl/common/multitini_async_mailbox.sv']
    section=''
    for raw in (ROOT/'hdl/hdl_sources.txt').read_text().splitlines():
        line=raw.strip()
        if line.startswith('['):section=line
        elif section=='[c64]' and line and not line.startswith('#'):
            path=(ROOT/'hdl'/line.split('|')[0].strip()).resolve()
            if path.suffix in ('.sv','.v','.vhd'):sources.append(path)
    sources.append(ROOT/'hdl/sim/tb_c64_uci_machine.sv')
    evidence=sources+[Path(__file__).resolve(),ROOT/'scripts/run_native_tool.py',ROOT/'hdl/hdl_sources.txt']
    receipt={'status':'FAIL','source_sha256':{p.relative_to(ROOT).as_posix():digest(p) for p in evidence},
             'scope':'Full production adapter, eight SID instances, real CPU/island/proxy/UCI/VIC/CIA/REU/cache; synthetic ROM, host BRAM and bounded memory responder. No physical DDR/network or board claim.'}
    for p in evidence:
        dest=out/'sources'/p.relative_to(ROOT);dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
    snap=lambda p:out/'sources'/p.relative_to(ROOT)
    v=Path(os.environ.get('XILINX_VIVADO','C:/Xilinx/2025.2/Vivado'))/'bin'
    def run(folder,name,argv,fail=None):
        result=subprocess.run([sys.executable,ROOT/'scripts/run_native_tool.py','--cwd',folder,'--',*argv],capture_output=True,text=True,errors='replace',timeout=900)
        log=result.stdout+result.stderr;(folder/(name+'.log')).write_text(log)
        failed=result.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:',log,re.I)
        if fail:
            if not failed or fail not in log:raise RuntimeError(name+' did not reject negative control: '+log[-2000:])
        elif failed:raise RuntimeError(name+' failed: '+log[-5000:])
        return log
    try:
        rows=[]
        for standard in range(2):
            folder=out/('ntsc' if standard else 'pal');folder.mkdir()
            for stash in range(2):(folder/f'kernal-{stash}.hex').write_text(''.join(f'{b:02x}\n' for b in kernal(stash)))
            wrapper=folder/'top.sv';wrapper.write_text(f'module uci_machine_top; tb_c64_uci_machine #(.NTSC({standard})) test(); endmodule\n')
            run(folder,'xvlog',[v/'xvlog.bat','--nolog','--sv','--relax',*[snap(p) for p in sources if p.suffix!='.vhd'],wrapper])
            run(folder,'xvhdl',[v/'xvhdl.bat','--nolog','--2008',*[snap(p) for p in sources if p.suffix=='.vhd']])
            run(folder,'xelab',[v/'xelab.bat','--nolog','--relax','--mt','off','--timescale','1ns/1ps','-s','uci_machine_sim','uci_machine_top'])
            log=run(folder,'simulate',[v/'xsim.bat','--nolog','uci_machine_sim','--runall'])
            if f'PASS UCI machine cases=6 NTSC={standard}' not in log or f'UCI_DECODE PASS checks=11 NTSC={standard}' not in log:raise RuntimeError('incomplete machine matrix/decode sweep')
            rows+=list(csv.DictReader((folder/'uci_machine.csv').open()))
            shutil.copyfile(folder/'uci_machine.csv',folder/'positive_uci_machine.csv')
            print('\n'.join(s for s in log.splitlines() if s.startswith(('UCI_MACHINE_CASE','PASS UCI'))),flush=True)
        if len(rows)!=12 or {(int(r['ntsc']),int(r['mode']),int(r['stash'])) for r in rows}!={(n,m,s) for n in range(2) for m in range(3) for s in range(2)}:raise RuntimeError('missing case inventory')
        receipt['cases']=rows
        receipt['negative_controls']=[]
        if not args.skip_negatives:
            original=snap(ROOT/'hdl/core/c64/multitini_c64_adapter.sv').read_text()
            for name,before,after,failure in [
                ('irq','!(reu_irq || ua_irq || uci_irq)','!(reu_irq || ua_irq)','UCI machine timeout selector=4'),
                ('hold','(uci_hold || uci_freeze || cart_erase_status[16]) && !reu_busy','uci_hold || ((uci_freeze || cart_erase_status[16]) && !reu_busy)','UCI machine timeout selector=2'),
                ('freeze','(uci_hold || uci_freeze || cart_erase_status[16]) && !reu_busy','((uci_hold || cart_erase_status[16]) && !reu_busy) || uci_freeze','UCI freeze paused active REU')]:
                if original.count(before)!=1:raise RuntimeError('negative anchor '+name)
                folder=out/'pal';mutant=folder/f'adapter-{name}.sv';mutant.write_text(original.replace(before,after,1))
                run(folder,name+'-compile',[v/'xvlog.bat','--nolog','--sv','--relax',mutant])
                run(folder,name+'-elaborate',[v/'xelab.bat','--nolog','--relax','--mt','off','--timescale','1ns/1ps','-s','uci_'+name+'_negative','uci_machine_top'])
                run(folder,name+'-negative',[v/'xsim.bat','--nolog','uci_'+name+'_negative','--runall'],failure)
                receipt['negative_controls'].append({'name':name,'rejected':True,'mutant_sha256':digest(mutant)})
        receipt['sources_unchanged']=all(digest(ROOT/k)==h for k,h in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:raise RuntimeError('source changed during validation')
        receipt['status']='PASS'
        receipt['limits']=['32MHz native/64MHz island test clocks for both video standards; timing here is functional, not physical-frequency signoff.',
            'Actual CPU reads DF1B=31 in stock and9 in accelerated modes. Only turbo policy startup reset window shortened to256 native ticks; scheduling, memory and bus modules unchanged.',
            'DDR responder completes linefill bursts within64 native clocks; real HP2 DDR bridge and ARM dispatcher are separately tested.',
            'Actual loaded ROM executes request/freeze, DF1E/DF1F read IRQ acknowledgments and DATA_ACC; DF1D49/C9 observed by CPU. One accepted FF00 starts deferred REU and UCI trigger freeze;64bytes drain before pause. Another64-byte fetch/stash drains while host HOLD arrives; cache drain and resumed CPU readback are checked.',
            'Additional isolated forced-selection decode/reset sweep: none, generic0, other type1, loading, DMA, and aperture boundaries. This does not claim execution of a loaded CRT.']
    except Exception as error:receipt['error']=str(error);print(receipt['error'],flush=True)
    receipt['artifact_sha256']={p.relative_to(out).as_posix():digest(p) for p in out.rglob('*') if p.is_file() and 'sources' not in p.relative_to(out).parts and p.suffix in ('.log','.csv','.hex','.sv')}
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return receipt['status']!='PASS'
if __name__=='__main__':raise SystemExit(main())
