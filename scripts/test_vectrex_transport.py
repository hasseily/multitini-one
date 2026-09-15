#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise Vectrex loader, abort epochs, modes, input, and bank transport."""
from pathlib import Path
import os
import subprocess
import sys


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    build = root / "build/vectrex-transport-sim"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    iverilog = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    vvp = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    launcher = [sys.executable, "-u", str(root / "scripts/run_native_tool.py"), "--"]
    sources = ["third_party/vectrex/cpu/mc6809i.v",
               "hdl/core/vectrex/chips/via6522.v", "hdl/core/vectrex/chips/YM2149.sv",
               "hdl/core/vectrex/multitini_vectrex_cpu.sv",
               "hdl/core/vectrex/multitini_vectrex_beam.sv",
               "hdl/core/vectrex/multitini_vectrex_renderer.sv",
               "hdl/core/vectrex/multitini_vectrex_machine.sv",
               "hdl/sim/tb_vectrex_transport.sv"]
    output = build / "tb_vectrex_transport.vvp"
    commands = [[iverilog, "-g2012", "-Wall", "-Wimplicit", "-s", "tb_vectrex_transport",
                 "-o", str(output), *(str(root / source) for source in sources)],
                [vvp, str(output)]]
    for command in commands:
        result = subprocess.run(launcher + command, cwd=root, check=False)
        if result.returncode:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
