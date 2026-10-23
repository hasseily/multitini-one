#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pin and verify the test-only VICE 3.10 source subset."""
from __future__ import annotations
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
REFERENCE = ROOT / 'third_party/vice_reference'
COMMIT = '4d283a2e7dd59b7e378524878e81ecc7826b700c'


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_reference() -> dict:
    manifest = json.loads((REFERENCE / 'import_manifest.json').read_text(encoding='utf-8'))
    if (manifest.get('commit') != COMMIT or manifest.get('tag') != '3.10.0'
            or manifest.get('repository') != 'https://github.com/VICE-Team/svn-mirror'):
        raise ValueError('VICE reference pin differs from the accepted release')
    root = (REFERENCE / 'upstream').resolve()
    seen = set()
    for entry in manifest['files']:
        relative = Path(entry['path'])
        path = (root / relative).resolve()
        if (relative.is_absolute() or not path.is_relative_to(root)
                or entry['path'] in seen or digest(path) != entry['sha256']):
            raise ValueError('Invalid or changed VICE reference: ' + entry['path'])
        seen.add(entry['path'])
    if not seen:
        raise ValueError('Empty VICE reference manifest')
    return manifest


if __name__ == '__main__':
    print('PASS VICE 3.10 source pin:', len(verify_reference()['files']), 'files')
