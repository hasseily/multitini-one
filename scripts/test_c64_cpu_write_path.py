#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prove the T65 store-port helper at real island falling-edge memory writes."""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import os
import re
import subprocess
import sys

import test_c64_machine_schedule as machine
import test_c64_turbo_ordering as ordering

ROOT = Path(__file__).resolve().parents[1]


def fixture():
    memory = bytearray(machine.synthetic_memory())
    original_end = memory.index(bytes.fromhex('A9 A5 8D 05 21'), 0x800)
    body = bytearray()
    # Imported sprite counters intentionally have no reset. Run genuine sprite
    # DMA while both lanes remain exact, then signal the bench before $0800.
    warmup = bytearray.fromhex('78 D8 A9 2F 85 00 A9 37 85 01')
    for address in range(0xd000, 0xd02f):
        warmup += bytes([0xa9, 0, 0x8d, address & 255, address >> 8])
    warmup += bytes.fromhex('A9 FF 8D 02 DD A9 03 8D 00 DD A9 14 8D 18 D0 A9 08 8D 16 D0 '
                           'A9 FF 8D 15 D0 AD 11 D0 10 FB AD 11 D0 30 FB AD 12 D0 F0 FB '
                           'A9 00 8D 15 D0 AD 1E D0 AD 1F D0 A9 A7 8D 08 21 4C 00 08')
    memory[0x4000:0x4000 + len(warmup)] = warmup
    body += memory[0x800:original_end]
    # Finish the real native reset window before exercising the fast port stores.
    body += bytes([0xea]) * 256
    cases = []
    restore = bytes.fromhex('A9 2F 85 00 A9 37 85 01 A2 FF 9A')

    def add(opcode, address, width, label, indirect=False):
        body.extend(bytes.fromhex('A9 55 A2 01 A0 01'))
        if indirect:
            # ($40,X) uses X=0; ($40),Y uses Y=1. Setup is itself real stores.
            body.extend(bytes([0xa9, address & 255, 0x85, 0x40,
                               0xa9, address >> 8, 0x85, 0x41,
                               0xa9, 0x55, 0xa2, 0]))
            operand = 0x40
        else:
            operand = address
        body.append(opcode)
        body.append(operand & 255)
        if width == 2:
            body.append(operand >> 8)
        body.extend(restore)
        cases.append({'opcode': f'{opcode:02x}', 'operand': f'{address:04x}', 'kind': label})

    for opcode in (0x85, 0x84, 0x86, 0x87):
        for address in (0, 1, 2): add(opcode, address, 1, 'zero-page-store')
    for opcode in (0x8d, 0x8c, 0x8e, 0x8f):
        for address in (0, 1, 2, 0x100): add(opcode, address, 2, 'absolute-store')
    for opcode in (0x95, 0x94, 0x96, 0x97):
        for address in (0xff, 0): add(opcode, address, 1, 'zero-page-index-wrap')
    for opcode in (0x9d, 0x99, 0x9c, 0x9e, 0x9f, 0x9b):
        for address in (0xffff, 0): add(opcode, address, 2, 'absolute-index-wrap')
    for opcode in (0x81, 0x83):
        for address in (0, 1): add(opcode, address, 1, 'indexed-indirect-store', True)
    for opcode in (0x91, 0x93):
        for address in (0xffff, 0): add(opcode, address, 1, 'indirect-index-store', True)
    for opcode in (0x06, 0x26, 0x46, 0x66, 0xc6, 0xe6, 0x07, 0x27, 0x47, 0x67, 0xc7, 0xe7):
        for address in (0, 1): add(opcode, address, 1, 'zero-page-rmw')
    for opcode in (0x0e, 0x2e, 0x4e, 0x6e, 0xce, 0xee, 0x0f, 0x2f, 0x4f, 0x6f, 0xcf, 0xef):
        for address in (0, 1): add(opcode, address, 2, 'absolute-rmw')
    for opcode in (0x16, 0xf6, 0x17, 0xf7):
        for address in (0xff, 0): add(opcode, address, 1, 'zero-page-index-rmw')
    for opcode in (0x1e, 0xfe, 0x1f, 0xff):
        for address in (0xffff, 0): add(opcode, address, 2, 'absolute-index-rmw')
    for opcode in (0x03, 0x23, 0x43, 0x63, 0xc3, 0xe3):
        for address in (0, 1): add(opcode, address, 1, 'indexed-indirect-rmw', True)
    for opcode in (0x13, 0x33, 0x53, 0x73, 0xd3, 0xf3):
        for address in (0xffff, 0): add(opcode, address, 1, 'indirect-index-rmw', True)
    # Stack, JSR/RTS and BRK/RTI writes must never substitute the VIC byte.
    body += bytes.fromhex('A9 55 48 08 28 68 20 10 07 00 EA A9 07 8D 00 D8 A9 A5 8D 05 21')
    stop = 0x800 + len(body)
    body += bytes([0x4c, stop & 255, stop >> 8])
    assert stop < 0xa000
    memory[0x800:0x800 + len(body)] = body
    memory[0x700:0x705] = bytes.fromhex('48 08 28 68 40')
    memory[0x710] = 0x60
    return memory, cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/c64-cpu-write-path')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out == ROOT / 'build' or not out.is_relative_to(ROOT / 'build') or out.exists():
        raise ValueError('Require a fresh dedicated ignored directory')
    out.mkdir(parents=True)
    cp = ROOT / 'hdl/core/c64'
    (out / 'c64_turbo_exact_lane.sv').write_text(ordering.make_lane())
    core = (cp / 'ports/fpga64_sid_iec.vhd').read_text()
    observed = machine.instrument(core).replace('ext_cpu_di <= cpuDi;',
        'ext_cpu_di <= cpuDi;\nexternal_observation: if EXTERNAL_CPU generate observedCpuRead <= cpuDi; end generate;')
    (out / 'fpga64_sid_iec_observed.vhd').write_text(observed)
    (out / 'cpu_6510_observed.vhd').write_text(machine.instrument_wrapper((cp / 'ports/cpu_6510.vhd').read_text()))
    island = (cp / 'multitini_c64_cpu_island.sv').read_text()
    declaration = '    output wire [7:0] observe_cpu_di,\n'
    connection = '        .debug_read(observe_cpu_di),\n'
    instrumented = island.replace('    output wire nmi_ack,', declaration + '    output wire nmi_ack,', 1).replace(
        '    cpu_6510 cpu_i (', '    cpu_6510 cpu_i (\n' + connection, 1)
    assert instrumented.replace(declaration, '', 1).replace('\n' + connection, '', 1) == island
    (out / 'island_observed.sv').write_text(instrumented)
    memory, directed = fixture()
    (out / 'machine_memory.hex').write_text('\n'.join(f'{b:02x}' for b in memory) + '\n')
    (out / 'directed_cases.json').write_text(json.dumps(directed, indent=2) + '\n')
    vhdl, section = [], ''
    originals = []
    for raw in (ROOT / 'hdl/hdl_sources.txt').read_text().splitlines():
        line = raw.strip()
        if line.startswith('['): section = line
        elif section == '[c64]' and line and not line.startswith('#'):
            path = (ROOT / 'hdl' / line.split('|')[0].strip()).resolve()
            if path.suffix == '.vhd':
                originals.append(path)
                if path.name == 'fpga64_sid_iec.vhd': path = out / 'fpga64_sid_iec_observed.vhd'
                elif path.name == 'cpu_6510.vhd': path = out / 'cpu_6510_observed.vhd'
                vhdl.append(path)
    sv = [ROOT / p for p in ('third_party/c64/upstream/rtl/mos6526.v',
        'hdl/core/c64/ports/sid_dac.sv', 'hdl/core/c64/ports/sid_filter.sv',
        'third_party/c64/upstream/rtl/sid/sid_envelope.sv', 'hdl/core/c64/ports/sid_tables.sv',
        'hdl/core/c64/ports/sid_voice.sv', 'hdl/core/c64/ports/sid_top.sv',
        'hdl/core/c64/multitini_c64_sid_array.sv', 'hdl/core/c64/multitini_c64_local_ram.sv',
        'hdl/core/c64/multitini_c64_fio_port.sv', 'hdl/sim/tb_c64_cpu_write_path.sv')]
    sv += [cp / name for name in ('multitini_c64_cpu_proxy.sv', 'multitini_c64_island_governor.sv',
        'multitini_c64_island_map.sv', 'multitini_c64_island_memory.sv', 'multitini_c64_nmi_delivery.sv',
        'multitini_c64_turbo_regs.sv')]
    sv += [out / 'island_observed.sv', out / 'c64_turbo_exact_lane.sv']
    sources = [*originals, *sv, *vhdl, Path(__file__).resolve(), ROOT / 'hdl/hdl_sources.txt',
        ROOT / 'scripts/test_c64_machine_schedule.py', ROOT / 'scripts/test_c64_turbo_exact.py',
        ROOT / 'scripts/test_c64_turbo_ordering.py', ROOT / 'scripts/run_native_tool.py',
        cp / 'multitini_c64_adapter.sv', cp / 'multitini_c64_cpu_island.sv']
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    before = {p.relative_to(ROOT).as_posix(): digest(p) for p in sources}
    receipt = {'schema': 1, 'status': 'FAIL', 'source_sha256': before, 'directed_opcodes': directed}
    env = {k: v for k, v in os.environ.items() if k.upper() != 'PATH'}
    env['PATH'] = 'C:/msys64/ucrt64/bin;' + os.environ.get('PATH', os.environ.get('Path', ''))
    def run(name, command, negative=False):
        result = subprocess.run([sys.executable, str(ROOT / 'scripts/run_native_tool.py'), '--',
            *map(str, command)], cwd=out, env=env, capture_output=True, text=True, timeout=600)
        text = result.stdout + result.stderr
        (out / (name + '.log')).write_text(text)
        if not negative and (result.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:', text, re.I)):
            raise RuntimeError(name + '\n' + text[-8000:])
        return text
    v = Path('C:/Xilinx/2025.2/Vivado/bin')
    try:
        run('xvlog', [v / 'xvlog.bat', '--nolog', '--sv', '--relax', *sv])
        run('xvhdl', [v / 'xvhdl.bat', '--nolog', '--2008', *vhdl])
        run('xelab', [v / 'xelab.bat', '--nolog', '--relax', '--timescale', '1ns/1ps',
            '--snapshot', 'write_path_sim', 'tb_c64_cpu_write_path'])
        text = run('simulate', [v / 'xsim.bat', '--nolog', 'write_path_sim', '--runall'])
        summaries = [line for line in text.splitlines() if line.startswith('WRITE_PATH_SUMMARY ')]
        assert len(summaries) == 10 and 'PASS: actual CPU falling-edge write path' in text
        rows = list(csv.DictReader((out / 'ordering_trace.csv').open(newline='')))
        assert rows and all(row['native'] == row['accelerated'] for row in rows)
        required = {(str(n), str(b), str(i)) for n in range(2) for b in range(2)
                    for i in ((0, 1, 15) if b else (0, 15))}
        assert {(row['ntsc'], row['badline'], row['index']) for row in rows} == required
        # All scalar source-field combinations: ZPG ignores DBR (upper24 bits),
        # BA uses its full lower16 address, and SP always addresses01xx.
        selector_cases = 0
        for dbr in range(256):
            for ad in range(256):
                assert (ad >> 1 == 0) == ((((dbr << 16) | ad) & 0xffff) >> 1 == 0)
                selector_cases += 1
        for address in range(65536):
            assert ((address >> 8 == 0) and ((address & 255) >> 1 == 0)) == (address >> 1 == 0)
            selector_cases += 1
        for stack in range(256):
            assert ((0x100 | stack) >> 1 != 0)
            selector_cases += 1
        negative = run('negative', [v / 'xsim.bat', '--nolog', 'write_path_sim',
            '--testplusarg', 'mutate_write', '--runall'], True)
        assert 'WRITE_PATH_MUTATION_REJECT' in negative and 'PASS: actual CPU falling-edge write path' not in negative
        assert before == {p.relative_to(ROOT).as_posix(): digest(p) for p in sources}
        receipt.update(status='PASS', sources_unchanged=True, cases=10, accelerated_cases=8,
            exact_control_cases=2, trace_rows=len(rows),
            summaries=summaries, selector_model_cases=selector_cases,
            negative_control='A deliberately corrupted accepted falling-edge store byte was rejected.',
            limits='Real T65/6510, island/proxy, BRAM and native VIC/CIA/SID. Synthetic ROM/program; eight accelerated PAL/NTSC cases plus two exact controls. Index0 with BA policy correctly remains exact; index1 supplies additional sparse accelerated BA-policy coverage. Selector-byte enumeration is an algebraic model, not additional RTL states. No routed timing or physical-board claim.')
        print('\n'.join(summaries))
        print('PASS:', len(rows), 'equal native/accelerated accepted CPU cycles')
    except Exception as error:
        receipt['error'] = str(error)
        raise
    finally:
        receipt['artifacts'] = {p.name: digest(p) for p in out.iterdir() if p.is_file() and p.suffix in ('.log', '.sv', '.vhd', '.hex', '.csv', '.json') and p.name != 'validation.json'}
        (out / 'validation.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
