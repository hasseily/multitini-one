#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run isolated Vectrex chip tests through the repository's safe tool launcher."""

from pathlib import Path
import os
import subprocess
import sys


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    build = root / "build" / "vectrex-chip-sim"
    build.mkdir(parents=True, exist_ok=True)
    tool_bin = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    iverilog = str(tool_bin / "iverilog.exe") if os.name == "nt" else "iverilog"
    vvp = str(tool_bin / "vvp.exe") if os.name == "nt" else "vvp"
    launcher = [sys.executable, "-u", str(root / "scripts" / "run_native_tool.py"), "--"]
    for chip in ("via", "psg"):
        name = f"tb_vectrex_chips_{chip}"
        output = build / f"{name}.vvp"
        compile_command = [iverilog, "-g2012", "-Wall", "-Wimplicit", "-s", name,
                           "-o", str(output),
                           str(root / "hdl/core/vectrex/chips/via6522.v"),
                           str(root / "hdl/core/vectrex/chips/YM2149.sv"),
                           str(root / "hdl/sim" / f"{name}.sv")]
        for command in (compile_command, [vvp, str(output)]):
            result = subprocess.run(launcher + command, cwd=root, check=False)
            if result.returncode:
                return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
