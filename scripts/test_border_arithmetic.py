#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check border lookahead underflow, signed viewport wrap, and pixel latency."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iverilog", default="iverilog")
    parser.add_argument("--vvp", default="vvp")
    args = parser.parse_args()
    build = ROOT / "build/border-arithmetic-tests"
    build.mkdir(parents=True, exist_ok=True)
    sources = ["hdl/board/multitini_border_overlay.sv", "hdl/sim/tb_border_arithmetic.sv"]
    tracked = sources + ["scripts/test_border_arithmetic.py"]
    before = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in tracked}
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    image = build / "arithmetic.vvp"
    commands = [
        ("compile", [args.iverilog, "-g2012", "-s", "tb_border_arithmetic", "-o", str(image)]
         + [str(ROOT / p) for p in sources]),
        ("run", [args.vvp, str(image)]),
    ]
    logs = []
    for name, command in commands:
        result = subprocess.run(guard + command, cwd=build, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=120)
        (build / (name + ".log")).write_text(result.stdout)
        logs.append({"name": name, "returncode": result.returncode})
        if result.returncode:
            print(result.stdout)
            raise RuntimeError(name + " failed")
        if name == "run":
            if "PASS border arithmetic: 94211 cases; all 4096 totals;" not in result.stdout:
                raise RuntimeError("Border arithmetic coverage marker missing")
            print(result.stdout.strip())
    after = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in tracked}
    if before != after:
        raise RuntimeError("Border arithmetic inputs changed during the run")
    (build / "validation.json").write_text(json.dumps({
        "passed": True, "logs": logs, "source_sha256": before,
        "source_sha256_after": after}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
