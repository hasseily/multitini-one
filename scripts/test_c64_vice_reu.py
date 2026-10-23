#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the production REU with unchanged VICE 3.10 register/DMA methods."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
REF = ROOT/'third_party/vice_reference/upstream/vice/src/c64/cart/reu.c'
PIN = '4d283a2e7dd59b7e378524878e81ecc7826b700c'
from test_c64_reu import Vectors, mame_vectors


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(src, name):
    pattern = r'^(?:(?:inline )?static )?(?:void|int|uint8_t|unsigned int) '+name+r'\([^;]*?\)\n\{'
    match = re.search(pattern, src, re.M)
    if not match:
        raise ValueError(name)
    depth = 1
    # Ignore braces inside comments and strings; retain the method verbatim.
    for token in re.finditer(r'/\*[\s\S]*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27|[{}]', src[match.end():]):
        if token[0] == '{': depth += 1
        elif token[0] == '}': depth -= 1
        if not depth:
            end = match.end()+token.end()
            return src[match.start():end], src.count('\n',0,match.start())+1, src.count('\n',0,end)+1
    raise ValueError('Unclosed '+name)


def vectors(bits):
    # Retain the broad historical stimulus matrix, but obtain every expected
    # observable byte from VICE, including the known MAME disagreements.
    v = mame_vectors()
    v.cases = [name.replace('irq-mask-does-not-retroactively-assert-or-clear',
                           'vice-mask-enables-existing-status-without-clearing-irq') for name in v.cases]
    mask = (1 << bits)-1
    def advance(a, i): return ((a & 0xf80000) | ((a+i) & 0x7ffff)) & mask
    def cb(a): return (a ^ (a >> 8) ^ 0x5a) & 255
    for base in (0x07fffe,0x87fffe,0xfffffe):
        for op in range(4):
            v.case(f'window-{base:06x}-op-{op}')
            v.setup(0x20fe,base,5)
            if op == 3:
                for i in range(5): v.put(6,advance(base,i),cb(0x20fe+i))
            v.write(1,0x90|op); v.put(4); v.dump()
            for i in range(5): v.put(7,advance(base,i))
    for length in (2,3,5):
        for final_matches in (False,True):
            for fixed in (0,0x40,0x80,0xc0):
                v.case(f'penultimate-{length}-last-match-{int(final_matches)}-fixed-{fixed:02x}')
                v.setup(0x3000,0x12340,length,fixed,0xe0)
                for i in range(length):
                    ca=0x3000+(0 if fixed&0x80 else i)
                    ra=0x12340+(0 if fixed&0x40 else i)
                    val=cb(ca) ^ (1 if i==length-2 or (i==length-1 and not final_matches) else 0)
                    v.put(6,ra,val)
                v.write(1,0x93); v.put(4); v.dump()
    for reg in (4,5,6):
        v.case(f'half-autoload-current-bank-vs-low-address-reg-{reg}')
        v.setup(0x20ff,0x2ffff,2); v.write(1,0x90); v.put(4)
        v.write(reg,0x45); v.dump(); v.write(1,0xb0); v.put(4); v.dump()
    for command in (0x01,0x11,0x00,0x21):
        v.case(f'ff00-armed-survives-command-{command:02x}')
        v.setup(0x2080,0x12340,3); v.write(1,0x81); v.write(1,command)
        v.put(3); v.put(4); v.dump(); v.put(3); v.put(4); v.dump()
    v.case('ff00-immediate-command-consumes-armed-latch')
    v.setup(0x2080,0x12340,3); v.write(1,0x81); v.write(1,0x91)
    v.put(4); v.put(3); v.put(4); v.dump()
    v.case('mask-enable-after-verify-error')
    v.setup(0x2000,0x400,1); v.write(1,0x93); v.put(4)
    v.write(9,0xa0); v.put(4); v.write(9,0); v.put(4); v.read(0); v.read(0)
    v.case('fixed-c64-font-stream')
    v.setup(0x0083,0x23456,64,0x80); v.write(1,0x91); v.put(4); v.dump()
    v.case('stash-then-fetch-read-after-write')
    v.setup(0x2000,0x10000,32); v.write(1,0x90); v.put(4)
    v.setup(0x3000,0x10000,32); v.write(1,0x91); v.put(4); v.dump()
    return v


def observable(path):
    # Physical/cache accesses are not a VICE API. Compare ordered C64 bus,
    # stored REU bytes, register readback and IRQ state, without backend logs.
    return [line for line in path.read_text().splitlines() if not line.startswith('M ')]


def compare(actual, expected):
    if actual != expected:
        first=next((i for i,(a,b) in enumerate(zip(actual,expected)) if a!=b),min(len(actual),len(expected)))
        raise RuntimeError(f'REU differs at {first}: RTL={actual[first:first+5]}, VICE={expected[first:first+5]}; lengths={len(actual)}/{len(expected)}')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/c64-vice-reu')
    args=parser.parse_args(); out=args.build_dir.resolve(); base=(ROOT/'build').resolve()
    if out==base or not out.is_relative_to(base): parser.error('Use a dedicated build/ child')
    out.mkdir(parents=True,exist_ok=True); receipt=out/'validation.json'; receipt.unlink(missing_ok=True)
    paths=[REF,Path(__file__).resolve(),ROOT/'scripts/test_c64_reu.py',ROOT/'scripts/run_native_tool.py',
           ROOT/'tests/c64_vice/reu_harness.cpp',ROOT/'hdl/core/c64/multitini_c64_reu.sv',
           ROOT/'hdl/sim/tb_c64_reu.sv',ROOT/'hdl/sim/tb_c64_reu_transport.sv']
    hashes=lambda:{p.relative_to(ROOT).as_posix():sha(p) for p in paths}
    initial=hashes()
    result={'schema':1,'status':'FAIL','reference_commit':PIN,'source_sha256':initial,'checks':[],
            'limits':['Unchanged VICE REU methods with flat synthetic C64 and REU memory. No private software or system ROMs.',
                      'Ordered C64 transactions, selected stored REU bytes, registers and IRQ are compared. No VIC arbitration, native timing, cache/DDR latency or full-machine equivalence claim.',
                      '19-bit and 24-bit profiles both wrap the low19 REC counter. Upper24-bit bank latch is write-only; CPU bank reads set bits7..3.']}
    runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    env={k:v for k,v in os.environ.items() if k.lower()!='path'}
    env['PATH']=str(runtime)+os.pathsep+next((v for k,v in os.environ.items() if k.lower()=='path'),'')
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--'] if os.name=='nt' else []
    def run(name, args, timeout=300):
        proc=subprocess.run(guard+list(map(str,args)),cwd=out,env=env,capture_output=True,text=True,errors='replace',timeout=timeout)
        output=proc.stdout+proc.stderr; (out/(name+'.log')).write_text(output)
        if proc.returncode or re.search(r'\bFATAL\b|\bERROR:',output): raise RuntimeError(name+' failed: '+output[-5000:])
        return output
    try:
        # Pin independently of generated expected traces.
        from c64_vice_oracle import verify_reference
        verify_reference()
        src=REF.read_text()
        names=['set_reu_size','reu_ba_register','reu_reset','nonsc_reu_clk_inc_pre','reu_clk_inc_post_write','reu_clk_inc_post_read','reu_read_without_sideeffects','reu_store_without_sideeffects','reu_io2_read','reu_io2_store','increment_reu_with_wrap_around','store_to_reu','read_from_reu','reu_dma_update_regs','reu_dma_host_to_reu','reu_dma_reu_to_host','reu_dma_swap','reu_dma_compare','reu_dma','reu_dma_start']
        chunks=[extract(src,n) for n in names]
        (out/'vice_reu_declarations.inc').write_text('// VICE 3.10 reu.c, GPL-2.0-or-later.\n'+src[src.index('enum {'):src.index('static io_source_t reu_io2_device =')])
        (out/'vice_reu_methods.inc').write_text('// Unchanged VICE 3.10 methods, GPL-2.0-or-later.\n'+'\n\n'.join(x[0] for x in chunks)+'\n')
        (out/'extraction.json').write_text(json.dumps({'commit':PIN,'source_sha256':sha(REF),'methods':{n:{'first_line':x[1],'last_line':x[2],'text_sha256':hashlib.sha256(x[0].encode()).hexdigest()} for n,x in zip(names,chunks)}},indent=2)+'\n')
        compiler=runtime/'g++.exe' if os.name=='nt' else 'g++'
        iv=runtime/'iverilog.exe' if os.name=='nt' else 'iverilog'
        vv=runtime/'vvp.exe' if os.name=='nt' else 'vvp'
        run('compile-reference',[compiler,'-std=c++17','-O2','-Wall','-Wextra','-I',out,ROOT/'tests/c64_vice/reu_harness.cpp','-o','reference.exe'])
        run('reference-timing',[out/'reference.exe','timing',out/'native-timing.csv'])
        for bits in (19,24):
            v=vectors(bits); fixture=out/f'vectors{bits}.txt'; v.save(fixture)
            actual=out/f'rtl{bits}.trace'; expected=out/f'vice{bits}.trace'
            for p in (actual,expected):p.unlink(missing_ok=True)
            run(f'compile-rtl-{bits}',[iv,'-g2012','-s','tb_c64_reu','-P',f'tb_c64_reu.ADDRESS_BITS={bits}','-o',f'rtl{bits}.vvp',ROOT/'hdl/core/c64/multitini_c64_reu.sv',ROOT/'hdl/sim/tb_c64_reu.sv'])
            run(f'reference-{bits}',[out/'reference.exe',bits,fixture,expected,out/f'vice{bits}.clock'])
            output=run(f'rtl-{bits}',[vv,f'rtl{bits}.vvp','+STIMULI='+fixture.as_posix(),'+TRACE='+actual.as_posix()],600)
            if 'PASS: C64 REU' not in output: raise RuntimeError('Missing RTL completion')
            a,b=observable(actual),observable(expected); compare(a,b)
            result['checks'].append({'address_bits':bits,'status':'PASS','case_count':len(v.cases),'cases':v.cases,'observable_events':len(a),'fixture_sha256':sha(fixture),'rtl_sha256':sha(actual),'vice_sha256':sha(expected)})
            print(f'PASS: VICE REU {bits}-bit {len(v.cases)} cases, {len(a)} events',flush=True)
        # Negative control uses an already proven matching pair.
        bad=a.copy(); bad[1]+=' CORRUPTED'
        try: compare(bad,b)
        except RuntimeError: result['negative_control_rejected']=True
        else: raise RuntimeError('Corruption negative control was accepted')
        run('compile-transport',[iv,'-g2012','-s','tb_c64_reu_transport','-o','transport.vvp',ROOT/'hdl/core/c64/multitini_c64_reu.sv',ROOT/'hdl/sim/tb_c64_reu_transport.sv'])
        output=run('transport',[vv,'transport.vvp'])
        markers=re.findall(r'^PASS: C64 REU transport.*$',output,re.M)
        if len(markers)!=1: raise RuntimeError('Missing transport completion')
        result['checks'].append({'name':'reset-disable-fault-transport','status':'PASS','summary':markers[0]})
        if hashes()!=initial: raise RuntimeError('Sources changed during validation')
        result.update(status='PASS',sources_unchanged=True)
    except Exception as error:
        result['error']=str(error); raise
    finally:
        result['evidence_sha256']={p.name:sha(p) for p in out.iterdir() if p.is_file() and p.name!='validation.json' and p.suffix in ('.trace','.clock','.txt','.log','.inc','.json')}
        receipt.write_text(json.dumps(result,indent=2)+'\n')
    return 0


if __name__=='__main__':
    try: raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        print('ERROR: '+str(error),file=sys.stderr); raise SystemExit(1)
