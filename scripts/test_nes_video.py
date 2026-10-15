#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the complete NES native raster and its shared DDR video transport."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    build = ROOT / 'build/nes-video-tests'
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get('MULTITINI_UCRT_BIN', 'C:/msys64/ucrt64/bin'))
    environment = os.environ.copy()
    if os.name == 'nt':
        environment['VERILATOR_ROOT'] = (runtime.parent/'share/verilator').as_posix()
    def tool(name):
        return str(runtime / (name + '.exe')) if os.name == 'nt' else name
    guard = [sys.executable, str(ROOT/'scripts/run_native_tool.py'), '--'] if os.name == 'nt' else []
    sources = [
        'third_party/appletini/hdl/cdc_bit_sync.sv',
        'hdl/common/multitini_async_mailbox.sv',
        'hdl/common/multitini_video_capture.sv',
        'hdl/common/multitini_video_frame_reader.sv',
        'hdl/board/multitini_nes_video_normalizer.sv',
        'hdl/board/multitini_nes_video_bridge.sv',
        'hdl/sim/video_fifo_model.sv',
        'hdl/sim/xilinx_primitives.sv',
    ]
    benches = ['hdl/sim/tb_nes_video_normalizer.sv', 'hdl/sim/tb_nes_video_bridge.sv']
    tracked = sources + benches + ['scripts/test_nes_video.py']
    def hashes():
        return {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in tracked}
    before = hashes()
    checks = []
    for name in ('normalizer', 'bridge'):
        directory = build / name
        directory.mkdir(parents=True, exist_ok=True)
        top = 'tb_nes_video_' + name
        bench = ROOT / ('hdl/sim/' + top + '.sv')
        if name == 'normalizer':
            executable = directory / 'video.vvp'
            commands = [
                ('compile', [tool('iverilog'), '-g2012', '-s', top, '-o', str(executable),
                             str(ROOT/sources[4]), str(bench)]),
                ('simulate', [tool('vvp'), str(executable)]),
            ]
        else:
            executable = directory/'obj'/(('V'+top)+('.exe' if os.name == 'nt' else ''))
            obj = directory/'obj'
            commands = [
                ('generate', [tool('verilator_bin') if os.name == 'nt' else tool('verilator'),
                             '--cc', '--exe', '--main', '--timing', '--top-module', top,
                             '-Wno-fatal', '--Mdir', str(obj),
                             *[str(ROOT/p) for p in sources], str(bench)]),
            ]
            if os.name == 'nt':
                include = runtime.parent/'share/verilator/include'
                # Source generation precedes expanding the generated C++ list.
                commands.append(('compile', None))
            else:
                commands.append(('compile', ['make', '-C', str(obj), '-f', 'V'+top+'.mk', '-j8']))
            commands.append(('simulate', [str(executable)]))
        for stage, command in commands:
            if command is None:
                command = [tool('g++'), '-std=c++20', '-O1', '-pthread', '-DVL_TIME_CONTEXT',
                           '-I'+str(include), '-I'+str(include/'vltstd'), '-I'+str(obj),
                           *[str(p) for p in sorted(obj.glob('*.cpp'))],
                           str(include/'verilated.cpp'), str(include/'verilated_timing.cpp'),
                           str(include/'verilated_threads.cpp'), '-o', str(executable)]
            result = subprocess.run(guard+command, cwd=directory, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=300, env=environment)
            (directory/(stage+'.log')).write_text(result.stdout)
            if result.returncode or (stage == 'simulate' and 'PASS NES' not in result.stdout):
                print(result.stdout[-18000:])
                raise RuntimeError(f'NES video {name}/{stage} failed')
            if stage == 'simulate':
                print('\n'.join(line for line in result.stdout.splitlines() if 'PASS' in line), flush=True)
        checks.append(name)
    if hashes() != before:
        raise RuntimeError('NES video test inputs changed during verification')
    (build/'validation.json').write_text(json.dumps({'passed': True, 'checks': checks,
        'source_sha256': before}, indent=2)+'\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
