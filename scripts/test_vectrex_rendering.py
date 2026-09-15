#!/usr/bin/env python3
"""Compile and run original beam/renderer regressions without desktop dialogs."""
from pathlib import Path
import os
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/"build"/"vectrex-rendering"
BUILD.mkdir(parents=True,exist_ok=True)
RUNTIME=Path(os.environ.get("MULTITINI_UCRT_BIN","C:/msys64/ucrt64/bin"))
IVERILOG=str(RUNTIME/"iverilog.exe") if os.name=="nt" else "iverilog"
VVP=str(RUNTIME/"vvp.exe") if os.name=="nt" else "vvp"
for leaf in ("beam","renderer","video_bridge"):
    top=f"tb_vectrex_{leaf}"
    image=BUILD/f"{top}.vvp"
    command=[sys.executable,str(ROOT/"scripts/run_native_tool.py"),"--",
             IVERILOG,"-g2012","-s",top,"-o",str(image)]
    if leaf=="video_bridge":
        command += [str(ROOT/"third_party/appletini/hdl/cdc_bit_sync.sv"),
                    str(ROOT/"hdl/board/multitini_vectrex_video_bridge.sv")]
    else:
        command += [str(ROOT/f"hdl/core/vectrex/multitini_vectrex_{leaf}.sv")]
    command += [str(ROOT/f"hdl/sim/{top}.sv")]
    subprocess.run(command,check=True)
    subprocess.run([sys.executable,str(ROOT/"scripts/run_native_tool.py"),"--",
                    VVP,str(image)],check=True)
