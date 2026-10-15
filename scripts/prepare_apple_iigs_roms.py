#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prepare user-supplied Apple ROMs for the runtime loader. No downloads.

Accept already ordered binary system ROMs or the upstream MAME bank layout.
Auxiliary ROMs accept binary or one-byte-per-token hexadecimal text. Generated
files and their private-input hashes stay outside version control. Copy the
prepared ROMs to multitini/cores/apple-iigs/roms on the SD card; disks and PRAM
belong under the separate multitini/cores/apple-iigs/media directory.
"""
import argparse,hashlib,json,re,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(data):return hashlib.sha256(data).hexdigest()
def auxiliary(path,size,duplicate=False):
    data=path.read_bytes()
    if path.suffix.lower() in ('.hex','.mem'):
        tokens=data.decode('ascii').split()
        if not tokens or any(re.fullmatch('[0-9a-fA-F]{2}',t) is None for t in tokens):
            raise ValueError(f'{path}: expected one byte per hexadecimal token')
        data=bytes(int(t,16) for t in tokens)
    if duplicate and size==4096 and len(data)==2048:data*=2
    if len(data)!=size:raise ValueError(f'{path}: expected {size} bytes, received {len(data)}')
    return data
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,
                   default=ROOT/'build/apple-iigs-user-roms/sd/multitini/cores/apple-iigs/roms',
                   help='Private ROM directory under build/; defaults to the SD ROM layout')
    p.add_argument('--mame-zip',type=Path)
    for name in ('rom3','rom1','char','adb3','adb1'):p.add_argument('--'+name,type=Path)
    args=p.parse_args();out=args.output.resolve()
    if not out.is_relative_to(ROOT/'build'):p.error('Private ROM output must be under this checkout build/')
    result={};inputs={};mame_members={}
    if args.mame_zip:
        inputs[str(args.mame_zip.resolve())]=digest(args.mame_zip.read_bytes())
        with zipfile.ZipFile(args.mame_zip) as archive:
            def member(name,length,optional=False):
                candidates=[s for s in archive.namelist() if Path(s).name==name]
                if not candidates and optional:return None
                if len(candidates)!=1:raise ValueError('Missing or ambiguous MAME bank: '+name)
                data=archive.read(candidates[0])
                if len(data)!=length:raise ValueError('Wrong MAME bank length: '+name)
                mame_members[candidates[0]]={'bytes':len(data),'sha256':digest(data)}
                return data
            a=member('341-0728',131072);b=member('341-0748',131072)
            result['rom3.rom']=a+b[65536:]+b[:65536]
            if any(Path(s).name=='342-0077-b' for s in archive.namelist()):result['rom1.rom']=member('342-0077-b',131072)
            character=member('344s0047.bin',16384,optional=True)
            if character is not None:
                # The pinned core's US KEGS font uses the first MegaII bank,
                # ignores bit 7, and duplicates the 2 KiB font in both halves.
                result['char.rom']=bytes(value & 0x7f for value in character[:2048])*2
            adb3=member('341s0632-2.bin',4096,optional=True)
            if adb3 is not None:result['adb3.rom']=adb3
            adb1=member('341s0345.bin',3072,optional=True)
            if adb1 is not None:result['adb1.rom']=bytes(1024)+adb1
    for name,length in (('rom3',262144),('rom1',131072),('char',4096),('adb3',4096),('adb1',4096)):
        path=getattr(args,name)
        if path:
            data=path.read_bytes() if name.startswith('rom') else auxiliary(path,length,name=='char')
            if len(data)!=length:raise ValueError(f'{name}: expected {length} bytes')
            result[name+'.rom']=data;inputs[str(path.resolve())]=digest(path.read_bytes())
    if not result:p.error('Supply a system/auxiliary ROM or a user-owned MAME ZIP')
    out.mkdir(parents=True,exist_ok=True)
    for name,data in result.items():
        target=out/name
        if target.exists() and target.read_bytes()!=data:raise ValueError('Refusing to replace different ROM: '+str(target))
        target.write_bytes(data)
    (out/'private-rom-provenance.json').write_text(json.dumps({'inputs':inputs,'mame_members':mame_members,'outputs':{n:{'bytes':len(d),'sha256':digest(d)} for n,d in result.items()},'runtime_directory':'multitini/cores/apple-iigs/roms','distribution':'User-supplied private inputs; excluded from hardware bundle.'},indent=2)+'\n')
    print(f'Prepared {len(result)} private runtime ROMs in {out}')
    return 0
if __name__=='__main__':raise SystemExit(main())
