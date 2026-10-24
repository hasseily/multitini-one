#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute unchanged production reset/storage handoff functions with owned mocks."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
NAMES=("quiesce_c64_directory","resume_c64_directory","set_core_reset_request",
       "prepare_c64_content_change","finish_c64_content_change","mount_c64_disk",
       "supervisor_quiesce","usb_sd_quiesce")

def extract(source,name):
    found=re.search(r"^static\s+(?:int|bool)\s+"+name+r"\([^;]*?\)\s*\{",source,re.M)
    if not found: raise RuntimeError("missing production function: "+name)
    depth=1;end=found.end()
    while depth:
        if source[end]=="{": depth+=1
        elif source[end]=="}": depth-=1
        end+=1
    return source[found.start():end]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--cc",default="C:/msys64/ucrt64/bin/gcc.exe" if os.name=="nt" else "cc")
    p.add_argument("--build-dir",type=Path,default=ROOT/"build/c64-iec-lifecycle")
    a=p.parse_args();out=a.build_dir.resolve()
    if out==ROOT/"build" or not out.is_relative_to(ROOT/"build"):p.error("output must be a child of build/")
    out.mkdir(parents=True,exist_ok=True)
    paths=[ROOT/"firmware/platform/xilinx/main.c",Path(__file__).resolve(),ROOT/"tests/host/test_c64_iec_lifecycle.c"]
    hashes={str(x.relative_to(ROOT)):hashlib.sha256(x.read_bytes()).hexdigest() for x in paths}
    source=paths[0].read_text()
    (out/"c64_iec_lifecycle.inc").write_text("\n\n".join(extract(source,n) for n in NAMES))
    binary=out/("test_c64_iec_lifecycle.exe" if os.name=="nt" else "test_c64_iec_lifecycle")
    commands=[[a.cc,"-std=c11","-Wall","-Wextra","-Werror","-I"+str(out),"-I"+str(ROOT/"firmware/include"),str(paths[2]),"-o",str(binary)],[str(binary)]]
    receipt={"status":"FAIL","source_sha256":hashes,"functions":list(NAMES),"scope":"Extracted production orchestration; mocked storage/MMIO, not physical SD or IEC timing."}
    try:
        for i,command in enumerate(commands):
            run=subprocess.run([sys.executable,str(ROOT/"scripts/run_native_tool.py"),"--",*command],cwd=ROOT,capture_output=True,text=True)
            (out/("compile.log" if i==0 else "run.log")).write_text(run.stdout+run.stderr)
            if run.returncode:raise RuntimeError(run.stdout+run.stderr)
        result=(out/"run.log").read_text();found=re.search(r"PASS C64 IEC lifecycle: (\d+) scenarios",result)
        if not found:raise RuntimeError("missing execution completion")
        receipt["scenarios"]=int(found.group(1))
        receipt["sources_unchanged"]=all(hashlib.sha256(x.read_bytes()).hexdigest()==hashes[str(x.relative_to(ROOT))] for x in paths)
        if not receipt["sources_unchanged"]:raise RuntimeError("production sources changed during test")
        receipt["status"]="PASS";print(result.strip())
    finally:
        receipt["artifacts"]={x.name:hashlib.sha256(x.read_bytes()).hexdigest() for x in out.iterdir() if x.name in ("compile.log","run.log","c64_iec_lifecycle.inc")}
        (out/"validation.json").write_text(json.dumps(receipt,indent=2)+"\n")
    return 0

if __name__=="__main__":raise SystemExit(main())
