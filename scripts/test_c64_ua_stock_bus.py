#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture or compare the immutable stock IO2/REU bus reference for UA/Turbo."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from test_c64_reu_machine import synthetic_memory, instrument, instrument_wrapper

ROOT = Path(__file__).resolve().parents[1]
GOLDEN = ROOT / 'tests/c64_ua/stock_bus.csv'


def program() -> bytes:
    source = synthetic_memory()
    original = source[:0x2000]
    # The reference machine deliberately leaves sprite internal state as its
    # real power-on state. Keep all its sprite-steal scenarios, but bracket
    # the 16 floating-bus aperture reads with sprites disabled: the captured
    # open-bus byte must be defined independently of uninitialized sprite
    # latches. The display and actual badline arbitration remain active.
    begin_float = bytes.fromhex('A9 00 8D 02 DF A9 DF 8D 03 DF')
    end_float = bytes.fromhex('AD 00 DF 8D 03 21')
    assert original.count(begin_float) == original.count(end_float) == 1
    original = original.replace(begin_float, bytes.fromhex('A9 00 8D 15 D0') + begin_float)
    original = original.replace(end_float, end_float + bytes.fromhex('A9 FF 8D 15 D0'))
    original = original[:0x2000] + source[0x2000:]
    # Color RAM has no hardware reset. Initialize every nibble through real
    # CPU writes so subsequent floating-bus samples are deterministic too.
    prefix = bytearray.fromhex('A9 0F A2 00 9D 00 D8 9D 00 D9 9D 00 DA 9D 00 DB E8 D0 F1')
    # All 256 IO2 addresses must retain their pre-UA mirrored register/open
    # register reads. Alter only the C64-address low byte; transfers set it.
    for address in range(0xdf00, 0xe000):
        prefix.extend((0xad, address & 255, address >> 8))
    for mirror in range(8):
        address = 0xdf02 + 32 * mirror
        prefix.extend((0xa9, 0x40 + mirror, 0x8d, address & 255, address >> 8,
                       0xad, 0x02, 0xdf))
    memory = bytearray(original)
    # Original initial SEI/CLD and 6510 port setup precede the probes.
    start = 0x80a
    tail = original[start:0x2000]
    memory[start:start + len(prefix)] = prefix
    memory[start + len(prefix):0x2000] = tail[:0x2000-start-len(prefix)]
    return bytes(memory)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT/'build/c64-ua-stock-golden')
    parser.add_argument('--vivado', type=Path, default=Path('C:/Xilinx/2025.2/Vivado'))
    parser.add_argument('--capture', action='store_true', help='Create the reference once; refuses overwrite')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if not build.is_relative_to((ROOT/'build').resolve()) or build == (ROOT/'build').resolve():
        parser.error('Use a dedicated build/ child')
    if args.capture and GOLDEN.exists():
        parser.error('The stock reference already exists; capture cannot replace it')
    build.mkdir(parents=True, exist_ok=True)
    production = ROOT/'hdl/core/c64/ports/fpga64_sid_iec.vhd'
    wrapper = ROOT/'hdl/core/c64/ports/cpu_6510.vhd'
    observed = build/'fpga64_observed.vhd'
    observed_wrapper = build/'cpu_6510_observed.vhd'
    observed.write_text(instrument(production.read_text()), encoding='utf-8')
    observed_wrapper.write_text(instrument_wrapper(wrapper.read_text()), encoding='utf-8')
    (build/'machine_memory.hex').write_text(''.join(f'{value:02x}\n' for value in program()))
    vhdl = []
    section = ''
    for raw in (ROOT/'hdl/hdl_sources.txt').read_text().splitlines():
        line = raw.strip()
        if line.startswith('['): section = line
        elif section == '[c64]' and line and not line.startswith('#'):
            path = (ROOT/'hdl'/line.split('|')[0].strip()).resolve()
            if path.suffix == '.vhd': vhdl.append(path)
    sv = [ROOT/path for path in (
        'third_party/c64/upstream/rtl/mos6526.v',
        'hdl/core/c64/ports/sid_dac.sv', 'hdl/core/c64/ports/sid_filter.sv',
        'third_party/c64/upstream/rtl/sid/sid_envelope.sv',
        'hdl/core/c64/ports/sid_tables.sv', 'third_party/c64/upstream/rtl/sid/sid_voice.sv',
        'hdl/core/c64/ports/sid_top.sv', 'hdl/core/c64/multitini_c64_local_ram.sv',
        'hdl/core/c64/multitini_c64_reu.sv', 'hdl/core/c64/multitini_c64_memory_arbiter.sv',
        'hdl/sim/tb_c64_reu_machine.sv', 'hdl/sim/tb_c64_ua_stock_bus.sv')]
    inputs = [Path(__file__).resolve(), ROOT/'scripts/test_c64_reu_machine.py',
              ROOT/'hdl/hdl_sources.txt', *vhdl, *sv]
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    hashes = lambda: {p.relative_to(ROOT).as_posix(): sha(p) for p in inputs}
    before = hashes()
    receipt = {'status':'FAIL', 'source_sha256':before,
               'limits':'Original synthetic program and production CPU/VIC/CIA/SID/REU RTL. '
                        'Observation-only FPGA64/6510 ports; abstract delayed expansion memory. '
                        'Not an adapter, cache/physical DDR, software corpus or board proof.'}
    env = {k:v for k,v in os.environ.items() if k.upper() != 'PATH'}
    env['PATH'] = 'C:/msys64/ucrt64/bin' + os.pathsep + os.environ.get('PATH', os.environ.get('Path', ''))
    def run(label: str, tool: str, *arguments: object) -> str:
        executable = args.vivado/'bin'/(tool + ('.bat' if os.name == 'nt' else ''))
        result = subprocess.run([sys.executable, str(ROOT/'scripts/run_native_tool.py'), '--',
                                 str(executable), *map(str,arguments)], cwd=build, env=env,
                                capture_output=True, text=True, errors='replace', timeout=600,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        output = result.stdout + result.stderr
        (build/(label+'.log')).write_text(output)
        if result.returncode or re.search(r'\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b', output,re.I):
            raise RuntimeError(label+' failed: '+output[-12000:])
        return output
    try:
        run('imported','xvlog','--nolog','--sv','--relax',*[p for p in sv if 'third_party' in p.parts])
        run('local','xvlog','--nolog','--sv',*[p for p in sv if 'third_party' not in p.parts])
        run('vhdl','xvhdl','--nolog','--2008',*[observed if p==production else observed_wrapper if p==wrapper else p for p in vhdl])
        run('elaborate','xelab','--nolog','--relax','--timescale','1ns/1ps','--snapshot','ua_stock','tb_c64_ua_stock_bus')
        output=run('simulate','xsim','--nolog','ua_stock','--runall')
        if 'PASS: C64 production REU machine' not in output: raise RuntimeError('Machine checks did not finish')
        trace=(build/'stock_bus.csv').read_text()
        if re.search(r'[xz]',trace.lower()): raise RuntimeError('Unknown value in golden trace')
        rows=list(csv.DictReader(trace.splitlines()))
        for standard in ('0','1'):
            subset=[r for r in rows if r['ntsc']==standard]
            read_addrs={int(r['address'],16) for r in subset if r['event']=='CPU_R'}
            if not set(range(0xdf00,0xe000)) <= read_addrs: raise RuntimeError('Missing IO2 mirror coverage')
            if sum(r['event'].startswith('DMA_') and r['event']!='DMA_OWN' for r in subset)!=4133:
                raise RuntimeError('Incorrect DMA acknowledgement coverage')
        if hashes()!=before: raise RuntimeError('Sources changed during stock capture')
        if args.capture:
            GOLDEN.parent.mkdir(parents=True, exist_ok=True)
            GOLDEN.write_text(trace,encoding='utf-8',newline='\n')
            revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,env=env,text=True).strip()
            (GOLDEN.parent/'stock_bus_manifest.json').write_text(json.dumps({
                'schema':1,'captured_from_commit':revision,'trace_sha256':sha(GOLDEN),
                'source_sha256':before,'rows':len(rows),'limits':receipt['limits']},indent=2)+'\n')
        elif trace!=GOLDEN.read_text():
            raise RuntimeError('Stock IO2/REU trace changed from the pre-UA/pre-turbo reference')
        receipt.update(status='PASS',rows=len(rows),trace_sha256=sha(build/'stock_bus.csv'),sources_unchanged=True)
        print(f'PASS: C64 UA stock bus golden rows={len(rows)} PAL/NTSC all IO2 mirrors, FF00, DMA and IRQ')
    except Exception as error:
        receipt['error']=str(error)
        raise
    finally:
        (build/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    return 0


if __name__=='__main__':
    try: raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as error:
        print('ERROR: '+str(error),file=sys.stderr)
        raise SystemExit(1)
