#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run focused beam and raster tests through the guarded native launcher."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
build = ROOT / "build/vectrex-video-sim"
build.mkdir(parents=True, exist_ok=True)
runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
compiler = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
simulator = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
launcher = [sys.executable, "-u", str(ROOT / "scripts/run_native_tool.py"), "--"]
for part in ("beam", "renderer", "renderer_decay"):
    top = "tb_vectrex_" + part
    executable = build / (top + ".vvp")
    for command in ([compiler, "-g2012", "-Wall", "-Wimplicit", "-s", top, "-o", str(executable),
                     str(ROOT / f"hdl/core/vectrex/multitini_vectrex_{part.split('_')[0]}.sv"), str(ROOT / f"hdl/sim/{top}.sv")],
                    [simulator, str(executable)]):
        result = subprocess.run(launcher + command, cwd=ROOT, check=False)
        if result.returncode:
            raise SystemExit(result.returncode)
