#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Loaded cartridge banking, expansion policy and REU DMA through the real adapter."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--skip-negatives', action='store_true')
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build') or out == ROOT / 'build':
        parser.error('fresh dedicated build/ child required')
    out.mkdir(parents=True)
    sources = [ROOT / 'hdl/generated/multitini_regs_pkg.sv',
               ROOT / 'third_party/appletini/hdl/cdc_bit_sync.sv',
               ROOT / 'hdl/common/multitini_async_mailbox.sv']
    section = ''
    for raw in (ROOT / 'hdl/hdl_sources.txt').read_text().splitlines():
        line = raw.strip()
        if line.startswith('['):
            section = line
        elif section == '[c64]' and line and not line.startswith('#'):
            path = (ROOT / 'hdl' / line.split('|')[0].strip()).resolve()
            if path.suffix in ('.sv', '.v', '.vhd'):
                sources.append(path)
    sources.append(ROOT / 'hdl/sim/tb_c64_cart_coexist.sv')
    evidence = sources + [Path(__file__).resolve(), ROOT / 'scripts/run_native_tool.py', ROOT / 'hdl/hdl_sources.txt']
    receipt = {'status': 'FAIL', 'source_sha256': {p.relative_to(ROOT).as_posix(): sha(p) for p in evidence},
               'cases': [], 'negative_controls': []}
    for path in evidence:
        target = out / 'sources' / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    snap = lambda p: out / 'sources' / p.relative_to(ROOT)
    vivado = Path(os.environ.get('XILINX_VIVADO', 'C:/Xilinx/2025.2/Vivado')) / 'bin'

    def run(folder, name, argv, failure=None):
        result = subprocess.run([sys.executable, ROOT / 'scripts/run_native_tool.py', '--cwd', folder,
                                 '--', *argv], capture_output=True, text=True, errors='replace', timeout=900)
        log = result.stdout + result.stderr
        (folder / (name + '.log')).write_text(log)
        bad = result.returncode or re.search(r'\b(?:ERROR|FATAL|FAIL)\s*:|\bFatal:', log, re.I)
        if failure:
            if not bad or failure not in log:
                raise RuntimeError(name + ' negative control not rejected: ' + log[-3000:])
        elif bad:
            raise RuntimeError(name + ': ' + log[-6000:])
        return log

    try:
        for ntsc in (0, 1):
            folder = out / ('ntsc' if ntsc else 'pal')
            folder.mkdir()
            wrapper = folder / 'profile.sv'
            wrapper.write_text(f'module profile; tb_c64_cart_coexist #(.NTSC({ntsc})) test(); endmodule\n')
            sv = [p for p in sources if p.suffix != '.vhd']
            run(folder, 'imported', [vivado / 'xvlog.bat', '--nolog', '--sv', '--relax',
                                    *[snap(p) for p in sv if 'third_party' in p.parts]])
            run(folder, 'local', [vivado / 'xvlog.bat', '--nolog', '--sv',
                                 *[snap(p) for p in sv if 'third_party' not in p.parts], wrapper])
            run(folder, 'vhdl', [vivado / 'xvhdl.bat', '--nolog', '--2008',
                                *[snap(p) for p in sources if p.suffix == '.vhd']])
            run(folder, 'elaborate', [vivado / 'xelab.bat', '--nolog', '--relax', '--mt', 'off',
                                     '--timescale', '1ns/1ps', '-s', 'coexist_sim', 'profile'])
            # The 2 MiB capacity streams have no video-standard dependency, so
            # they run on PAL only; they add two profiles and the id 87 line.
            capacity = ntsc == 0
            plusargs = [] if capacity else ['--testplusarg', 'skip_capacity']
            log = run(folder, 'positive', [vivado / 'xsim.bat', '--nolog', 'coexist_sim', '--runall', *plusargs])
            matches = re.findall(r'PASS CART COEXIST NTSC=' + str(ntsc) +
                                 r' profiles=(\d+) checks=(\d+) DMA=272 cartreads=(\d+)', log)
            profiles = 16 if capacity else 14
            if len(matches) != 1 or int(matches[0][0]) != profiles:
                raise RuntimeError('incomplete cartridge profile inventory')
            for direction in range(4):
                if log.count(f'CART_DMA PASS NTSC={ntsc} mode={direction} bytes=64') != 1:
                    raise RuntimeError('missing bidirectional DMA evidence')
            if log.count(f'TMB PASS NTSC={ntsc} banks=256 modes=4 DMA=16') != 1:
                raise RuntimeError('missing TwoMegabyter banking/mode/DMA evidence')
            rom_ids = (11, 54, 64, 65, 66, 71, 86, 87) if capacity else (11, 54, 64, 65, 66, 71, 86)
            for ident in rom_ids:
                if log.count(f'ROM_MAPPER PASS NTSC={ntsc} id={ident}\n') != 1:
                    raise RuntimeError(f'missing ROM mapper evidence: {ident}')
            for ident in (86, 87):
                if log.count(f'ROM_CAPACITY PASS NTSC={ntsc} id={ident} bytes=2097152 selections=256') != capacity:
                    raise RuntimeError(f'unexpected upper-bank capacity evidence: {ident}')
            for banks in (2, 4):
                if log.count(f'BB8_SIZE PASS NTSC={ntsc} banks={banks}') != 1:
                    raise RuntimeError('missing BB8 size/mirroring evidence')
            if log.count(f'NORMAL_LAYOUT PASS NTSC={ntsc} layouts=4') != 1:
                raise RuntimeError('missing CRT type 0 layout evidence')
            if log.count(f'EASYFLASH PASS NTSC={ntsc} modes=16 journal=3 uci=DE1B') != 1:
                raise RuntimeError('missing EasyFlash mapper/flash evidence')
            receipt['cases'].append({'ntsc': ntsc, 'profiles': profiles, 'checks': int(matches[0][1]),
                                     'dma_bytes': 272, 'cart_reads': int(matches[0][2]),
                                     'rom_mapper_ids': list(rom_ids),
                                     'max_capacity_bytes': 2097152 if capacity else None,
                                     'normal_layouts': 4, 'easyflash_modes': 16})
            print('\n'.join(line for line in log.splitlines() if line.startswith(
                ('CART_DMA', 'TMB PASS', 'ROM_MAPPER', 'ROM_CAPACITY', 'BB8_SIZE', 'NORMAL_LAYOUT',
                 'EASYFLASH', 'PASS CART'))), flush=True)
        if not args.skip_negatives:
            folder = out / 'pal'
            # The scoped controls below need the unmutated scoped run to pass.
            log = run(folder, 'easyflash-only', [vivado / 'xsim.bat', '--nolog', 'coexist_sim', '--runall',
                                                 '--testplusarg', 'easyflash_only'])
            if (log.count('EASYFLASH PASS NTSC=0 modes=16 journal=3 uci=DE1B') != 1 or
                    log.count('EASYFLASH ONLY NTSC=0') != 1 or 'PASS CART COEXIST' in log):
                raise RuntimeError('EasyFlash-only scope did not run exactly that scenario')
            receipt['scoped_positive'] = {'scope': 'easyflash_only', 'passed': True}
            adapter = snap(ROOT / 'hdl/core/c64/multitini_c64_adapter.sv')
            mapper = snap(ROOT / 'hdl/core/c64/ports/cartridge.sv')
            controls = [
                ('blanket_reu_exclusion', adapter, '(!cart_loaded || loader_cart_expansion_policy[0])',
                 '!cart_loaded', 'CART COEXIST REU-compatible FC3 preserves REU/UCI'),
                ('mapper_io2_collision', adapter, '.IOF(cart_iof && !uci_io_select && !reu_io_select && !ua_io_select)',
                 '.IOF(cart_iof && !uci_io_select && !ua_io_select)', 'CART COEXIST FC3 REU native register readback'),
                ('cartridge_data_request_only', adapter, 'assign cart_external_select = cart_loaded &&',
                 'assign cart_external_select = cart_loaded && cart_mem_ce &&', 'CART COEXIST FC3 bank zero ROM read'),
                ('mapper_implicit_state_lifetime', mapper, 'static reg init_n = 0;',
                 'reg init_n = 0;', 'CART COEXIST FC3 DFFF bank switching'),
                ('easyflash_uci_mirror', adapter, '.IOE(cart_ioe && !uci_io_select),',
                 '.IOE(cart_ioe),', 'CART COEXIST EasyFlash UCI masks the $DE1E mode mirror', 'easyflash_only'),
                ('easyflash_jumper_fixed', mapper, 'game_overide  <= data_in[2] ? ~data_in[0] : ~cart_boot;',
                 'game_overide  <= ~data_in[0] & data_in[2];',
                 'CART COEXIST EasyFlash jumper Disable memory configuration', 'easyflash_only')]
            previous_source = None
            # A scoped control runs only the scenario its check lives in.
            for name, source, before, after, failure, *scope in controls:
                original = source.read_text()
                if original.count(before) != 1:
                    raise RuntimeError('negative control anchor: ' + name)
                if previous_source is not None and previous_source != source:
                    run(folder, name + '-restore', [vivado / 'xvlog.bat', '--nolog', '--sv', '--relax', previous_source])
                mutant = folder / (name + '.sv')
                mutant.write_text(original.replace(before, after, 1))
                run(folder, name + '-compile', [vivado / 'xvlog.bat', '--nolog', '--sv', '--relax', mutant])
                run(folder, name + '-elaborate', [vivado / 'xelab.bat', '--nolog', '--relax', '--mt', 'off',
                                                '--timescale', '1ns/1ps', '-s', name, 'profile'])
                plusargs = [arg for item in scope for arg in ('--testplusarg', item)]
                run(folder, name + '-negative', [vivado / 'xsim.bat', '--nolog', name, '--runall', *plusargs],
                    failure)
                receipt['negative_controls'].append({'name': name, 'rejected': True, 'sha256': sha(mutant),
                                                     'scope': scope[0] if scope else 'full'})
                print('NEGATIVE PASS ' + name, flush=True)
                previous_source = source
        receipt['sources_unchanged'] = all(sha(ROOT / p) == digest for p, digest in receipt['source_sha256'].items())
        if not receipt['sources_unchanged']:
            raise RuntimeError('sources changed during validation')
        receipt['limits'] = [
            'Full production adapter, native PLA, cartridge mapper/cache, REU/cache and UCI; synthetic ROM, native CPU-pin injection.',
            'Streamed FC3 subtypes 0/1, Magic Desk, Epyx, CRT type 0 in 8K/16K/split-16K/Ultimax layouts and ROM-only CRT IDs 11/54/64/65/66/71/86/87; no private cartridge code or board acceptance claim.',
            'On PAL, Megabyter and TwoMegabyter each stream the full 2 MiB window and select all 256 register values; smaller images exercise mirroring on both standards.',
            'Delayed bounded memory responders model transactions; physical PSRAM/DDR timing and routed timing are separate gates.',
            'Both video modes use functional 32MHz native/64MHz CPU clocks; no physical-frequency timing signoff.']
        receipt['status'] = 'PASS'
    except Exception as error:
        receipt['error'] = str(error)
        print(receipt['error'], flush=True)
    receipt['artifact_sha256'] = {p.relative_to(out).as_posix(): sha(p) for p in out.rglob('*')
                                 if p.is_file() and 'sources' not in p.relative_to(out).parts and p.suffix in ('.sv', '.log')}
    (out / 'validation.json').write_text(json.dumps(receipt, indent=2) + '\n')
    return receipt['status'] != 'PASS'


if __name__ == '__main__':
    raise SystemExit(main())
