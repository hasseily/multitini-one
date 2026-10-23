#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check REU cache forwarding, queued stores, faults and hard/guest reset scope."""
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT/'build/c64-reu-cache')
    parser.add_argument('--vivado', type=Path, default=Path('C:/Xilinx/2025.2/Vivado'))
    args = parser.parse_args()
    build = args.build_dir.resolve()
    base = (ROOT/'build').resolve()
    if build == base or not build.is_relative_to(base):
        parser.error('Use a dedicated build/ child')
    build.mkdir(parents=True, exist_ok=True)
    receipt = build/'validation.json'
    receipt.unlink(missing_ok=True)
    dut = ROOT/'hdl/core/c64/multitini_c64_reu_cache.sv'
    bench = ROOT/'hdl/sim/tb_c64_reu_cache.sv'
    sources = [dut, bench]
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    hashes = lambda: {str(p.relative_to(ROOT)): sha(p) for p in [Path(__file__).resolve(), *sources]}
    initial = hashes()
    evidence = {'status': 'FAIL', 'source_sha256': initial,
                'limits': 'Actual cache with an independent synthetic acknowledged-memory model. '
                          'Reference bytes update at logical acceptance; backing bytes update only at acknowledged '
                          'completion. No MAME, native DMA cadence, physical DDR latency or board claim. '
                          'Hard reset discards cache state; DDR bridge orphan drain is a separate regression.',
                'directed_coverage': ['resident hit and direct-mapped eviction',
                    'partial-word coalescing and disable/flush drain',
                    'oldest-to-newest queued fill forwarding and same-edge write',
                    'write to an already returned word before fill completes',
                    'full queue backpressure and merge while full',
                    'read-after-write including dirty eviction', '16 MiB prefetch wrap',
                    'zero-latency read/write completion',
                    'write fault blocks clean quiescence and hard-reset recovery',
                    'faulted fill stays unreadable', 'hard reset during request backpressure']}
    env = {k: v for k, v in os.environ.items() if k.upper() != 'PATH'}
    env['PATH'] = os.environ.get('PATH', os.environ.get('Path', ''))

    def run(label, arguments, expect_failure=False):
        tool = args.vivado/'bin'/(arguments[0]+('.bat' if os.name == 'nt' else ''))
        child = subprocess.run([sys.executable, str(ROOT/'scripts/run_native_tool.py'), '--',
                                str(tool), *map(str, arguments[1:])], cwd=build, env=env,
                               capture_output=True, text=True, errors='replace', timeout=300,
                               creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        output = child.stdout+child.stderr
        (build/(label+'.log')).write_text(output)
        failed = child.returncode != 0 or re.search(r'\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b', output, re.I)
        if failed and not expect_failure:
            raise RuntimeError(label+' failed: '+output[-12000:])
        return output, bool(failed)

    try:
        run('compile', ['xvlog', '--nolog', '--sv', *sources])
        run('elaborate', ['xelab', '--nolog', '--timescale', '1ns/1ps', '--snapshot', 'reu_cache', 'tb_c64_reu_cache'])
        output, _ = run('simulate', ['xsim', '--nolog', 'reu_cache', '--runall'])
        passes = [line for line in output.splitlines() if line.startswith('PASS: C64 REU cache ')]
        if len(passes) != 1:
            raise RuntimeError('Missing unique cache completion')
        evidence['summary'] = passes[0]
        evidence['counts'] = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', passes[0])}
        evidence['log_sha256'] = sha(build/'simulate.log')
        print(passes[0], flush=True)
        # Deliberately drop pending-store overlay in a generated cache copy.
        # The independent architectural byte scoreboard must reject readback.
        text = dut.read_text()
        original = 'fill_data=merge_bytes(fill_data,wb_data[slot],wb_strb[slot]);'
        assert text.count(original) == 1
        bad = build/'missing_forwarding.sv'
        bad.write_text(text.replace(original, 'fill_data=fill_data;'))
        run('negative-compile', ['xvlog', '--nolog', '--sv', bad])
        run('negative-elaborate', ['xelab', '--nolog', '--timescale', '1ns/1ps', '--snapshot', 'cache_bad', 'tb_c64_reu_cache'])
        output, failed = run('negative', ['xsim', '--nolog', 'cache_bad', '--runall'], True)
        if not failed or 'readback mismatch' not in output:
            raise RuntimeError('Missing forwarding negative control was not rejected by readback scoreboard')
        evidence['negative_control'] = {'status': 'PASS', 'missing_forwarding_rejected': True,
                                        'log_sha256': sha(build/'negative.log')}
        if hashes() != initial:
            raise RuntimeError('Sources changed during cache validation')
        evidence.update(status='PASS', sources_unchanged=True)
    except Exception as error:
        evidence['error'] = str(error)
        raise
    finally:
        receipt.write_text(json.dumps(evidence, indent=2)+'\n')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print('ERROR: '+str(error), file=sys.stderr)
        raise SystemExit(1)
