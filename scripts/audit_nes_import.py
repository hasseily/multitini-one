#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify every pinned NES source byte and its commercial-use license entry."""
from pathlib import Path
import argparse
import csv
import hashlib
import json
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]

def require(condition, description):
    if not condition:
        raise ValueError(description)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path)
    args=parser.parse_args()
    data=json.loads((ROOT/'third_party/nes/import.json').read_text())
    require(data['commit']=='49a0a662e244469ca77b2155746a066df704ffae', 'NES source pin changed')
    expected={e['path'] for e in data['files']}
    require(len(expected)==len(data['files']), 'Duplicate NES import path')
    require(all(not Path(p).is_absolute() and '..' not in Path(p).parts for p in expected),
            'Unsafe NES import path')
    actual={p.relative_to(ROOT/'third_party/nes/upstream').as_posix()
            for p in (ROOT/'third_party/nes/upstream').rglob('*') if p.is_file()}
    require(actual==expected, f'NES import inventory mismatch: extra {actual-expected}, missing {expected-actual}')
    imports=json.loads((ROOT/'provenance/imports.lock.json').read_text())['imports']
    locked={e['path']:e for e in imports if e['source']=='MiSTer NES'}
    require(len(locked)==len(expected), 'NES import lock count differs')
    sources=json.loads((ROOT/'provenance/sources.lock.json').read_text())['sources']
    pins=[e for e in sources if e['name']=='MiSTer NES']
    require(len(pins)==1 and pins[0]['commit']==data['commit'], 'NES source lock pin differs')
    with (ROOT/'licenses/files.csv').open(newline='',encoding='utf-8') as stream:
        license_rows=list(csv.DictReader(stream))
    if args.source:
        commit=subprocess.check_output(['git','-C',str(args.source),'rev-parse','HEAD'],text=True).strip()
        require(commit==data['commit'], 'NES source checkout pin differs')
        require(not subprocess.check_output(['git','-C',str(args.source),'status','--porcelain'],text=True).strip(),
                'NES source checkout is dirty')
    for entry in data['files']:
        path=ROOT/'third_party/nes/upstream'/entry['path']
        require(hashlib.sha256(path.read_bytes()).hexdigest()==entry['sha256'], f'NES source bytes changed: {path}')
        license='BSD-3-Clause' if '/t65/' in entry['path'] else 'GPL-3.0-only'
        require(entry['license']==license, f'NES source license differs: {path}')
        relative=path.relative_to(ROOT).as_posix()
        rows=[e for e in license_rows if e['path']==relative]
        require(len(rows)==1 and rows[0]['commit']==data['commit'] and rows[0]['license']==license,
                f'NES per-file license row missing or inconsistent: {relative}')
        require(locked.get(relative,{}).get('sha256')==entry['sha256'] and
                locked[relative]['source_path']==entry['path'], f'NES import lock differs: {relative}')
        if args.source:
            require(path.read_bytes()==(args.source/entry['path']).read_bytes(), f'NES checkout source differs: {path}')
        require(path.suffix.lower() not in ('.nes','.fds','.nsf','.bin','.mif','.chr','.rbf','.png','.jpg'),
                f'Excluded binary/media imported: {path}')
    notice=(ROOT/'third_party/nes/T65-NOTICE.txt').read_text()
    require('Neither the name' in notice, 'T65 non-endorsement notice missing')
    for entry in data['files']:
        if '/t65/' in entry['path']:
            require(notice.strip() in (ROOT/'third_party/nes/upstream'/entry['path']).read_text(),
                    'Redistributed T65 notice does not match original source')
    subprocess.run([sys.executable,str(ROOT/'scripts/port_nes_core.py'),'--check'],check=True)
    print(f'PASS NES import: {len(expected)} exact pinned sources; GPL-3.0-only and BSD-3-Clause; no game, ROM or logo content')
    return 0
if __name__=='__main__':raise SystemExit(main())
