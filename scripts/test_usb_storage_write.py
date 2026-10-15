#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the production MSC final-write handler against durability failures."""
import argparse,os,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def function(text,name):
    start=re.search(r'^static\s+(?:void|int)\s+'+name+r'\([^;]*?\)\s*\{',text,re.M)
    assert start is not None,name
    depth=1;end=start.end()
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start.start():end]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc',default='C:/msys64/ucrt64/bin/gcc.exe' if os.name=='nt' else 'cc')
    p.add_argument('--output',type=Path,default=ROOT/'build/usb-storage-write')
    a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    source=(ROOT/'firmware/platform/xilinx/xusbps_class_storage.c').read_text()
    (out/'usb_storage_handlers.inc').write_text('\n'.join(function(source,name) for name in
        ('StorageResetPending','SendStatus','StorageHandleWriteData','StorageOpcodeNeedsDefer')))
    binary=out/('test.exe' if os.name=='nt' else 'test')
    command=[a.cc,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(out),str(ROOT/'tests/host/test_usb_storage_write.c'),'-o',str(binary)]
    env=os.environ.copy()
    if os.name=='nt':env['PATH']='C:/msys64/ucrt64/bin'+os.pathsep+env.get('PATH','')
    for c in (command,[str(binary)]):
        result=subprocess.run(c,cwd=ROOT,env=env)
        if result.returncode:return result.returncode
    return 0
if __name__=='__main__':raise SystemExit(main())
