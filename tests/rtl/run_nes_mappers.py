#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the production NES cartridge mapper mux with Icarus Verilog."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, default=ROOT)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'build/nes-mapper-tests')
    parser.add_argument('--iverilog', help='Icarus Verilog compiler executable')
    parser.add_argument('--vvp', help='Icarus Verilog simulation executable')
    args = parser.parse_args()
    root = args.source_root.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixture = Path(__file__).resolve().parent / 'nes_cart_mappers.sv'
    names = ['third_party/nes/upstream/rtl/regs_savestates.sv',
             'hdl/core/nes/ports/MMC1.sv', 'hdl/core/nes/ports/MMC2.sv',
             'hdl/core/nes/ports/MMC3.sv', 'hdl/core/nes/ports/generic.sv',
             'hdl/core/nes/ports/cart.sv']
    tracked = {name: root / name for name in names}
    tracked['tests/rtl/nes_cart_mappers.sv'] = fixture
    tracked['tests/rtl/run_nes_mappers.py'] = Path(__file__).resolve()
    if os.name == 'nt':
        tracked['scripts/run_native_tool.py'] = root / 'scripts/run_native_tool.py'
    def hashes():
        return {name: hashlib.sha256(path.read_bytes()).hexdigest()
                for name, path in tracked.items()}
    before = hashes()
    runtime = Path(os.environ.get('MULTITINI_UCRT_BIN', 'C:/msys64/ucrt64/bin'))
    def tool(name):
        explicit = getattr(args, name)
        if explicit is not None:
            return explicit
        value = str(runtime / (name + '.exe')) if os.name == 'nt' else shutil.which(name)
        if value is None:
            raise FileNotFoundError(name)
        return value
    guard = [sys.executable, str(root / 'scripts/run_native_tool.py'), '--'] if os.name == 'nt' else []
    executable = output / 'cart-mappers.vvp'
    commands = [
        ('compile', guard + [tool('iverilog'), '-g2012', '-s', 'tb_cart_mappers',
                            '-o', str(executable),
                            *[str(root / name) for name in names], str(fixture)]),
        ('simulate', guard + [tool('vvp'), str(executable)]),
    ]
    checks = []
    for stage, command in commands:
        result = subprocess.run(command, cwd=root, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=90)
        (output / (stage + '.log')).write_text(result.stdout, encoding='utf-8')
        checks.append({'stage': stage, 'command': command, 'returncode': result.returncode})
        if result.returncode or (stage == 'simulate' and
                                'PASS NES mappers: 11 mapper IDs, 85 mapped-address assertions' not in result.stdout):
            raise RuntimeError(stage + ' failed:\n' + result.stdout)
        if stage == 'simulate':
            print(result.stdout.strip())
    if hashes() != before:
        raise RuntimeError('Mapper regression inputs changed during verification')
    report = {
        'passed': True,
        'checks': checks,
        'source_sha256': before,
        'mapper_ids': [0, 1, 2, 3, 4, 7, 9, 10, 11, 66, 71],
        'mapped_address_assertions': 85,
        'coverage': ['explicit mapper output mux/pruning', 'PRG and CHR bank changes',
                     'CPU RAM override', 'horizontal/vertical/one-screen mirroring',
                     'master CE hold', 'MMC1 serial registers and NVRAM-only SRAM flags',
                     'MMC3 PRG/CHR inversion, SRAM enable/protect and A12 IRQ',
                     'MMC2/MMC4 distinct PRG sizes and PPU latch windows'],
        'limitations': 'The fixture includes a small SV equivalent of the unused VHDL '
                      'savestate register interface. No savestate, full CPU cartridge, '
                      'physical PSRAM or board gameplay acceptance is claimed.',
    }
    (output / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
