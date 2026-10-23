#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate the C64 REU DDR aperture, AXI responses and reset drain."""
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
    parser.add_argument('--build-dir', type=Path, default=ROOT/'build/c64-ddr')
    parser.add_argument('--vivado', type=Path, default=Path('C:/Xilinx/2025.2/Vivado'))
    args = parser.parse_args()
    build = args.build_dir.resolve()
    base = (ROOT/'build').resolve()
    if build == base or not build.is_relative_to(base):
        parser.error('Use a dedicated build/ child')
    build.mkdir(parents=True, exist_ok=True)
    receipt = build/'validation.json'
    receipt.unlink(missing_ok=True)
    dut = ROOT/'hdl/core/c64/multitini_c64_ddr_path.sv'
    sources = [ROOT/'third_party/appletini/hdl/cdc_bit_sync.sv', dut,
               ROOT/'hdl/sim/tb_c64_ddr.sv']
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    hashes = lambda: {str(p.relative_to(ROOT)): sha(p) for p in [Path(__file__).resolve(), *sources]}
    initial = hashes()
    evidence = {'status': 'FAIL', 'source_sha256': initial, 'checks': [],
                'aperture': '0x25000000..0x25ffffff',
                'limits': 'Production AXI bridge with modeled DDR slave and random backpressure. '
                          'No physical DDR latency, cache cadence, whole-machine or board claim.'}
    env = {k: v for k, v in os.environ.items() if k.upper() != 'PATH'}
    env['PATH'] = os.environ.get('PATH', os.environ.get('Path', ''))

    def run(label, arguments, expect_failure=False):
        tool = args.vivado/'bin'/(arguments[0]+('.bat' if os.name == 'nt' else ''))
        child = subprocess.run([sys.executable, str(ROOT/'scripts/run_native_tool.py'), '--',
                                str(tool), *map(str, arguments[1:])], cwd=build, env=env,
                               capture_output=True, text=True, errors='replace', timeout=300,
                               creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
        output = child.stdout+child.stderr
        log = build/(label+'.log')
        log.write_text(output)
        failed = child.returncode != 0 or re.search(r'\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b', output, re.I)
        if failed and not expect_failure:
            raise RuntimeError(label+' failed: '+output[-12000:])
        return output, bool(failed)

    try:
        run('compile', ['xvlog', '--nolog', '--sv', *sources])
        for label, top in [('pal', 'tb_c64_ddr'), ('ntsc', 'tb_c64_ddr_ntsc')]:
            snapshot = 'ddr_'+label
            run(label+'-elaborate', ['xelab', '--nolog', '--timescale', '1ns/1ps', '--snapshot', snapshot, top])
            output, _ = run(label, ['xsim', '--nolog', snapshot, '--runall'])
            passes = [line for line in output.splitlines() if line.startswith('PASS: C64 REU DDR')]
            if len(passes) != 1:
                raise RuntimeError(label+' lacks unique completion')
            evidence['checks'].append({'name': label, 'summary': passes[0], 'log_sha256': sha(build/(label+'.log'))})
            print(passes[0], flush=True)
        # Corrupt only the physical aperture in a generated copy. The same
        # independent AXI address monitor must reject it.
        bad = build/'wrong_aperture.sv'
        text = dut.read_text()
        assert text.count("APERTURE=32'h25000000") == 1
        bad.write_text(text.replace("APERTURE=32'h25000000", "APERTURE=32'h24000000"))
        run('negative-compile', ['xvlog', '--nolog', '--sv', bad])
        run('negative-elaborate', ['xelab', '--nolog', '--timescale', '1ns/1ps', '--snapshot', 'ddr_wrong', 'tb_c64_ddr'])
        output, failed = run('negative', ['xsim', '--nolog', 'ddr_wrong', '--runall'], True)
        if not failed or 'write bounds/attributes' not in output:
            raise RuntimeError('Wrong aperture negative control was not rejected by AXI bounds check')
        evidence['negative_control'] = {'status': 'PASS', 'wrong_aperture_rejected': True,
                                        'log_sha256': sha(build/'negative.log')}
        if hashes() != initial:
            raise RuntimeError('Sources changed during DDR validation')
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
