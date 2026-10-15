#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit user-supplied NES files through the real parser, boot stream and HDL header gate.

Reads cartridges without rewriting them. Reports contain hashes and 16-byte
headers, never game payloads. Generated binaries stay in the selected build dir.
"""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    return hashlib.sha256(data).hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--media', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/core-nes-m051/private-file-tests')
    parser.add_argument('--baseline-ref', help='Optional old revision whose parser must reject every supplied file')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    require(build.is_relative_to(ROOT / 'build'), 'Audit outputs must remain under build/')
    build.mkdir(parents=True, exist_ok=True)
    (build / 'validation.json').unlink(missing_ok=True)
    runtime = Path(os.environ.get('MULTITINI_UCRT_BIN', 'C:/msys64/ucrt64/bin'))
    dll_directory = os.add_dll_directory(str(runtime)) if os.name == 'nt' else None
    def tool(name):
        return runtime / (name + '.exe') if os.name == 'nt' else name
    guard = [sys.executable, ROOT / 'scripts/run_native_tool.py', '--'] if os.name == 'nt' else []
    def run(name, command):
        result = subprocess.run(list(map(str, guard + command)), cwd=build, capture_output=True, text=True)
        (build / (name + '.log')).write_text(result.stdout + result.stderr)
        require(result.returncode == 0, name + ' failed: ' + result.stderr[-3000:])
        return result.stdout + result.stderr
    source_names = ['tests/host/nes_file_probe.c', 'firmware/common/nes_content.c',
                    'firmware/common/nes_boot.c', 'firmware/common/core_link.c',
                    'firmware/include/multitini_nes_content.h', 'firmware/include/multitini_nes_boot.h',
                    'firmware/include/multitini_core_link.h', 'firmware/include/multitini_nes_interface.h',
                    'firmware/include/multitini_content.h', 'firmware/include/multitini_regs.h',
                    'hdl/core/nes/multitini_nes_defs_pkg.sv', 'scripts/test_nes_files.py']
    before = {name: digest((ROOT / name).read_bytes()) for name in source_names}
    def compile_probe(name, sources, baseline=False):
        library = build / (name + ('.dll' if os.name == 'nt' else '.so'))
        run(name, [tool('gcc'), '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared',
                   *(['-fPIC'] if os.name != 'nt' else []),
                   *(['-DMT_NES_PROBE_INSPECT_ONLY'] if baseline else []),
                   '-I', ROOT / 'firmware/include', ROOT / 'tests/host/nes_file_probe.c',
                   *sources, '-o', library])
        function = ctypes.CDLL(str(library)).mt_nes_file_probe
        function.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                             ctypes.c_size_t, ctypes.c_void_p, ctypes.c_int]
        function.restype = ctypes.c_int
        return function
    current = compile_probe('current', [ROOT / name for name in source_names[1:4]])
    baseline = None
    baseline_record = None
    if args.baseline_ref:
        require(re.fullmatch(r'[A-Za-z0-9._/-]+', args.baseline_ref) is not None, 'Invalid baseline ref')
        old = subprocess.run(['git', '-C', str(ROOT), 'show', args.baseline_ref + ':firmware/common/nes_content.c'],
                             check=True, capture_output=True).stdout
        path = build / 'baseline.c'
        path.write_bytes(old)
        baseline = compile_probe('baseline', [path], baseline=True)
        baseline_record = {'revision': args.baseline_ref,
                           'parser_sha256': digest(old), 'expected_rejection': 5}
    files = sorted(p for p in args.media.rglob('*') if p.is_file() and p.suffix.lower() == '.nes')
    require(files, 'No .nes files found')
    records = []
    sv = ['module tb_files; import multitini_nes_defs_pkg::*; initial begin']
    for index, path in enumerate(files):
        data = path.read_bytes()
        source = ctypes.create_string_buffer(data)
        output = ctypes.create_string_buffer(len(data))
        metadata = (ctypes.c_uint32 * 4)()
        old_result = baseline(source, len(data), output, len(data), metadata, 0) if baseline else None
        if baseline:
            require(old_result == 5, 'Baseline did not reproduce unsupported-version rejection: ' + path.name)
        streams = []
        for burst in (0, 1):
            result = current(source, len(data), output, len(data), metadata, burst)
            require(result == 0, 'Parser/boot rejection ' + str(result) + ': ' + path.name)
            wire = output.raw
            require(wire[16:] == data[16:], 'PRG/CHR payload changed: ' + path.name)
            require(source.raw[:len(data)] == data, 'Input memory changed: ' + path.name)
            streams.append(wire)
        require(streams[0] == streams[1], 'Command and burst streams differ')
        require(path.read_bytes() == data, 'Source file changed: ' + str(path))
        header = streams[0][:16]
        sv.append(f"if (!nes_header_ok(128'h{header[::-1].hex()},32'd{len(data)})) $fatal(1,\"Header {index} rejected by production HDL\");")
        records.append({'name': str(path.relative_to(args.media)), 'bytes': len(data), 'sha256': digest(data),
                        'header': data[:16].hex(' '), 'canonical_header': header.hex(' '),
                        'mapper': metadata[0], 'prg_bytes': metadata[1], 'chr_bytes': metadata[2],
                        'battery': bool(metadata[3]), 'payload_sha256': digest(data[16:]),
                        'baseline_result': old_result, 'command_and_burst_streams_passed': True,
                        'payload_and_input_unchanged': True})
    sv.append('$display("PASS NES exact-file canonical headers accepted by production HDL"); $finish; end endmodule')
    bench = build / 'headers.sv'
    bench.write_text('\n'.join(sv) + '\n')
    executable = build / 'headers.vvp'
    run('headers-compile', [tool('iverilog'), '-g2012', '-s', 'tb_files', '-o', executable,
                           ROOT / 'hdl/core/nes/multitini_nes_defs_pkg.sv', bench])
    output = run('headers-simulate', [tool('vvp'), executable])
    require('PASS NES exact-file' in output and 'FATAL' not in output, 'HDL header audit failed')
    require(before == {name: digest((ROOT / name).read_bytes()) for name in source_names}, 'Audit source changed')
    report = {'passed': True, 'files': records, 'baseline': baseline_record, 'source_sha256': before,
              'limits': 'Exact private files, ARM parser/boot streams and production HDL header gate; no gameplay or physical-board claim.'}
    (build / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    reproduced = 'old rejection reproduced, ' if baseline else ''
    print(f'PASS NES {len(files)} exact files: {reproduced}parser/command/burst/HDL accepted; source files and payloads unchanged')
    if dll_directory is not None:
        dll_directory.close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
