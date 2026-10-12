#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check native DDR reader scaling, buffer ownership, faults, and reset drain."""
from pathlib import Path
import hashlib
import json
import os
import argparse
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dynamic-display", action="store_true")
    parser.add_argument("--case", action="append", help="Run a named geometry case only")
    args=parser.parse_args()
    build = ROOT / "build/video-frame-reader-tests" / ("dynamic-display" if args.dynamic_display else "static")
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    compiler = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    simulator = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    sources = ["hdl/sim/xilinx_primitives.sv", "hdl/common/multitini_async_mailbox.sv",
               "hdl/common/multitini_video_frame_reader.sv", "hdl/sim/tb_video_frame_reader.sv"]
    cases=[{"name":f"auto-{n}-{d}","SCALE_NUM":n,"SCALE_DEN":d}
           for n,d in (((1,1),(2,1),(3,1),(4,1),(5,1),(6,1),(3,2)) if args.dynamic_display else ((4,1),(6,1),(3,2)))]
    if args.dynamic_display:
        cases += [{"name":"zero-1","SCALE_NUM":1,"SCALE_DEN":1,"VIEW_X":0,"VIEW_Y":0,"H_ACTIVE":67,"V_ACTIVE":9}]
        for n,d,x,y,w,h in ((1,1,-13,-3,42,6),(2,1,-17,-5,97,13),(3,1,-22,-4,143,21),
                            (4,1,-35,-7,199,28),(5,1,-41,-13,231,29),(6,1,-50,-17,269,35),(3,2,-17,-3,70,9)):
            cases.append({"name":f"crop-{n}-{d}","SCALE_NUM":n,"SCALE_DEN":d,"VIEW_X":x,"VIEW_Y":y,"H_ACTIVE":w,"V_ACTIVE":h})
        cases += [{"name":f"margin-{x}","SCALE_NUM":3,"SCALE_DEN":2,"VIEW_X":x,"VIEW_Y":0,"H_ACTIVE":104,"V_ACTIVE":13} for x in (1,2,3)]
        cases += [{"name":"wide-crop-4","SCALE_NUM":4,"SCALE_DEN":1,"WIDTH":640,"HEIGHT":19,
                   "VIEW_X":-117,"VIEW_Y":-13,"H_ACTIVE":511,"V_ACTIVE":55}]
    if args.case:
        known={case["name"] for case in cases}
        if set(args.case)-known:
            parser.error("Unknown case(s): "+", ".join(sorted(set(args.case)-known)))
        cases=[case for case in cases if case["name"] in args.case]
    hashed=sources+["scripts/test_video_frame_reader.py","scripts/run_native_tool.py"]
    hashes=lambda:{name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in hashed}
    before=hashes();logs=[];passed=False
    def run(command,name):
        result=subprocess.run(guard+command,cwd=build,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors="replace")
        (build/(name+".log")).write_text(result.stdout,encoding="utf-8")
        logs.append({"command":command,"returncode":result.returncode,"log":name+".log","output":result.stdout})
        if result.returncode or re.search(r"\b(?:FATAL|ERROR):",result.stdout):
            print(result.stdout)
            raise RuntimeError(name+" failed")
        if name.endswith("-run"):print(result.stdout.strip())
    try:
        for case in cases:
            output=build/(case["name"]+".vvp")
            parameters={"DYNAMIC_DISPLAY":int(args.dynamic_display),"SHARED_PIXEL_RESET":int(args.dynamic_display),
                        "WIDTH":67 if args.dynamic_display else 68,**{name:value for name,value in case.items() if name!="name"}}
            command=[compiler,"-g2012","-Wall","-Wimplicit","-s","tb_video_frame_reader"]
            for name,value in parameters.items():command += ["-P",f"tb_video_frame_reader.{name}={value}"]
            run(command+["-o",str(output)]+[str(ROOT/path) for path in sources],case["name"]+"-compile")
            run([simulator,str(output)],case["name"]+"-run")
        passed=True
    finally:
        after=hashes();changed=[name for name in before if before[name]!=after[name]]
        (build/"validation.json").write_text(json.dumps({"passed":passed and not changed,"dynamic_display":args.dynamic_display,
            "cases":cases,"source_sha256":before,"source_sha256_after":after,"changed_sources":changed,"logs":logs},indent=2)+"\n",encoding="utf-8")
    if changed:raise RuntimeError("Reader sources changed during validation: "+", ".join(changed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
