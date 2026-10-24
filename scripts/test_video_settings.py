#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused video/register regressions; all native children use the safe launcher."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/video-settings-tests"
BUILD.mkdir(parents=True, exist_ok=True)
UCRT = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
IVERILOG = str(UCRT / "iverilog.exe") if os.name == "nt" else "iverilog"
VVP = str(UCRT / "vvp.exe") if os.name == "nt" else "vvp"
GUARD = [sys.executable, "-u", str(ROOT / "scripts/run_native_tool.py"), "--"]
SOURCES = ["hdl/generated/multitini_regs_pkg.sv", "hdl/common/multitini_display_pkg.sv",
           "hdl/common/multitini_display_geometry.sv", "hdl/sim/xilinx_primitives.sv",
           "third_party/appletini/hdl/cdc_bit_sync.sv",
           "hdl/common/multitini_async_mailbox.sv", "hdl/common/multitini_border_config.sv",
           "hdl/common/multitini_control_regs.sv", "hdl/board/multitini_video_test.sv",
           "hdl/board/multitini_c64_video_bridge.sv", "hdl/board/multitini_gameboy_video_bridge.sv",
           "hdl/board/multitini_vectrex_video_bridge.sv",
           "hdl/core/vectrex/multitini_vectrex_compositor.sv"]

def run(name, parameters=()):
    output = BUILD / (name + "".join(parameters).replace("=", "_") + ".vvp")
    command = [IVERILOG, "-g2012", "-Wall", "-Wimplicit", "-s", name, "-o", str(output)]
    command += ["-P" + name + "." + item for item in parameters]
    command += [str(ROOT / path) for path in SOURCES + ["hdl/sim/" + name + ".sv"]]
    subprocess.run(GUARD + command, cwd=BUILD, check=True)
    subprocess.run(GUARD + [VVP, str(output)], cwd=BUILD, check=True)

if __name__ == "__main__":
    if len(sys.argv) > 1:
        for name in sys.argv[1:]:
            run(name)
    else:
        for name in ("tb_control_regs", "tb_border_config", "tb_video_test", "tb_c64_video_bridge",
                     "tb_c64_ntsc_video_bridge", "tb_gameboy_video_bridge", "tb_vectrex_video_bridge"):
            run(name)
