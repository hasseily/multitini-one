#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise exact production floppy CPU gates, ownership and cooldown logic.

The fixture supplies CPU/cache/native boundary responses; it does not emulate
the gate policy. A production flux_drive proves the virtual disk stays parked.
No Apple ROM, user media, FPGA build or hardware connection is involved.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
ADAPTER = 'hdl/core/apple_iigs/multitini_apple_iigs_adapter.sv'
MACHINE = 'hdl/core/apple_iigs/ports/iigs.sv'
DRIVE = 'hdl/core/apple_iigs/ports/flux_drive.sv'
BENCH = 'hdl/sim/tb_apple_iigs_fast_floppy_overlap.sv'
INCLUDE = 'apple_iigs_fast_floppy_overlap_production.svh'


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def extract(adapter, machine):
    def block(source, start, end):
        require(source.count(start) == source.count(end) == 1, 'Production extraction anchors changed')
        a = source.index(start)
        return source[a:source.index(end, a)]
    gates = block(adapter, '    wire floppy_clock_active=', '    wire [7:0] cpu_slowram_data;')
    retire = block(adapter, '    reg native_pending,native_return_seen;', '    reg committed_bus;')
    clock = re.findall(r'^  wire fast_floppy_clock_active = .*?;\s*$', machine, re.M)
    require(len(clock) == 1, 'Machine captured-clock ownership anchor changed')
    machine_gates = block(machine, 'assign fast_floppy_active =', 'wire [3:0] fast_thresh =')
    # The machine's reset pin is adapter.machine_reset, whereas the adapter's
    # reset pin is the global shell reset. Preserve both actual contexts.
    return gates + '\n' + retire + '\n' + 'generate begin:machine_gate_context\nwire reset=machine_reset;\n' + clock[0] + '\n' + machine_gates + '\nend endgenerate\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--source-archive', type=Path, help='Explicit frozen HDL source TAR baseline')
    parser.add_argument('--expect-prechange', action='store_true')
    parser.add_argument('--negative-controls', action='store_true')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    require(build.is_relative_to(ROOT / 'build') and not build.exists(), 'Select a fresh repository build directory')
    require(not (args.expect_prechange and args.negative_controls), 'Negative mutations qualify corrected production only')
    inputs = {n: (ROOT / n).read_bytes() for n in (ADAPTER, MACHINE, DRIVE, BENCH, Path(__file__).relative_to(ROOT).as_posix())}
    baseline = None
    if args.source_archive:
        archive = args.source_archive.resolve()
        require(archive.is_relative_to(ROOT / 'build'), 'Baseline archive must stay in repository build')
        baseline = {'path': archive.relative_to(ROOT).as_posix(), 'sha256': digest(archive.read_bytes())}
        with tarfile.open(archive, 'r:gz') as tar:
            for name in (ADAPTER, MACHINE, DRIVE):
                member = tar.getmember(name)
                require(member.isfile(), 'Baseline lacks production source ' + name)
                inputs[name] = tar.extractfile(member).read()
    hashes = {n: digest(data) for n, data in inputs.items()}
    production = extract(inputs[ADAPTER].decode(), inputs[MACHINE].decode())
    build.mkdir(parents=True)
    captured = build / 'captured'
    for name, data in inputs.items():
        target = captured / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    runtime = Path(os.environ.get('MULTITINI_UCRT_BIN', 'C:/msys64/ucrt64/bin'))
    env = {k: v for k, v in os.environ.items() if k.upper() != 'PATH'}
    env['PATH'] = str(runtime) + os.pathsep + str(runtime.parents[1] / 'usr/bin') + os.pathsep + os.environ.get('PATH', '')
    env['VERILATOR_ROOT'] = (runtime.parent / 'share/verilator').as_posix()
    commands = {}

    def run(command, label, *, failure=None):
        command = [str(p).replace('\\', '/') for p in command]
        commands[label] = command
        result = subprocess.run(command, cwd=build, env=env, capture_output=True, text=True)
        output = result.stdout + result.stderr
        (build / (label + '.log')).write_text(output, encoding='utf-8')
        if failure is None:
            require(result.returncode == 0 and not re.search(r'%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed', output), label + ' failed: ' + output[-7000:])
        else:
            require(result.returncode != 0 and failure in output, label + ' did not fail intended assertion: ' + output[-7000:])
        return {'exit_code': result.returncode, 'log_sha256': digest(output.encode()),
                'results': [line for line in output.splitlines() if line.startswith('PASS:')]}

    def compile_case(text, name):
        directory = build / name
        directory.mkdir()
        (directory / INCLUDE).write_text(text, encoding='utf-8', newline='\n')
        top = 'tb_apple_iigs_fast_floppy_overlap'
        obj = directory / 'obj'
        run([runtime / 'verilator_bin.exe', '--cc', '--exe', '--main', '--timing', '-Wno-fatal', '--top-module', top,
             '-I' + str(directory), '--Mdir', obj, captured / DRIVE, captured / BENCH], name + '-generate')
        inc = runtime.parent / 'share/verilator/include'
        exe = obj / ('V' + top + '.exe')
        run([runtime / 'g++.exe', '-std=c++20', '-O1', '-pthread', '-DVL_TIME_CONTEXT', '-I' + str(inc),
             '-I' + str(inc / 'vltstd'), '-I' + str(obj), *sorted(obj.glob('*.cpp')),
             inc / 'verilated.cpp', inc / 'verilated_timing.cpp', inc / 'verilated_threads.cpp', '-o', exe], name + '-compile')
        return exe

    exe = compile_case(production, 'production')
    cases = {}
    for case in range(15):
        failure = None
        if args.expect_prechange and case in (0, 1, 4, 11, 12):
            failure = 'background CPU stalled during backend wait'
        elif args.expect_prechange and case == 9:
            failure = 'native IO response did not retire with virtual floppy parked'
        result = run([exe, '+CASE=' + str(case)], f'case-{case}', failure=failure)
        result['expected_prechange_failure'] = failure
        cases[str(case)] = result
    negatives = []
    if args.negative_controls:
        mutations = [
            ('cooldown_wait_freezes', r' \|\|\s*\(floppy_backend_wait && !floppy_cpu_stall\)', '', 0,
             'background CPU stalled during backend wait'),
            ('guard_wait_bypassed', r'floppy_backend_wait &&\s*\(cpu_floppy_guard \|\| cpu_floppy_access \|\| floppy_iwm_owned\)',
             'floppy_backend_wait && (cpu_floppy_access || floppy_iwm_owned)', 2,
             'protected floppy software delay advanced during backend wait'),
            ('virtual_wait_omitted', r'floppy_cpu_stall \|\| floppy_backend_wait \|\|', 'floppy_cpu_stall ||', 0,
             'virtual disk advanced during background work'),
        ]
        for name, old, new, case, assertion in mutations:
            require(len(re.findall(old, production)) == 1, 'Negative mutation anchor changed: ' + name)
            mutated = re.sub(old, lambda _: new, production, count=1)
            negative_exe = compile_case(mutated, 'negative-' + name)
            result = run([negative_exe, '+CASE=' + str(case)], 'negative-' + name, failure=assertion)
            negatives.append({'mutation': name, 'expected_failure': assertion, **result})
    require(all(digest((captured / n).read_bytes()) == h for n, h in hashes.items()), 'Captured overlap source changed')
    if baseline is None:
        require(all(digest((ROOT / n).read_bytes()) == h for n, h in hashes.items()), 'Production source changed during overlap test')
    record = {'status': 'EXPECTED_PRECHANGE_FAIL' if args.expect_prechange else 'PASS', 'files': hashes,
              'source_archive': baseline, 'exact_production_blocks': True, 'production_block_sha256': digest(production.encode()),
              'commands': commands, 'cases': cases, 'negative_controls': negatives,
              'limits': ['CPU/cache/native boundaries are fixtures; CPU retire gates, ownership and cooldown are exact extracted production text.',
                         'Actual production flux_drive timer and position are checked; only spindle/ready mechanics are forced.',
                         'No full65816 instruction stream, file service, ROM boot, routed timing or physical hardware acceptance is implied.']}
    (build / 'validation.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': record['status'], 'cases': len(cases), 'negative_controls': len(negatives)}, indent=2))


if __name__ == '__main__':
    main()
