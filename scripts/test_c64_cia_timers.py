#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay verified Lorenz 2.15 CIA vectors through the production CIA and CPU.

Requires explicitly supplied Disk2.d64 and Disk3.d64, and py65==1.2.0.
Media, extracted PRGs and generated memory files stay in the ignored build
directory. Wolfgang Lorenz's original suite documentation declares it public
domain: https://github.com/floooh/chips-test/blob/
3785836e76c43922f78a50e1f8adfed259ab9672/tests/testsuite-2.15/Test_Suite_2_15.txt
"""
from pathlib import Path
from importlib.metadata import PackageNotFoundError, version
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PY65_VERSION = '1.2.0'
PROGRAMS = {
    'cia1ta': (2, '7dd8c04afc6734f93659f043ed13f1895359e2c4ad33258a9cfc5923da3a5bdd', 0x96d, 0xeaf),
    'cia1tb': (2, '5676dd8c596b8436b6510d31615a1f2e8a0270d0d7976940eeaebed53345b827', 0x96d, 0xeaf),
    'cia2ta': (3, 'cff2cd07316877af82506059e8e88cc69292bc263f2d11b7534bc2ecc3de8297', 0x985, 0x11bf),
    'cia2tb': (3, '53fa1efd28f81ad0b91e958eeddcdbdbae9ed7a3586b90c4771de44b240c74ae', 0x985, 0x11bf),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def extract_disk(data):
    """Read a standard 35-track D64; reject invalid or cyclic sector chains."""
    if len(data) != 174848:
        raise ValueError('expected a standard 174848-byte D64')
    counts = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5
    starts = [0]
    for count in counts:
        starts.append(starts[-1] + count * 256)

    def sector(track, number):
        if not (1 <= track <= 35 and 0 <= number < counts[track - 1]):
            raise ValueError('invalid D64 sector')
        offset = starts[track - 1] + number * 256
        return data[offset:offset + 256]

    def chain(track, number):
        seen = set()
        result = bytearray()
        while track:
            if (track, number) in seen:
                raise ValueError('cyclic D64 file chain')
            seen.add((track, number))
            block = sector(track, number)
            if not block[0] and block[1] < 1:
                raise ValueError('invalid D64 terminal byte count')
            result.extend(block[2:] if block[0] else block[2:block[1] + 1])
            track, number = block[0], block[1]
        return bytes(result)

    result, seen = {}, set()
    track, number = 18, 1
    while track:
        if (track, number) in seen:
            raise ValueError('cyclic D64 directory chain')
        seen.add((track, number))
        block = sector(track, number)
        for entry in range(8):
            item = block[entry * 32:entry * 32 + 32]
            if item[2] != 0x82:
                continue
            name = item[5:21].rstrip(b'\xa0').decode('ascii', errors='replace').lower()
            if name in PROGRAMS:
                if name in result:
                    raise ValueError('duplicate test program: ' + name)
                result[name] = chain(item[3], item[4])
        track, number = block[0], block[1]
    return result


def oracle(name, binary):
    from py65.devices.mpu6502 import MPU
    _, expected_hash, dispatch, compare = PROGRAMS[name]
    if sha(binary) != expected_hash or binary[:2] != b'\x01\x08':
        raise ValueError('unsupported executable revision: ' + name)
    memory = [0] * 65536
    memory[0x801:0x801 + len(binary) - 2] = binary[2:]
    if bytes(memory[dispatch:dispatch + 3]) != bytes.fromhex('ade308'):
        raise ValueError('unsupported oracle dispatcher')
    if bytes(memory[compare:compare + 7]) != bytes.fromhex('adf108cdf408d0'):
        raise ValueError('unsupported oracle comparison')
    cpu = MPU(memory=memory)
    rows, max_steps = [], 0
    for initial in range(30, -1, -1):
        for replacement in range(20, -1, -1):
            for initial_index, initial_cr in enumerate((0x10, 0x11, 0x18, 0x19)):
                for second_index, second_cr in enumerate((0, 1, 8, 9, 0x10, 0x11, 0x18, 0x19)):
                    memory[0x8e3:0x8e5] = [initial_index, second_index]
                    memory[0x8ed:0x8f7] = [initial, initial_cr, replacement, second_cr, 0, 0, 0, 0xcc, 0xcc, 0xcc]
                    cpu.pc, cpu.p, cpu.sp = dispatch, 0x24, 0xff
                    cpu.a = cpu.x = cpu.y = 0
                    for steps in range(600):
                        if cpu.pc == compare:
                            break
                        if not dispatch <= cpu.pc < compare:
                            raise ValueError('oracle escaped its verified code range')
                        cpu.step()
                    else:
                        raise ValueError('oracle instruction bound exceeded')
                    expected = memory[0x8f4:0x8f7]
                    if 0xcc in expected:
                        raise ValueError('oracle did not write every result')
                    rows.append(bytes([initial, replacement, initial_cr, second_cr, *expected]).hex())
                    max_steps = max(max_steps, steps)
    if len(rows) != 20832 or len({row[:8] for row in rows}) != 20832:
        raise ValueError('incomplete oracle matrix')
    return rows, max_steps


def program_memory(binary, timer):
    # Original minimal boot fixture; the test's own critical instructions and
    # RTI handler are loaded from the explicitly selected, verified PRG.
    code = bytearray.fromhex('78 a900 8d0edd 8d05dd a97f 8d0ddd a981 8d0ddd a9f7 8d1803 a908 8d1903 a911 a000 4c4809')
    code[4], code[7], code[15] = (15, 7, 0x82) if timer else (14, 5, 0x81)
    assert code[0x1e] == 0x11 and code[0x20] == 0
    return '\n'.join(['@0801', *[f'{value:02x}' for value in binary[2:]], '@0200', *[f'{value:02x}' for value in code]]) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--disk2', type=Path, required=True)
    parser.add_argument('--disk3', type=Path, required=True)
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build') or out == ROOT / 'build':
        parser.error('fresh dedicated build/ child required')
    out.mkdir(parents=True)
    receipt = {'status': 'FAIL', 'cases': [], 'negative_controls': [], 'source_sha256': {}, 'programs': {}}
    try:
        try:
            installed = version('py65')
        except PackageNotFoundError as error:
            raise ValueError('requires py65==1.2.0; install with python -m pip install py65==1.2.0') from error
        if installed != PY65_VERSION:
            raise ValueError('requires py65==1.2.0; install with python -m pip install py65==1.2.0')
        receipt['py65_version'] = installed
        inputs, binaries = {}, {}
        for disk, path in ((2, args.disk2), (3, args.disk3)):
            data = path.read_bytes()
            inputs[str(path.resolve())] = sha(data)
            extracted = extract_disk(data)
            for name, (needed_disk, _, _, _) in PROGRAMS.items():
                if disk == needed_disk:
                    if name not in extracted:
                        raise ValueError('missing executable: ' + name)
                    binaries[name] = extracted[name]
        receipt['input_sha256'] = inputs
        vectors = {}
        for name, binary in binaries.items():
            rows, maximum = oracle(name, binary)
            vectors[name] = rows
            folder = out / 'generated'
            folder.mkdir(exist_ok=True)
            (folder / (name + '.prg')).write_bytes(binary)
            encoded = ('\n'.join(rows) + '\n').encode()
            (folder / (name + '.hex')).write_bytes(encoded)
            receipt['programs'][name] = {'sha256': sha(binary), 'rows': len(rows), 'vectors_sha256': sha(encoded), 'max_oracle_instructions': maximum}
        vhdl = [ROOT / 'third_party/c64/upstream/rtl/t65' / name for name in ('T65_Pack.vhd', 'T65_ALU.vhd', 'T65_MCode.vhd')]
        vhdl += [ROOT / name for name in ('hdl/core/c64/ports/T65.vhd', 'hdl/core/c64/multitini_c64_6510_port.vhd', 'hdl/core/c64/ports/cpu_6510.vhd')]
        bench = ROOT / 'hdl/sim/tb_c64_cia_timers.sv'
        production = ROOT / 'hdl/core/c64/ports/mos6526.v'
        baseline = ROOT / 'third_party/c64/upstream/rtl/mos6526.v'
        sources = [Path(__file__).resolve(), ROOT / 'scripts/run_native_tool.py', bench, production, baseline, *vhdl]
        for source in sources:
            key = source.relative_to(ROOT).as_posix()
            receipt['source_sha256'][key] = sha(source.read_bytes())
            target = out / 'sources' / key
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
        snap = lambda source: out / 'sources' / source.relative_to(ROOT)
        vivado = Path(os.environ.get('XILINX_VIVADO', 'C:/Xilinx/2025.2/Vivado')) / 'bin'
        suffix = '.bat' if os.name == 'nt' else ''

        def run(folder, label, argv):
            command = [sys.executable, str(ROOT / 'scripts/run_native_tool.py'), '--cwd', str(folder), '--', str(vivado / (argv[0] + suffix)), *map(str, argv[1:])]
            result = subprocess.run(command, capture_output=True, text=True, errors='replace', timeout=900)
            log = result.stdout + result.stderr
            (folder / (label + '.log')).write_text(log, encoding='utf-8')
            if label != 'simulate' and (result.returncode or re.search(r'ERROR:|FATAL:|Fatal:', log)):
                raise RuntimeError(label + ' failed: ' + log[-3000:])
            return log, result.returncode

        def case(label, timer, mode, rows, source, no_nmi=0, negative=False):
            folder = out / label
            folder.mkdir()
            (folder / 'vectors.hex').write_text('\n'.join(rows) + '\n')
            if mode:
                name = 'cia2tb' if timer else 'cia2ta'
                (folder / 'program.hex').write_text(program_memory(binaries[name], timer))
            wrapper = folder / 'wrapper.sv'
            directed = int(not negative and mode == 0 and timer == 0)
            wrapper.write_text(f'module wrapper; tb_c64_cia_timers #(.CPU_MODE({mode}),.TIMER({timer}),.ROWS({len(rows)}),.NO_NMI({no_nmi}),.DIRECTED({directed})) test(); endmodule\n')
            run(folder, 'verilog', ['xvlog', '--nolog', '--sv', '--relax', snap(source), snap(bench), wrapper])
            if mode:
                run(folder, 'vhdl', ['xvhdl', '--nolog', '--2008', *map(snap, vhdl)])
            run(folder, 'elaborate', ['xelab', '--nolog', '--relax', '--mt', 'off', '--timescale', '1ns/1ps', '-s', 'cia_timers', 'wrapper'])
            log, code = run(folder, 'simulate', ['xsim', '--nolog', 'cia_timers', '--runall'])
            matches = re.findall(r'CIA_RESULT mode=(\d+) timer=(\d+) rows=(\d+) errors=(\d+) nmi=(\d+)', log)
            if len(matches) != 1 or tuple(map(int, matches[0][:3])) != (mode, timer, len(rows)):
                raise RuntimeError(label + ' missing complete result: ' + log[-3000:])
            errors, nmis = map(int, matches[0][3:])
            bad = code or re.search(r'ERROR:|FATAL:|Fatal:|CIA_CPU_TIMEOUT|CIA_MISMATCH', log)
            if negative:
                if not errors or 'CIA_VECTOR_FAILURE' not in log or 'CIA_MISMATCH' not in log:
                    raise RuntimeError(label + ' negative control did not reject')
                if no_nmi and (nmis != 0 or errors != len(rows)):
                    raise RuntimeError('missing NMI control did not expose every corrected row')
                if not no_nmi and 'init=06141100 got=0e0000 expected=10' not in log:
                    raise RuntimeError('baseline did not reproduce the reported row')
                if not no_nmi and errors != 577:
                    raise RuntimeError('baseline mismatch inventory changed')
            elif bad or errors or log.count('CIA_VECTORS_PASS') != 1 or (mode == 2 and nmis == 0):
                raise RuntimeError(label + ' failed: ' + log[-5000:])
            if directed and log.count('CIA_DIRECTED_RESULT checks=10') != 1:
                raise RuntimeError('missing cascade/reset directed cases')
            item = {'label': label, 'mode': mode, 'timer': timer, 'rows': len(rows), 'errors': errors, 'nmis': nmis, 'source_sha256': sha(source.read_bytes())}
            receipt['negative_controls' if negative else 'cases'].append(item)
            print(label + ': ' + str(item), flush=True)

        for timer in (0, 1):
            case('leaf-' + str(timer), timer, 0, vectors['cia1tb' if timer else 'cia1ta'], production)
        for timer in (0, 1):
            case('cpu-nmi-' + str(timer), timer, 2, vectors['cia2tb' if timer else 'cia2ta'], production)
        for timer in (0, 1):
            case('baseline-' + str(timer), timer, 0, vectors['cia1tb' if timer else 'cia1ta'], baseline, negative=True)
        corrected = [a for a, b in zip(vectors['cia2ta'], vectors['cia1ta']) if a != b]
        if len(corrected) != 778:
            raise ValueError('unexpected CIA2 NMI correction inventory')
        case('missing-nmi', 0, 2, corrected, production, no_nmi=1, negative=True)
        receipt['sources_unchanged'] = all(sha((ROOT / key).read_bytes()) == digest for key, digest in receipt['source_sha256'].items())
        receipt['inputs_unchanged'] = all(sha(Path(key).read_bytes()) == digest for key, digest in inputs.items())
        if not receipt['sources_unchanged'] or not receipt['inputs_unchanged']:
            raise ValueError('source or input changed during validation')
        receipt['limits'] = ['Direct CIA native register protocol and production cpu_6510/T65 NMI timing with synthetic memory; not the complete adapter/island route.', 'Exact verified test code and synthetic SEI/JMP($0318) trampoline; no system ROM image distributed.', 'No board or routed timing acceptance claim.']
        receipt['status'] = 'PASS'
    except Exception as error:
        receipt['error'] = str(error)
        print(str(error), flush=True)
    receipt['artifact_sha256'] = {
        path.relative_to(out).as_posix(): sha(path.read_bytes())
        for path in out.rglob('*')
        if path.is_file() and path.suffix in ('.log', '.sv', '.hex', '.prg')
        and not {'sources', 'xsim.dir', '.Xil'}.intersection(path.relative_to(out).parts)
    }
    (out / 'validation.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt['status'] != 'PASS'


if __name__ == '__main__':
    sys.exit(main())
