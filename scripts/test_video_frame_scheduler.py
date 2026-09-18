#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Ordered autonomous frame scheduling regression; guard Windows native tools."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    build = ROOT / "build/video-frame-scheduler-tests"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    compiler = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    simulator = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    for grace in (16, 256):
        output = build / f"scheduler-{grace}.vvp"
        subprocess.run(guard + [compiler, "-g2012", "-Wall", "-s", "tb_video_frame_scheduler",
                       "-P", f"tb_video_frame_scheduler.GRACE={grace}", "-o", str(output),
                       str(ROOT / "hdl/common/multitini_video_frame_scheduler.sv"),
                       str(ROOT / "hdl/sim/tb_video_frame_scheduler.sv")], cwd=build, check=True)
        subprocess.run(guard + [simulator, str(output)], cwd=build, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
