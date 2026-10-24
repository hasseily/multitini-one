#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce local SID/FPGA64 ports from pinned upstream and reviewed patches.

The patches include the pre-existing compatibility/turbo/arbitration changes;
the untouched upstream sources remain the import and licensing authority.
"""
from __future__ import annotations
import argparse,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
COMMIT='62fc697e3c9b5982c9dc76aa8cd03db545d20803'
FILES={'sid_top.sv':'rtl/sid/sid_top.sv','sid_voice.sv':'rtl/sid/sid_voice.sv','fpga64_sid_iec.vhd':'rtl/fpga64_sid_iec.vhd'}
def patched(text,patch):
    original=text.splitlines(keepends=True);lines=patch.splitlines(keepends=True);result=[];cursor=0;i=0
    while i<len(lines):
        if not lines[i].startswith('@@ '):i+=1;continue
        m=re.fullmatch(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@\n',lines[i])
        if not m:raise ValueError('Malformed port hunk')
        start=int(m[1])-1;old_count=int(m[2] or 1);new_count=int(m[4] or 1)
        if old_count==0:start+=1
        if start<cursor:raise ValueError('Overlapping port hunks')
        result+=original[cursor:start];cursor=start;i+=1;old=0;new=0
        while i<len(lines) and not lines[i].startswith('@@ '):
            line=lines[i];i+=1
            if not line or line[0] not in ' +-':raise ValueError('Unexpected patch line')
            if line[0] in ' -':
                if cursor>=len(original) or original[cursor]!=line[1:]:raise ValueError('Pinned port context changed')
                cursor+=1;old+=1
            if line[0] in ' +':result.append(line[1:]);new+=1
        if old!=old_count or new!=new_count:raise ValueError('Port hunk size mismatch')
    return ''.join(result+original[cursor:])
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');a=p.parse_args()
    manifest=json.loads((ROOT/'third_party/c64/import_manifest.json').read_text())
    if manifest['source']['commit']!=COMMIT:raise ValueError('Unexpected C64 pin')
    lock=json.loads((ROOT/'provenance/imports.lock.json').read_text())['imports']
    for name,source in FILES.items():
        path=ROOT/'third_party/c64/upstream'/source
        entries=[e for e in lock if e['path']==path.relative_to(ROOT).as_posix()]
        if len(entries)!=1 or entries[0]['source_path']!=source or entries[0]['sha256']!=hashlib.sha256(path.read_bytes()).hexdigest():raise ValueError('Pinned upstream changed: '+source)
        output=patched(path.read_text(),(ROOT/'third_party/c64/patches'/(name+'.patch')).read_text())
        target=ROOT/'hdl/core/c64/ports'/name
        if a.check:
            if target.read_text()!=output:raise ValueError('Generated port diverges: '+name)
        else:target.write_text(output)
    print('PASS: pinned SID/FPGA64 ports reproduce exactly')
if __name__=='__main__':main()
