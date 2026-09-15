#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the original Vectrex CPU bus/interrupt smoke test with Icarus."""

from pathlib import Path
import os
import shutil
import subprocess
import sys


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    build = root / "build" / "vectrex-cpu-sim"
    build.mkdir(parents=True, exist_ok=True)
    launcher = root / "scripts" / "run_native_tool.py"
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))

    def tool(name: str) -> str:
        if os.name == "nt" and (runtime / f"{name}.exe").is_file():
            return str(runtime / f"{name}.exe")
        found = shutil.which(name)
        if not found:
            raise FileNotFoundError(f"{name} is required for this test")
        return found

    output = build / "tb_vectrex_cpu.vvp"
    commands = [
        [tool("iverilog"), "-g2012", "-Wall", "-Wimplicit", "-s", "tb_vectrex_cpu",
         "-o", str(output),
         str(root / "third_party/vectrex/cpu/mc6809i.v"),
         str(root / "hdl/core/vectrex/multitini_vectrex_cpu.sv"),
         str(root / "hdl/sim/tb_vectrex_cpu.sv")],
        [tool("vvp"), str(output)],
    ]
    for command in commands:
        result = subprocess.run([sys.executable, "-u", str(launcher), "--", *command],
                                cwd=root, check=False)
        if result.returncode:
            return result.returncode
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
