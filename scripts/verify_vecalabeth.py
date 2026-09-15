#!/usr/bin/env python3
"""Run the user's external Vecalabeth without copying or modifying its files."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

def hashes(directory):
    return {path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(directory.iterdir()) if path.is_file()}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path, help='External Vecalabeth directory')
    parser.add_argument('runner', type=Path, help='Built vectrex32_host executable')
    parser.add_argument('--output', type=Path, required=True, help='Generated report directory')
    parser.add_argument('--bios', type=Path, help='Optional external 8192-byte BIOS for exact raster glyph data')
    args = parser.parse_args()
    game = args.game.resolve(strict=True)
    source = game/'vec.bas'
    if not source.is_file():
        parser.error('The game directory must contain vec.bas')
    output = args.output.resolve()
    if output == game or game in output.parents:
        parser.error('Output must be outside the game directory')
    output.mkdir(parents=True, exist_ok=True)
    before = hashes(game)
    bios_args = []
    bios_record = None
    if args.bios:
        bios_path = args.bios.resolve(strict=True)
        bios_data = bios_path.read_bytes()
        if len(bios_data) != 8192:
            parser.error('The external BIOS must contain exactly 8192 bytes')
        bios_record = {'path': str(bios_path), 'sha256': hashlib.sha256(bios_data).hexdigest()}
        bios_args = ['--bios', str(bios_path)]
    completed = subprocess.run([sys.executable, str(Path(__file__).with_name('run_native_tool.py')), '--',
                                str(args.runner.resolve(strict=True)), str(source),
                                '2000', str(output/'frame-2000.svg')] + bios_args,
                               capture_output=True, text=True, timeout=60)
    log = completed.stdout + completed.stderr
    (output/'run.log').write_text(log, encoding='utf-8')
    exploration = subprocess.run([sys.executable, str(Path(__file__).with_name('run_native_tool.py')), '--',
                                  str(args.runner.resolve(strict=True)), str(source), '5000',
                                  str(output/'exploration.svg'), 'explore'] + bios_args,
                                 capture_output=True, text=True, timeout=60)
    exploration_log=exploration.stdout+exploration.stderr
    (output/'exploration.log').write_text(exploration_log,encoding='utf-8')
    after = hashes(game)
    if before != after:
        raise SystemExit('External game files changed')
    if bios_record and hashlib.sha256(bios_path.read_bytes()).hexdigest() != bios_record['sha256']:
        raise SystemExit('External BIOS changed')
    match = re.search(r'status=(\d+) frames=(\d+) statements=(\d+) lines=(\d+) assets=(\d+) psg=(\d+).*stopped=(\d+) error_line=(\d+) error=(.*)', log)
    if completed.returncode or not match:
        raise SystemExit(f'Game execution failed; see {output / "run.log"}')
    status, frames, statements, lines, assets, psg, stopped, error_line = map(int, match.groups()[:8])
    if status or frames != 2000 or statements < 50000 or lines < 200000 or assets < 2 or not stopped or error_line or match[9]:
        raise SystemExit(f'Game execution did not meet the validation gate: {match[0]}')
    if 'asset=SPLASH' not in log or 'asset=MOUNTAIN' not in log:
        raise SystemExit('Replay did not reach both title and outdoor assets')
    if exploration.returncode or 'exploration entered=1 exited=1 attacks=1 damage=1 entries=2' not in exploration_log:
        raise SystemExit(f'Dungeon replay failed; see {output / "exploration.log"}')
    if bios_record and ('raster_font=vector_fallback' in log or 'raster_font=vector_fallback' in exploration_log):
        raise SystemExit('Supplied BIOS was not used for raster glyphs')
    for name in ('dungeon-entry.svg','combat.svg','dungeon-exit.svg'):
        if not (output/name).is_file():
            raise SystemExit(f'Missing replay capture: {name}')
    report = {'external_game': str(game), 'sha256': before, 'files_unchanged': True,
              'frames': frames, 'statements': statements, 'lines': lines,
              'asset_opens': assets, 'psg_writes': psg, 'callback_stop': bool(stopped),
              'dungeon_entry_exit': True, 'combat_damage': True,
              'raster_font': 'vector_fallback' if 'raster_font=vector_fallback' in log else 'bios_or_unused',
              'bios': bios_record,
              'scope': 'Title/setup/outdoors, dungeon entry/exit and monster damage; no board or full-game completion claim'}
    (output/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({key: value for key, value in report.items() if key not in ('sha256', 'external_game')}))
    print(f'Hashed {len(before)} external files; report: {output / "report.json"}')

if __name__ == '__main__':
    main()
