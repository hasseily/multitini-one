#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Assemble original C64 Ultimate Audio board probes and synthetic data."""
from __future__ import annotations
import argparse,hashlib,json,math,shutil,struct,subprocess
from pathlib import Path

HERE=Path(__file__).resolve().parent
ROOT=HERE.parent

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/c64ua')
    parser.add_argument('--acme',type=Path)
    parser.add_argument('--check',action='store_true',help='Reassemble and compare with the existing local build receipt')
    args=parser.parse_args();build=args.build_dir.resolve()
    if not build.is_relative_to(ROOT/'build') or build==ROOT/'build':parser.error('Use dedicated build child')
    previous=None
    if args.check:
        previous=json.loads((build/'validation.json').read_text())
        for name,entry in previous['outputs'].items():
            if hashlib.sha256((build/name).read_bytes()).hexdigest()!=entry['sha256']:raise RuntimeError('Existing output digest differs: '+name)
        build=build/'check'
    acme=args.acme or shutil.which('acme') or Path.home()/'tools/acme/acme.exe'
    version=subprocess.run([str(acme),'--version'],capture_output=True,text=True,check=True).stdout.strip()
    if '0.97' not in version:raise RuntimeError('ACME 0.97 required: '+version)
    work=build/'work';work.mkdir(parents=True,exist_ok=True)
    sources=sorted([*HERE.glob('*.a'),*HERE.glob('*.inc')])
    for source in sources:shutil.copyfile(source,work/source.name)
    tones=bytearray()
    for harmonic in (16,24,32,40,48,64,96):
        for frame in range(1024):
            sample=round(12000*math.sin(2*math.pi*harmonic*frame/1024))
            tones+=struct.pack('<hh',sample,sample)
    (work/'tones.bin').write_bytes(tones)
    outputs=[]
    for source,name,rate,mode in [('detect.a','detect.prg',130,0),('background.a','background-48k.prg',130,0),
                             ('background.a','background-cap.prg',0,0),('period.a','period.prg',130,0),
                             ('period.a','period16.prg',130,16),('stress.a','stress.prg',130,0)]:
        output=build/name
        child=subprocess.run([str(acme),'-DRATE='+str(rate),'-DMODE='+str(mode),'--format','cbm','--outfile',str(output),
                              '--symbollist',str(build/(name+'.sym')),source],cwd=work,capture_output=True,text=True)
        if child.returncode:raise RuntimeError(child.stdout+child.stderr)
        data=output.read_bytes()
        if data[:2]!=b'\x01\x08':raise RuntimeError('Wrong PRG load address')
        outputs.append(output)
    preload=build/'synthetic-1m.reu'
    preload.write_bytes(bytes(((i*73)^(i>>8)^(i>>16)^0xa5)&255 for i in range(1024*1024)))
    outputs.append(preload)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    receipt={'status':'PASS','assembler':version,'source_sha256':{p.relative_to(ROOT).as_posix():sha(p) for p in [Path(__file__),*sources]},
             'outputs':{p.name:{'bytes':p.stat().st_size,'sha256':sha(p)} for p in outputs},
             'limits':'Original synthetic assets; assembly and PRG layout only. Physical C64 board execution pending.'}
    if previous is not None and receipt['outputs']!=previous['outputs']:raise RuntimeError('Rebuilt PRGs or synthetic data differ')
    (build/'validation.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print('PASS: assembled',len(outputs)-1,'C64 audio probes and synthetic 1MiB preload')

if __name__=='__main__':main()
