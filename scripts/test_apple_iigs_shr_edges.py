#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify SHR image/borders with real native timing, VGC and source normalizer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/apple-iigs-shr-edge-tests')
    parser.add_argument('--vgc-source',type=Path,help='Explicit derivative fixture for negative controls')
    args=parser.parse_args();build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    env=os.environ.copy();env['PATH']=str(runtime)+os.pathsep+env.get('PATH','')
    env['VERILATOR_ROOT']=(runtime.parent/'share/verilator').as_posix()
    top='tb_apple_iigs_shr_edges';obj=build/'obj'
    paths=['hdl/sim/'+top+'.sv','hdl/board/multitini_apple_iigs_video_normalizer.sv',
           'hdl/core/apple_iigs/ports/vgc.sv','hdl/core/apple_iigs/ports/runtime_rom.sv',
           'third_party/apple-iigs/upstream/rtl/video_timing.v']
    if args.vgc_source:paths[2]=str(args.vgc_source.resolve())
    captured=paths+['third_party/apple-iigs/upstream/rtl/dhr_lut.vh','scripts/port_apple_iigs_core.py',
                    'scripts/test_apple_iigs_shr_edges.py']
    def hashes():return {p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in captured}
    before=hashes()
    def run(command,name):
        result=subprocess.run([str(p).replace('\\','/') for p in command],cwd=build,env=env,
                              stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        (build/(name+'.log')).write_text(result.stdout)
        if result.returncode or re.search(r'%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed',result.stdout):
            raise RuntimeError(result.stdout[-12000:])
        return result.stdout
    win=os.name=='nt'
    run([runtime/('verilator_bin.exe' if win else 'verilator'),'--cc','--exe','--main','--timing',
         '-Wno-fatal','--top-module',top,'-I'+str(ROOT/'third_party/apple-iigs/upstream/rtl'),
         '--Mdir',obj,*[ROOT/p for p in paths]],'generate')
    executable=obj/('V'+top+('.exe' if win else ''))
    if win:
        inc=runtime.parent/'share/verilator/include'
        run([runtime/'g++.exe','-std=c++20','-O1','-pthread','-DVL_TIME_CONTEXT','-I'+str(inc),
             '-I'+str(inc/'vltstd'),'-I'+str(obj),*sorted(obj.glob('*.cpp')),inc/'verilated.cpp',
             inc/'verilated_timing.cpp',inc/'verilated_threads.cpp','-o',executable],'compile')
    else:run(['make','-C',obj,'-f','V'+top+'.mk','-j8'],'compile')
    output=run([executable],'simulate')
    if 'PASS IIgs SHR edges:' not in output:raise RuntimeError('No SHR PASS marker')
    if hashes()!=before:raise RuntimeError('SHR inputs changed during verification')
    (build/'validation.json').write_text(json.dumps({'passed':True,'source_sha256':before,
        'frames':6,'pixels':2285568,'real_pixels':2154624,'active_pixels':1536000,
        'stride_padding_pixels':130944,'checks':['NTSC/PAL320/640/fill','all640imagecolumns',
        'distinct palette ramp','nativeC034/BORDERCOLOR palette all4bands with perrawline changes',
        '42pixels eachside,24source lines top/bottom','NTSC494wrap/PAL232 consecutive topborder',
        'RGB border retained during nativeblanking','44zero stridepadding samples eachrow',
        'native912pixels and262/312lines unchanged','one-pixel synchronous video RAM',
        'production normalizer RGB565 and coordinates'],
        'limits':['BORDERCOLOR is driven at the nativeVGC port; CPU C034 decoding is a separate integration path. No hardware acceptance implied.']},indent=2)+'\n')
    print(output.strip())


if __name__=='__main__':main()
