#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the native UCI endpoint with unchanged pinned Ultimate VHDL in XSim."""
from __future__ import annotations

import argparse
import csv
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'third_party/ultimate_firmware_reference'
COMMIT = 'd43546fe25b26cb332f43f00219d33153dfe44f8'
REF_FILES = [
    'fpga/ip/busses/vhdl_source/io_bus_pkg.vhd',
    'fpga/ip/busses/vhdl_source/slot_bus_pkg.vhd',
    'fpga/io/command_interface/vhdl_source/command_if_pkg.vhd',
    'fpga/io/command_interface/vhdl_source/command_protocol.vhd',
]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out.exists() or out == ROOT / 'build' or not out.is_relative_to(ROOT / 'build'):
        parser.error('Choose a fresh child of this checkout build/ directory')
    out.mkdir(parents=True)
    manifest_path = REFERENCE / 'import_manifest.json'
    manifest = json.loads(manifest_path.read_text())
    assert manifest['commit'] == COMMIT
    assert manifest['repository'] == 'https://github.com/GideonZ/1541ultimate'
    entries = {entry['path']: entry for entry in manifest['files']}
    for relative in REF_FILES:
        assert digest(REFERENCE / relative) == entries[relative]['sha256'], relative
    sources = [REFERENCE / path for path in REF_FILES] + [manifest_path,
        ROOT / 'hdl/core/c64/multitini_c64_uci.sv',
        ROOT / 'hdl/sim/c64_uci_upstream_bridge.vhd',
        ROOT / 'hdl/sim/tb_c64_uci_oracle.sv', Path(__file__).resolve(),
        ROOT / 'scripts/run_native_tool.py']
    before = {path.relative_to(ROOT).as_posix(): digest(path) for path in sources}
    receipt = {'status': 'FAIL', 'reference_commit': COMMIT,
        'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'source_sha256': before, 'commands': [],
        'scope': 'Real pinned command_protocol VHDL and production native endpoint. Identical '
                 'accepted guest transactions, separate synchronous RAM fixtures, equivalent '
                 'host actions translated through each real host API. Compare only after bounded '
                 'settling; no claim of equal internal clock latency, firmware, CPU/CDC or board proof.',
        'intentional_differences': [
            '896-byte command: upstream reports895; DUT reports896, all896 RAM bytes equal.',
            '896-byte response: first896 bytes equal; upstream repeats last byte, DUT empties.',
            'Idle ABORT: upstream exposes abort bit; DUT keeps separate sticky diagnostic.',
            'Busy command writes: upstream modifies command buffer; DUT drops and diagnoses.',
            'Overflow write: upstream replaces last byte; DUT bounds and diagnoses.'],
        'limits': ['FF00 endpoint triggering covered; accepted native FF00 routing, CPU halt/resume '
                   'and turbo snoop timing require the separate full-machine integration.',
                   'Rejected PUSH common status tested with normal control bits; retained original '
                   'FREEZE/IRQ/trigger flags on rejected pushes are a deliberate safety difference.',
                   'Upstream status length is8bits, so common maximum is255; DUT256 extension '
                   'is outside this comparison.']}
    env = {key: value for key, value in os.environ.items() if key.lower() != 'path'}
    env['PATH'] = os.environ.get('PATH', os.environ.get('Path', ''))
    vendor = Path('C:/Xilinx/2025.2/Vivado/bin')

    def run(name: str, arguments: list, negative: bool = False) -> str:
        command = [sys.executable, str(ROOT / 'scripts/run_native_tool.py'), '--',
                   *map(str, arguments)]
        result = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True,
                                errors='replace', timeout=300)
        output = result.stdout + result.stderr
        log = out / (name + '.log')
        log.write_text(output)
        receipt['commands'].append({'argv': command, 'returncode': result.returncode,
                                    'log': log.name, 'sha256': digest(log)})
        if negative:
            assert 'NEGATIVE CONTROL wrong IRQ clearing' in output, output[-5000:]
            assert 'PASS C64 UCI oracle' not in output, 'Corruption unexpectedly passed'
        elif result.returncode or re.search(r'\b(?:ERROR|FATAL)\s*:', output, re.I):
            raise RuntimeError(name + ': ' + output[-6000:])
        print(name, 'rejected as expected' if negative else 'completed', flush=True)
        return output

    try:
        run('reference-compile', [vendor / 'xvhdl.bat', '--nolog', '--2008',
            *[REFERENCE / path for path in REF_FILES], ROOT / 'hdl/sim/c64_uci_upstream_bridge.vhd'])
        run('dut-compile', [vendor / 'xvlog.bat', '--nolog', '--sv',
            ROOT / 'hdl/core/c64/multitini_c64_uci.sv', ROOT / 'hdl/sim/tb_c64_uci_oracle.sv'])
        run('elaborate', [vendor / 'xelab.bat', '--nolog', '--relax', '--mt', 'off',
            '--timescale', '1ns/1ps', '--snapshot', 'uci_oracle', 'tb_c64_uci_oracle'])
        output = run('compare', [vendor / 'xsim.bat', '--nolog', 'uci_oracle', '--runall'])
        match = re.search(r'PASS C64 UCI oracle cases=(\d+) checks=(\d+) intentional_differences=(\d+)', output)
        assert match, output[-6000:]
        receipt['cases'], receipt['checks'], receipt['difference_cases'] = map(int, match.groups())
        assert receipt['cases'] == 39 and receipt['difference_cases'] == 5
        with (out / 'comparison.csv').open(newline='') as trace:
            rows = list(csv.DictReader(trace))
        receipt['case_list'] = list(dict.fromkeys(row['case'] for row in rows))
        assert len(receipt['case_list']) == receipt['cases']
        receipt['compared_observations'] = len(rows)
        run('negative-irq-clear', [vendor / 'xsim.bat', '--nolog', 'uci_oracle', '--runall',
                                  '--testplusarg', 'wrong_irq_clear'], negative=True)
        receipt['negative_control'] = {'wrong_irq_clear': 'rejected'}
        assert before == {path.relative_to(ROOT).as_posix(): digest(path) for path in sources}, 'Sources changed'
        receipt['sources_unchanged'] = True
        receipt['status'] = 'PASS'
    except Exception as error:
        receipt['error'] = str(error)
        print(error, flush=True)
    finally:
        receipt['finished_utc'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        receipt['artifacts'] = {path.name: digest(path) for path in out.iterdir()
                                if path.is_file() and path.suffix in ('.log', '.csv')}
        (out / 'validation.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt['status'] != 'PASS'


if __name__ == '__main__':
    raise SystemExit(main())
