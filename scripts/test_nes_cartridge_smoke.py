#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Optional private NES file smoke tests; all cartridge-derived output stays in build/."""
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'scripts'))
from test_nes import sources


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('cartridges', nargs='*', type=Path)
    parser.add_argument('--directory', type=Path)
    parser.add_argument('--probe-dll', type=Path, default=ROOT / 'build/core-nes-m051/private-file-tests/current.dll')
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/nes-private-cartridge-smoke')
    parser.add_argument('--frames', type=int, default=30)
    parser.add_argument('--latency', type=int, default=80)
    parser.add_argument('--jobs', type=int, default=3)
    parser.add_argument('--timeout', type=int, default=1200)
    parser.add_argument('--vivado-bin', type=Path, default=Path(os.environ.get('XILINX_VIVADO', 'C:/Xilinx/2025.2/Vivado')) / 'bin')
    args = parser.parse_args()
    if not 10 <= args.frames <= 120 or not 1 <= args.jobs <= 3 or args.latency < 1:
        parser.error('frames must be 10..120, jobs1..3, latency positive')
    build = args.build_dir.resolve()
    if not build.is_relative_to((ROOT / 'build').resolve()):
        parser.error('private output must stay under this checkout build/ directory')
    build.mkdir(parents=True, exist_ok=True)
    report_path = build / 'validation.json'
    report_path.unlink(missing_ok=True)
    files = args.cartridges + (sorted(args.directory.glob('*.nes')) if args.directory else [])
    if not files:
        parser.error('provide cartridge paths or --directory')
    probe = args.probe_dll.resolve()
    provenance = probe.with_name('validation.json')
    prior = json.loads(provenance.read_text())
    for name, digest in prior['source_sha256'].items():
        if sha(ROOT / name) != digest:
            raise RuntimeError('production parser/boot probe is stale: ' + name)
    dll = ctypes.CDLL(str(probe))
    bridge = dll.mt_nes_file_probe
    byte_pointer = ctypes.POINTER(ctypes.c_uint8)
    bridge.argtypes = [byte_pointer, ctypes.c_size_t, byte_pointer, ctypes.c_size_t,
                       ctypes.POINTER(ctypes.c_uint32), ctypes.c_int]
    bridge.restype = ctypes.c_int
    vhdl, sv = sources()
    fixture = ROOT / 'tests/rtl/nes_cartridge_smoke.sv'
    tracked = vhdl + sv + [fixture, Path(__file__), ROOT / 'scripts/test_nes.py',
                          ROOT / 'scripts/port_nes_core.py']
    before = {p.relative_to(ROOT).as_posix(): sha(p) for p in tracked}
    jobs = []
    for number, original in enumerate(files):
        original = original.resolve()
        data = original.read_bytes()
        source_hash = hashlib.sha256(data).hexdigest()
        incoming = (ctypes.c_uint8 * len(data)).from_buffer_copy(data)
        wire = (ctypes.c_uint8 * len(data))()
        metadata = (ctypes.c_uint32 * 4)()
        status = bridge(incoming, len(data), wire, len(wire), metadata, 1)
        if status:
            raise RuntimeError(f'production NES parser/boot rejected {original.name}: {status}')
        canonical = bytes(wire)
        if bytes(incoming) != data or canonical[16:] != data[16:]:
            raise RuntimeError('production boot changed private cartridge payload')
        slug = f'{number:02d}-' + re.sub(r'[^a-z0-9]+', '-', original.stem.lower()).strip('-')
        job_output = (build / slug).resolve()
        if not job_output.is_relative_to(build):
            raise RuntimeError('private job output escapes build directory')
        job_output.mkdir(exist_ok=True)
        # Validate resolved files as well as their parent, including an old
        # output path that might have been replaced by a symlink or junction.
        for name in ('canonical.nes', 'config.txt', 'run.tcl', 'result.json',
                     'final.ppm', 'validation.json', 'simulate.log'):
            if not (job_output / name).resolve().is_relative_to(build):
                raise RuntimeError('private output file escapes build directory: ' + name)
        canonical_path = job_output / 'canonical.nes'
        canonical_path.write_bytes(canonical)
        (job_output / 'config.txt').write_text(f'{len(data)} {args.frames} {args.latency}\n')
        jobs.append({'path': original, 'directory': job_output, 'canonical': canonical_path,
                     'name': original.name, 'bytes': len(data), 'sha256': source_hash,
                     'canonical_sha256': sha(canonical_path), 'mapper': int(metadata[0]),
                     'prg_bytes': int(metadata[1]), 'chr_bytes': int(metadata[2]),
                     'battery': bool(metadata[3])})

    def run(stage, command, directory, timeout):
        converted = [p.as_posix() if isinstance(p, Path) else str(p) for p in command]
        temporary = directory / 'tmp'
        temporary.mkdir(exist_ok=True)
        environment = dict(os.environ, TEMP=temporary.as_posix(), TMP=temporary.as_posix())
        result = subprocess.run(converted, cwd=directory, capture_output=True, text=True,
                                timeout=timeout, env=environment)
        output = result.stdout + result.stderr
        (directory / f'{stage}.log').write_text(output)
        if result.returncode or re.search(r'\b(?:ERROR|FAIL|FATAL)\s*:', output, re.I):
            raise RuntimeError(f'{stage} failed in {directory}:\n{output[-10000:]}')
        return output

    compiled = build / 'compiled'
    compiled.mkdir(exist_ok=True)
    suffix = '.bat' if os.name == 'nt' else ''
    bin_path = args.vivado_bin
    run('xvlog', [bin_path / ('xvlog' + suffix), '--sv', *sv, fixture], compiled, 300)
    run('xvhdl', [bin_path / ('xvhdl' + suffix), '--2008', *vhdl], compiled, 300)
    run('xelab', [bin_path / ('xelab' + suffix), 'nes_cartridge_smoke',
                  '--snapshot', 'private_cartridge', '--debug', 'typical'], compiled, 600)
    print('Private generic original T65/PPU/APU snapshot compiled', flush=True)

    def simulate(job):
        directory = job['directory']
        # Each process gets its own simulator scratch files. The common
        # compiled snapshot contains no game data; files are opened at runtime.
        shutil.copytree(compiled / 'xsim.dir', directory / 'xsim.dir', dirs_exist_ok=True)
        (directory / 'run.tcl').write_text('run all\nquit\n')
        command = [bin_path / ('xsim' + suffix), 'private_cartridge', '--tclbatch', directory / 'run.tcl']
        output = run('simulate', command, directory, args.timeout)
        if 'PASS NES private cartridge smoke:' not in output:
            raise RuntimeError('private cartridge smoke has no PASS marker: ' + job['name'])
        if sha(job['path']) != job['sha256']:
            raise RuntimeError('private input changed during simulation: ' + job['name'])
        if sha(job['canonical']) != job['canonical_sha256']:
            raise RuntimeError('canonical runtime bytes changed during simulation')
        result = {key: value for key, value in job.items() if key not in ('path', 'directory', 'canonical')}
        result.update(json.loads((directory / 'result.json').read_text()))
        result['passed'] = True
        result['private_output'] = directory.relative_to(ROOT).as_posix()
        (directory / 'validation.json').write_text(json.dumps(result, indent=2) + '\n')
        print(f'PASS {job["name"]}: {result["frames"]} frames, {result["cpu_fetches"]} CPU fetches, '
              f'{result["nonuniform_frames"]} nonuniform frames, peak debt{result["peak_debt"]}', flush=True)
        return result

    results, failures = [], []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        tasks = {pool.submit(simulate, job): job for job in jobs}
        for task in as_completed(tasks):
            try:
                results.append(task.result())
            except Exception as error:
                failures.append({'name': tasks[task]['name'], 'error': str(error)})
                print(str(error), flush=True)
    after = {p.relative_to(ROOT).as_posix(): sha(p) for p in tracked}
    if before != after:
        raise RuntimeError('private smoke HDL/harness sources changed during validation')
    report = {'passed': not failures, 'files': sorted(results, key=lambda row: row['name']),
              'failures': failures, 'source_sha256': before, 'probe_dll_sha256': sha(probe),
              'probe_source_sha256': prior['source_sha256'], 'frames_requested': args.frames,
              'rom_response_service_cycles': args.latency,
              'limits': 'Optional private-file original T65/PPU/APU initialization smoke only. '
                        'No controller input, gameplay acceptance, physical PSRAM, HDMI or board proof. '
                        'Unknown startup pixels are counted for frames1..3; later visible unknowns fail. '
                        'All cartridge copies and final frame images remain under ignored build/, outside delivery.'}
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    return 1 if failures else 0


if __name__ == '__main__':
    raise SystemExit(main())
