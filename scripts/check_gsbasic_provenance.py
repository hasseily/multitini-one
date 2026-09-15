#!/usr/bin/env python3
"""Verify every unmodified GS BASIC import against its pinned Git blob."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODIFIED = {
    'BuiltInFunctions.cpp', 'CallSpec.cpp', 'Executive.cpp', 'Executive.h', 'Expression.cpp',
    'Expression.h', 'GSBCommon.h', 'GSBString.h', 'GSBString.cpp', 'Statements.cpp',
    'Statements.h', 'Token.cpp', 'Value.h', 'vio.h', 'VectrexSound.cpp', 'VectrexSound.h',
}

def main():
    manifest = json.loads((ROOT/'third_party/gsbasic/UPSTREAM.json').read_text())
    assert manifest['commit'] == '457c5a98f914a812c9341c2949e9d57473ccac4a'
    count = 0
    for entry in manifest['files']:
        path = ROOT/entry['path']
        data = path.read_bytes()
        if path.name in MODIFIED:
            continue
        blob = b'blob ' + str(len(data)).encode() + b'\0' + data
        if hashlib.sha1(blob).hexdigest() != entry['git_blob_sha1']:
            raise SystemExit(f'Unexpected change to imported file: {entry["path"]}')
        count += 1
    print(f'{count} exact upstream blobs verified; {len(MODIFIED)} documented port files')

if __name__ == '__main__':
    main()
