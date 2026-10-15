#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check IIgs native crop, PAL blanking, duplicated rows, and field rejection."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    build=ROOT/'build/apple-iigs-video-tests'
    build.mkdir(parents=True,exist_ok=True)
    runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    compiler=str(runtime/'iverilog.exe') if os.name=='nt' else 'iverilog'
    simulator=str(runtime/'vvp.exe') if os.name=='nt' else 'vvp'
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--'] if os.name=='nt' else []
    sources=['hdl/board/multitini_apple_iigs_video_normalizer.sv',
             'hdl/sim/tb_apple_iigs_video_normalizer.sv']
    tracked=sources+['scripts/test_apple_iigs_video.py']
    hashes={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in tracked}
    image=build/'video.vvp'
    commands=[('compile',[compiler,'-g2012','-Wall','-Wimplicit','-s','tb_apple_iigs_video_normalizer',
                          '-o',str(image)]+[str(ROOT/p) for p in sources]),
              ('run',[simulator,str(image)])]
    for name,command in commands:
        result=subprocess.run(guard+command,cwd=build,stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT,text=True,timeout=120)
        (build/(name+'.log')).write_text(result.stdout)
        if result.returncode or (name=='run' and 'PASS IIgs native video:' not in result.stdout):
            print(result.stdout)
            raise RuntimeError(name+' failed')
        if name=='run':print(result.stdout.strip())
    after={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in tracked}
    if after!=hashes:raise RuntimeError('Video inputs changed during verification')
    (build/'validation.json').write_text(json.dumps({'passed':True,'source_sha256':hashes},indent=2)+'\n')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
