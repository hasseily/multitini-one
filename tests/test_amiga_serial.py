#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute production headless core-selection and Amiga serial dispatch."""
from pathlib import Path
import argparse,os,subprocess,sys
ROOT=Path(__file__).resolve().parents[1]
def extract(source,name):
    begin=source.index("static bool "+name+"(const char *line)\n{")
    return source[begin:source.index("\n}",begin)+2]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',required=True)
    parser.add_argument('--build-dir',type=Path,default=ROOT/'build/amiga-serial-tests')
    args=parser.parse_args();out=args.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'firmware/platform/xilinx/main.c').read_text()
    (out/'amiga_serial.inc').write_text('\n\n'.join(extract(source,n) for n in ('amiga_serial_command','supervisor_serial_command')))
    binary=out/('test_amiga_serial.exe' if os.name=='nt' else 'test_amiga_serial')
    environment=os.environ.copy()
    if os.name=='nt':environment['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+environment.get('PATH','')
    guard=[sys.executable,str(ROOT/'scripts/run_native_tool.py'),'--']
    compile=[args.cc,'-std=c11','-Wall','-Wextra','-Werror','-Wpedantic','-I'+str(out),
             '-I'+str(ROOT/'firmware/include'),'-I'+str(ROOT/'firmware/platform/xilinx'),
             str(ROOT/'tests/host/test_amiga_serial.c'),'-o',str(binary)]
    for command in (compile,[str(binary)]):
        result=subprocess.run(guard+command,cwd=ROOT,env=environment)
        if result.returncode:return result.returncode
    return 0
if __name__=='__main__':raise SystemExit(main())
