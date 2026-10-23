#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check native REU byte cadence through its production cache and DDR bridge."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
from c64_vice_oracle import COMMIT as PIN, verify_reference


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT/'build/c64-reu-native')
    parser.add_argument('--single', action='store_true', help='Run only 24-bit PAL development case')
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if build == ROOT/'build' or not build.is_relative_to(ROOT/'build'):
        parser.error('Use a dedicated build/ child')
    build.mkdir(parents=True, exist_ok=True)
    receipt = build/'validation.json'
    receipt.unlink(missing_ok=True)
    sources = [ROOT/'third_party/appletini/hdl/cdc_bit_sync.sv',
               ROOT/'hdl/core/c64/multitini_c64_reu.sv',
               ROOT/'hdl/core/c64/multitini_c64_reu_cache.sv',
               ROOT/'hdl/core/c64/multitini_c64_ddr_path.sv',
               ROOT/'hdl/sim/tb_c64_reu_native.sv']
    reference = ROOT/'third_party/vice_reference/upstream/vice/src/c64/cart/reu.c'
    inputs = [Path(__file__).resolve(), ROOT/'scripts/run_native_tool.py', ROOT/'scripts/c64_vice_oracle.py', reference, *sources]
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    hashes = lambda: {p.relative_to(ROOT).as_posix(): sha(p) for p in inputs}
    before = hashes()
    result = {'schema': 1, 'status': 'FAIL', 'reference_commit': PIN,
              'source_sha256': before, 'checks': [],
              'limits': ['Production REU/cache/DDR bridge with independent bounded AXI RAM; no physical DDR latency or board proof.',
                         'VICE supplies one native cycle for stash/fetch/verify and two for swap; this backend test does not compare VIC BA or verify-tail release.',
                         'Four native cycles between register writes permit normal address-hint prefetch. The qualified modeled backend budget is64 core clocks per accepted request through its last response.',
                         'The longer90-95 clock profile is outside that qualified budget and must retain its real cold-first-byte failure; unbounded DDR stalls do not preserve every native slot.',
                         'A synthetic native slot-availability driver is used here; VICE BA arbitration/release is qualified by test_c64_vice_bus.py and actual VIC/memory mapping by the production-machine regression.']}
    runtime = Path(os.environ.get('MULTITINI_UCRT_BIN', 'C:/msys64/ucrt64/bin'))
    iv = runtime/'iverilog.exe' if os.name == 'nt' else 'iverilog'
    vv = runtime/'vvp.exe' if os.name == 'nt' else 'vvp'
    guard = [sys.executable, str(ROOT/'scripts/run_native_tool.py'), '--'] if os.name == 'nt' else []
    env = {k: v for k, v in os.environ.items() if k.upper() != 'PATH'}
    env['PATH'] = os.environ.get('PATH', os.environ.get('Path', ''))
    evidence_dirs = []

    def run(directory, name, command, expected_failure=False):
        child = subprocess.run([*guard, *map(str, command)], cwd=directory, env=env,
                               capture_output=True, text=True, errors='replace', timeout=300)
        output = child.stdout+child.stderr
        log = directory/(name+'.log')
        log.write_text(output)
        failed = bool(child.returncode or re.search(r'\b(?:FATAL|ERROR):', output))
        if failed and not expected_failure:
            raise RuntimeError(name+' failed:\n'+output[-12000:])
        return output, failed

    try:
        verify_reference()
        profiles = [(24, 0)] if args.single else [(19, 0), (19, 1), (24, 0), (24, 1)]
        for bits, ntsc in profiles:
            directory = build/f'{bits}-{ntsc}'
            directory.mkdir(exist_ok=True)
            evidence_dirs.append(directory)
            exe = directory/'native.vvp'
            for name in ('native.csv', 'ddr.csv', 'compile.log', 'simulate.log'):
                (directory/name).unlink(missing_ok=True)
            run(directory, 'compile', [iv, '-g2012', '-s', 'tb_c64_reu_native',
                f'-Ptb_c64_reu_native.ADDRESS_BITS={bits}', f'-Ptb_c64_reu_native.NTSC={ntsc}',
                '-o', exe, *sources])
            output, _ = run(directory, 'simulate', [vv, exe])
            passes = [s for s in output.splitlines() if s.startswith('PASS: C64 REU native ')]
            cases = [s for s in output.splitlines() if s.startswith('CASE: ')]
            if len(passes) != 1 or len(cases) != 57 or len(set(cases)) != len(cases):
                raise RuntimeError('Native case coverage/completion missing')
            counts = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', passes[0])}
            if counts.get('bits') != bits or counts.get('ntsc') != ntsc or counts.get('cases') != len(cases):
                raise RuntimeError('Native profile or case count differs')
            if not 60 <= counts.get('max_ddr_clocks', 0) <= 64:
                raise RuntimeError('Bounded high latency DDR profile not exercised')
            result['checks'].append({'address_bits': bits, 'ntsc': ntsc, 'summary': passes[0],
                'counts': counts, 'cases': cases, 'log_sha256': sha(directory/'simulate.log'),
                'trace_sha256': sha(directory/'native.csv'),
                'ddr_trace_sha256': sha(directory/'ddr.csv')})
            print(passes[0], flush=True)
        # Skip one otherwise valid native slot from an already passing pair.
        negative = build/'negative-gap'
        negative.mkdir(exist_ok=True)
        evidence_dirs.append(negative)
        for name in ('native.csv', 'ddr.csv', 'simulate.log'):
            (negative/name).unlink(missing_ok=True)
        output, failed = run(negative, 'simulate', [vv, exe, '+INJECT_GAP'], True)
        if not failed or 'native slot missing' not in output:
            raise RuntimeError('Injected native gap was not rejected')
        result['negative_control'] = {'status': 'PASS', 'native_gap_rejected': True,
                                     'log_sha256': sha(negative/'simulate.log')}
        # Retain the actual cold-start deadline failure outside the qualified
        # backend budget. This is a limitation measurement, not a waived pass.
        outside = build/'outside-latency-contract'
        outside.mkdir(exist_ok=True)
        evidence_dirs.append(outside)
        for name in ('native.csv', 'ddr.csv', 'compile.log', 'simulate.log'):
            (outside/name).unlink(missing_ok=True)
        outside_exe = outside/'native.vvp'
        run(outside, 'compile', [iv, '-g2012', '-s', 'tb_c64_reu_native',
            '-Ptb_c64_reu_native.ADDRESS_BITS=24', '-Ptb_c64_reu_native.NTSC=0',
            '-Ptb_c64_reu_native.LONG_LATENCY=1', '-o', outside_exe, *sources])
        output, failed = run(outside, 'simulate', [vv, outside_exe], True)
        signature = 'native slot missing case=cold-bank-written-last frame=0 accepted=0'
        if not failed or signature not in output:
            raise RuntimeError('Outside-contract cold-start failure was not reproduced')
        observed = re.findall(r'max_ddr_clocks=(\d+)', output)
        if not observed or not 90 <= int(observed[-1]) <= 96:
            raise RuntimeError('Outside-contract DDR latency not demonstrated')
        result['outside_qualified_latency'] = {'simulation_status': 'FAIL',
            'expected_failure_observed': True, 'failure': signature,
            'observed_max_ddr_clocks': int(observed[-1]), 'qualified_budget_clocks': 64,
            'log_sha256': sha(outside/'simulate.log'),
            'limits': 'Native first-slot deadline fails with this90-95 clock modeled backend; this profile is not timing-qualified.'}
        if hashes() != before:
            raise RuntimeError('Sources changed during native REU validation')
        result.update(status='PASS', sources_unchanged=True)
    except Exception as error:
        result['error'] = str(error)
        raise
    finally:
        result['artifacts'] = {p.relative_to(build).as_posix(): {'bytes': p.stat().st_size, 'sha256': sha(p)}
                               for directory in evidence_dirs for p in sorted(directory.iterdir())
                               if p.suffix in ('.csv', '.log')}
        receipt.write_text(json.dumps(result, indent=2)+'\n')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print('ERROR: '+str(error), file=sys.stderr)
        raise SystemExit(1)
