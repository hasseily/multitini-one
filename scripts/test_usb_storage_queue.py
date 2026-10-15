#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute production USB0 queue/reset guards with an interrupt injected in SD I/O."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('UsbStorageIrqSave', 'UsbStorageIrqRestore', 'UsbProcessQueuedStorageReq',
         'UsbResetStorageQueue', 'UsbResetLocalStorageState')
INPUTS = ('firmware/platform/xilinx/usb_storage_service.c',
          'scripts/test_usb_storage_queue.py', 'tests/host/test_usb_storage_queue.c')


def function(source, name):
    match = re.search(r'^static\s+[A-Za-z_][\w *]*?\b' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if match is None:
        raise ValueError('Missing production function: ' + name)
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='C:/msys64/ucrt64/bin/gcc.exe' if os.name == 'nt' else 'cc')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/usb-storage-queue')
    parser.add_argument('--negative-before', type=Path,
                        help='Optional captured old queue function; must fail the same regression')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    before = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in INPUTS}
    source = (ROOT / INPUTS[0]).read_text()
    functions = {name: function(source, name) for name in NAMES}
    env = os.environ.copy()
    if os.name == 'nt':
        env['PATH'] = 'C:/msys64/ucrt64/bin' + os.pathsep + env.get('PATH', '')
    log, commands = [], []

    def run_case(directory, selected):
        directory.mkdir(exist_ok=True)
        (directory / 'usb_storage_queue.inc').write_text('\n\n'.join(selected[name] for name in NAMES))
        binary = directory / ('test.exe' if os.name == 'nt' else 'test')
        command = [args.cc, '-std=c11', '-Wall', '-Wextra', '-Werror', '-I' + str(directory),
                   str(ROOT / INPUTS[2]), '-o', str(binary)]
        commands.append(command)
        compiled = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True)
        log.append('$ ' + ' '.join(command) + '\n' + compiled.stdout + compiled.stderr)
        if compiled.returncode:
            raise ValueError('Queue regression compile failed: ' + compiled.stderr)
        result = subprocess.run([str(binary)], cwd=ROOT, env=env, capture_output=True, text=True)
        log.append('$ ' + str(binary) + '\n' + result.stdout + result.stderr)
        return result

    try:
        result = run_case(out, functions)
        if result.returncode:
            raise ValueError('Production queue regression failed: ' + result.stdout + result.stderr)
        negative = None
        if args.negative_before:
            selected = dict(functions)
            selected['UsbProcessQueuedStorageReq'] = args.negative_before.read_text()
            failed = run_case(out / 'negative-before', selected)
            if failed.returncode == 0:
                raise ValueError('Captured unguarded queue unexpectedly passed')
            negative = {'status': 'EXPECTED_FAILURE', 'exit_code': failed.returncode,
                        'captured_function_sha256': hashlib.sha256(args.negative_before.read_bytes()).hexdigest()}
        after = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in INPUTS}
        if before != after:
            raise ValueError('Queue test inputs changed while executing')
        record = {'status': 'PASS', 'files': before, 'commands': commands,
                  'negative_before': negative, 'result': result.stdout.strip(),
                  'scope': 'Actual USB0 GIC/IER/queue/reset functions; injected reset during blocking class handling; USB1 stays enabled.'}
        (out / 'validation.json').write_text(json.dumps(record, indent=2) + '\n')
        print(result.stdout.strip())
        if negative:
            print('PASS captured unguarded queue fails the same regression')
    finally:
        (out / 'run.log').write_text('\n'.join(log))


if __name__ == '__main__':
    main()
