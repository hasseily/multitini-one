#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run production serial handlers, diagnostic clock and shared USB pacing delegation."""
import argparse,os,re,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def extract(source,name):
    match=re.search(r"^static\s+(?:void|bool|uint64_t)\s+"+name+r"\([^;]*?\)\s*\{",source,re.M)
    if match is None:raise ValueError("production handler missing: "+name)
    depth=1;end=match.end()
    while depth:
        if source[end]=="{":depth+=1
        elif source[end]=="}":depth-=1
        end+=1
    return source[match.start():end]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cc",default="C:/msys64/ucrt64/bin/gcc.exe" if os.name=="nt" else "cc")
    p.add_argument("--build-dir",type=Path,default=ROOT/"build/iigs-serial-tests")
    a=p.parse_args();out=a.build_dir.resolve();out.mkdir(parents=True,exist_ok=True)
    source=(ROOT/"firmware/platform/xilinx/main.c").read_text()
    (out/"iigs_serial_handlers.inc").write_text("\n\n".join(extract(source,name) for name in
        ("monotonic_us","iigs_transfer_progress","print_iigs_media_status",
         "legacy_c64_media_command","handle_extended_command")))
    binary=out/("test_iigs_serial.exe" if os.name=="nt" else "test_iigs_serial")
    command=[a.cc,"-std=c11","-Wall","-Wextra","-Werror","-I"+str(out),"-I"+str(ROOT/"firmware/include"),
        "-I"+str(ROOT/"firmware/platform/xilinx"),str(ROOT/"tests/host/test_iigs_serial.c"),"-o",str(binary)]
    env=os.environ.copy()
    if os.name=="nt":env["PATH"]="C:/msys64/ucrt64/bin"+os.pathsep+env.get("PATH","")
    for cmd in (command,[str(binary)]):
        result=subprocess.run(cmd,cwd=ROOT,env=env)
        if result.returncode:return result.returncode
    return 0
if __name__=="__main__":raise SystemExit(main())
