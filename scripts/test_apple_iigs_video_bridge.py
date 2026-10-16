#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise IIgs border-raster capture, padded DDR transport and scanout."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/apple-iigs-video-bridge-tests')
    args=parser.parse_args()
    build=args.build_dir.resolve();build.mkdir(parents=True,exist_ok=True)
    runtime=Path(os.environ.get('MULTITINI_UCRT_BIN','C:/msys64/ucrt64/bin'))
    env=os.environ.copy()
    if os.name=='nt':env['VERILATOR_ROOT']=(runtime.parent/'share/verilator').as_posix()
    def tool(name):return str(runtime/(name+'.exe')) if os.name=='nt' else name
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--'] if os.name=='nt' else []
    sources=['third_party/appletini/hdl/cdc_bit_sync.sv','hdl/common/multitini_async_mailbox.sv',
             'hdl/common/multitini_video_capture.sv','hdl/common/multitini_video_frame_reader.sv',
             'hdl/board/multitini_apple_iigs_video_normalizer.sv','hdl/board/multitini_apple_iigs_video_bridge.sv',
             'hdl/sim/video_fifo_model.sv','hdl/sim/xilinx_primitives.sv','hdl/sim/tb_apple_iigs_video_bridge.sv']
    tracked=sources+['scripts/test_apple_iigs_video_bridge.py']
    hashes=lambda:{p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in tracked}
    before=hashes();top='tb_apple_iigs_video_bridge';obj=build/'obj'
    executable=obj/('V'+top+('.exe' if os.name=='nt' else ''))
    commands=[('generate',[tool('verilator_bin') if os.name=='nt' else tool('verilator'),
        '--cc','--exe','--main','--timing','--top-module',top,'-Wno-fatal','--Mdir',str(obj),
        *[str(ROOT/p) for p in sources]]),('compile',None),
        ('ntsc',[str(executable)]),('pal',[str(executable),'+PAL'])]
    for stage,command in commands:
        if command is None:
            if os.name=='nt':
                include=runtime.parent/'share/verilator/include'
                command=[tool('g++'),'-std=c++20','-O2','-pthread','-DVL_TIME_CONTEXT',
                    '-I'+str(include),'-I'+str(include/'vltstd'),'-I'+str(obj),
                    *[str(p) for p in sorted(obj.glob('*.cpp'))],str(include/'verilated.cpp'),
                    str(include/'verilated_timing.cpp'),str(include/'verilated_threads.cpp'),'-o',str(executable)]
            else:command=['make','-C',str(obj),'-f','V'+top+'.mk','-j8']
        result=subprocess.run(guard+command,cwd=build,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                              text=True,errors='replace',timeout=300,env=env)
        (build/(stage+'.log')).write_text(result.stdout,encoding='utf-8')
        if result.returncode or (stage in ('ntsc','pal') and 'PASS IIgs source DDR' not in result.stdout):
            print(result.stdout[-16000:]);raise RuntimeError(stage+' failed')
        if stage in ('ntsc','pal'):print(result.stdout.strip(),flush=True)
    if hashes()!=before:raise RuntimeError('IIgs video bridge inputs changed during verification')
    (build/'validation.json').write_text(json.dumps({'schema':1,'status':'PASS','passed':True,
        'source_sha256':before,'checks':['NTSC and PAL native four-enable cadence',
        '724 image pixels plus44 zero stride samples in every496-row frame',
        'complete frame publication only after all95232 DDR words',
        'every359104 real image pixel observed at reader scanout',
        'bounded four-slot DDR ring and display ownership','modeled AXI backpressure',
        'pause and stopped pixel-clock drain/restart','source reset abort/drain/recovery']},indent=2)+'\n',encoding='utf-8')
    return 0


if __name__=='__main__':raise SystemExit(main())
