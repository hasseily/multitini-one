#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute an original PIO diagnostic on real Minimig stock/020 CPUs.

The production memory, selected Gayle controller, management bridge and bulk
sector mover are used. Only the four-frame guest reset delay is shortened.
This is simulation evidence, not FPGA timing or board/OS compatibility proof.
"""
from __future__ import annotations
import argparse,hashlib,importlib.util,json,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'software/amiga'))
from m68k import Assembler
from test_amiga_ports import machine_sources

def diagnostic(odd_fifo=False,classic=False):
    a=Assembler();i=a.ins
    i('move.b','#2','0xbfe001.l')
    i('move.w','#0x8000','0xdaa000.l')
    i('lea','0xda0000.l','a0');i('lea','0x2000.l','a1')
    def command(value):
        for reg,value in ((8,1),(12,1),(16,0),(20,0),(24,0xe0),(28,value)):
            i('move.b',f'#{value}',f'{0xda0000+reg:#x}.l')
    def wait_drq(name):
        a.label(name);i('move.b','0xda001c.l','d0');i('andi.b','#8','d0');i('beq',name)
    i('move.w','#1','0x1000.l');command(0x20);wait_drq('read_drq');i('moveq','#0','d1')
    expected=bytearray();checksum=0;n=0
    def word(n):
        raw=((0xa55a ^ (n*0x103))+0x1739)&0xffff
        return ((raw&255)<<8)|(raw>>8)
    while n<256:
        w=word(n);expected+=w.to_bytes(2,'big')
        if n==1:
            # Odd byte lane must not pop; even byte lane consumes one word.
            # Classic has the original unqualified read strobe; its odd
            # data byte consumes a word. Probe its odd STATUS byte instead.
            i('move.b','0xda001d.l' if classic else '0xda0001.l','d0');i('move.b','d0','0x1100.l')
            i('moveq','#0','d0');i('move.b','(a0)','d0');i('move.b','d0','0x1101.l')
            i('add.l','d0','d1');checksum=(checksum+(w>>8))&0xffffffff
            i('lea','0x2004.l','a1');n+=1
        elif n>=(3 if odd_fifo else 4) and n%4==(3 if odd_fifo else 0) and n<255:
            w2=word(n+1);expected+=w2.to_bytes(2,'big')
            i('move.l','(a0)','d0');i('add.l','d0','d1');i('move.l','d0','(a1)+')
            checksum=(checksum+(w<<16|w2))&0xffffffff;n+=2
        else:
            i('moveq','#0','d0');i('move.w','(a0)','d0');i('add.l','d0','d1');i('move.w','d0','(a1)+')
            checksum=(checksum+w)&0xffffffff;n+=1
    i('move.l','d1','0x1010.l');i('move.w','#2','0x1000.l')
    command(0x30);wait_drq('write_drq');n=0
    while n<256:
        w=(0x3456+n*0x113)&0xffff
        if n%4==(3 if odd_fifo else 0) and n<255:
            w2=(0x3456+(n+1)*0x113)&0xffff
            i('move.l',f'#{(w<<16|w2):#x}','(a0)');n+=2
        else:i('move.w',f'#{w:#x}','(a0)');n+=1
    a.label('write_done');i('move.b','0xda001c.l','d0');i('andi.b','#0x80','d0');i('bne','write_done')
    i('move.w','#3','0x1000.l');i('move.w','#0xf0','0xdff180.l');a.label('finished');i('bra','finished')
    code=b'\x00\x1f\xf0\x00\x00\xf8\x00\x08'+a.finish()
    return code,expected,checksum,a.listing

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=ROOT/'build/amiga-fastide-tests')
    p.add_argument('--modes',default='28,14,stock,classic,classic14')
    p.add_argument('--odd-fifo',action='store_true',help='Start longword reads at odd FIFO word positions')
    p.add_argument('--mutation',action='store_true',help='Require the pinned uncorrected odd-word packing to fail the actual CPU data oracle')
    p.add_argument('--vivado',type=Path,default=Path('C:/Xilinx/2025.2/Vivado'))
    args=p.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build'):raise SystemExit('Use a project-local build directory')
    out.mkdir(parents=True,exist_ok=True)
    code,expected,checksum,listing=diagnostic(args.odd_fifo)
    if len(code)>8192:raise SystemExit('Diagnostic exceeds fixture ROM')
    (out/'fastide-rom.hex').write_text(''.join(f'{int.from_bytes(code[n:n+2],"big"):04x}\n' for n in range(0,len(code),2)))
    (out/'fastide-expected.hex').write_text(''.join(f'{x:02x}\n' for x in expected))
    (out/'fastide-checksum.hex').write_text(f'{checksum:08x}\n')
    (out/'diagnostic-listing.txt').write_text('\n'.join(f'{offset+8:04x} {text}' for offset,text in listing)+'\n')
    vhdlbase=ROOT/'third_party/amiga/upstream/rtl/tg68k'
    vhdl=[vhdlbase/name for name in ('TG68K_Pack.vhd','TG68K_ALU.vhd','TG68K_MMU030.vhd','TG68KdotC_Kernel.vhd','TG68K.vhd')]
    sources=machine_sources()+[ROOT/x for x in ('third_party/appletini/hdl/cdc_bit_sync.sv','hdl/sim/amiga_cdc_model.sv','hdl/common/multitini_async_mailbox.sv',
        'hdl/core/amiga/multitini_amiga_ide_block.sv','hdl/core/amiga/multitini_amiga_diagnostics.sv','hdl/core/amiga/multitini_amiga_host.sv','hdl/core/amiga/multitini_amiga_memory.sv',
        'hdl/sim/tb_amiga_ports.sv','hdl/sim/tb_amiga_fastide.sv')]
    mutation=None
    if args.mutation:
        if not args.odd_fifo:raise SystemExit('The packing mutation requires --odd-fifo')
        from port_amiga_core import hoist_module_declarations,NOTICE
        mutation=out/'synthetic_original_ide.v'
        mutation.write_text(NOTICE+hoist_module_declarations((ROOT/'third_party/amiga/upstream/rtl/ide.v').read_text()))
        sources[sources.index(ROOT/'hdl/core/amiga/ports/ide.v')]=mutation
    assets=[ROOT/'hdl/core/amiga/ports'/n for n in ('m68k_cyc_idx.hex','m68k_ea_idx.hex')]
    assets +=[ROOT/'third_party/amiga/upstream/rtl/fx68k'/n for n in ('microrom.mem','nanorom.mem')]
    for asset in assets:shutil.copyfile(asset,out/asset.name)
    tracked=list(dict.fromkeys(vhdl+sources+assets+[Path(__file__).resolve(),ROOT/'scripts/test_amiga_ports.py',ROOT/'software/amiga/m68k.py']))
    def hashes():return {q.relative_to(ROOT).as_posix():hashlib.sha256(q.read_bytes()).hexdigest() for q in tracked}
    before=hashes();(out/'inputs.json').write_text(json.dumps(before,indent=2)+'\n')
    def run(name,arguments,tag):
        executable=args.vivado/'bin'/f'{name}.bat'
        command=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--cwd',str(out),'--','cmd.exe','/d','/c',str(executable)]+arguments
        result=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,errors='replace',timeout=600)
        log=result.stdout+result.stderr;(out/f'{tag}.log').write_text(log)
        if result.returncode:print(log[-9000:]);raise SystemExit(result.returncode)
        print(tag+': PASS',flush=True);return log
    run('xvhdl',['--2008','--work','work']+[str(q) for q in vhdl],'vhdl')
    run('xvlog',['--sv','--work','work','-i',str(ROOT/'third_party/amiga/upstream/rtl')]+[str(q) for q in sources],'verilog')
    modes=args.modes.split(',');diagnostics={}
    for mode in modes:
        code,expected,checksum,listing=diagnostic(args.odd_fifo,mode in ('stock','classic','classic14'))
        (out/'fastide-rom.hex').write_text(''.join(f'{int.from_bytes(code[n:n+2],"big"):04x}\n' for n in range(0,len(code),2)))
        (out/'fastide-expected.hex').write_text(''.join(f'{x:02x}\n' for x in expected))
        (out/'fastide-checksum.hex').write_text(f'{checksum:08x}\n')
        (out/f'diagnostic-{mode}-listing.txt').write_text('\n'.join(f'{offset+8:04x} {text}' for offset,text in listing)+'\n')
        diagnostics[mode]={'bytes':len(code),'sha256':hashlib.sha256(code).hexdigest(),'expected_checksum':f'{checksum:08x}'}
        run('xelab',[f'work.tb_amiga_fastide_{mode}','-s',f'fastide_{mode}','--debug','typical','--timescale','1ns/1ps','--mt','auto'],f'elaborate-{mode}')
        log=run('xsim',[f'fastide_{mode}','--runall'],f'simulate-{mode}')
        if args.mutation:
            if 'Fatal: Guest READ buffer byte6' not in log:raise SystemExit('Original FIFO packing mutation did not fail at its independently expected byte')
        elif 'PASS FAST IDE real CPU' not in log or 'Fatal:' in log:raise SystemExit(f'Native {mode} diagnostic did not pass')
    if not args.mutation:
        run('xelab',['work.tb_amiga_fastide_fifo32','-s','fifo32','--debug','typical','--timescale','1ns/1ps','--mt','auto'],'elaborate-fifo32')
        log=run('xsim',['fifo32','--runall'],'simulate-fifo32')
        if 'PASS IDE direct32 odd READ/WRITE256' not in log or 'Fatal:' in log:raise SystemExit('Direct odd io32 RAM-bank test did not pass')
    if hashes()!=before:raise SystemExit('Native sources changed during validation')
    receipt={'status':'EXPECTED_FAILURE_REPRODUCED' if args.mutation else 'PASS','board_evidence':False,'modes':modes,'odd_fifo':args.odd_fifo,'source_sha256':before,
        'diagnostics':diagnostics,
        'bounds':'Real stock/TG CPUs, production memory/backend/host/bulk, variable DDR latency and held native edges; reset delay shortened; no OS or hardware proof'}
    (out/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return 0
if __name__=='__main__':raise SystemExit(main())
