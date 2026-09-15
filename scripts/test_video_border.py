#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check border geometry, color, clipping, and continuous pixel alignment."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    build = ROOT / "build/video-border-tests"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    cc = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    sim = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    guard = [sys.executable, "-u", str(ROOT / "scripts/run_native_tool.py"), "--"]
    output = build / "border.vvp"
    commands = [
        [cc, "-g2012", "-Wall", "-Wimplicit", "-s", "tb_border_overlay", "-o", str(output),
         str(ROOT / "hdl/board/multitini_border_overlay.sv"),
         str(ROOT / "hdl/sim/tb_border_overlay.sv")],
        [sim, str(output), "+capture=" + str(build / "vectrex-side-borders.ppm")],
    ]
    with (build / "test.log").open("w", encoding="utf-8") as log:
        for command in commands:
            process = subprocess.Popen(guard + command, cwd=ROOT, text=True,
                                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            for line in process.stdout:
                print(line, end="", flush=True)
                log.write(line)
            if process.wait():
                return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
