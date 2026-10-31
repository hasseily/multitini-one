#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run actual native ECS PAL timing through the production video normalizer."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
from test_amiga_ports import ROOT,machine_sources


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,default=ROOT / "build/amiga-native-video")
    parser.add_argument("--vivado",type=Path,default=Path("C:/Xilinx/2025.2/Vivado"))
    args=parser.parse_args()
    build=args.build_dir.resolve()
    if not build.is_relative_to(ROOT / "build"):raise SystemExit("Build directory must remain inside this repository")
    build.mkdir(parents=True,exist_ok=True)
    vhdlbase=ROOT / "third_party/amiga/upstream/rtl/tg68k"
    vhdl=[vhdlbase / p for p in ("TG68K_Pack.vhd","TG68K_ALU.vhd","TG68K_MMU030.vhd","TG68KdotC_Kernel.vhd","TG68K.vhd")]
    sources=machine_sources()+[ROOT / "hdl/sim/tb_amiga_ports.sv",ROOT / "hdl/board/multitini_amiga_video_normalizer.sv",ROOT / "hdl/sim/tb_amiga_native_video.sv"]
    assets=[ROOT / "hdl/core/amiga/ports/m68k_cyc_idx.hex",ROOT / "hdl/core/amiga/ports/m68k_ea_idx.hex",ROOT / "third_party/amiga/upstream/rtl/fx68k/microrom.mem",ROOT / "third_party/amiga/upstream/rtl/fx68k/nanorom.mem"]
    for p in assets:shutil.copyfile(p,build / p.name)
    receipt={"schema_version":1,"board_evidence":False,"purpose":"Native ECS PAL sync to complete640x512 normalized canvases; CPU held reset; no system ROM",
             "startup_model":"The 19 unreset beam-counter FFs are initialized to FPGA INIT=0 in the fixture; sync warmup counters are tracked separately from two complete steady fields",
             "sources":{p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in vhdl+sources+assets}}
    (build / "inputs.json").write_text(json.dumps(receipt,indent=2)+"\n")
    def run(name,argv):
        result=subprocess.run([sys.executable,str(ROOT / "scripts/run_native_tool.py"),"--cwd",str(build),"--","cmd.exe","/d","/c",str(args.vivado / "bin" / f"{name}.bat"),*map(str,argv)],capture_output=True,text=True,errors="replace")
        (build / f"{name}.stdout.log").write_text(result.stdout+result.stderr)
        if result.returncode:
            print((result.stdout+result.stderr)[-10000:]);raise SystemExit(result.returncode)
        print(f"{name}: passed",flush=True)
    run("xvhdl",["--2008","--work","work",*vhdl])
    run("xvlog",["--sv","--work","work","-i",ROOT / "third_party/amiga/upstream/rtl",*sources])
    run("xelab",["work.tb_amiga_native_video","-s","amiga_native_video","--debug","typical","--timescale","1ns/1ps","--mt","auto"])
    run("xsim",["amiga_native_video","--runall"])
    log=(build / "xsim.stdout.log").read_text()
    if "PASS native PAL ECS timing" not in log or "Fatal:" in log:raise SystemExit(f"Native video failed; see {build / 'xsim.stdout.log'}")
    for name,expected in receipt["sources"].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest()!=expected:raise SystemExit(f"Input changed during native video test: {name}")
    receipt["result"]="passed"
    (build / "report.json").write_text(json.dumps(receipt,indent=2)+"\n")
    print("PASS actual native PAL video timing and complete canvas output")
    return 0


if __name__=="__main__":raise SystemExit(main())
