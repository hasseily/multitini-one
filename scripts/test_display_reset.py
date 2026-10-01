#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check display hold CDC, immediate clock-loss reset, and mode-controller faults."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    build = ROOT / "build/display-reset-tests"
    build.mkdir(parents=True, exist_ok=True)
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    tests = [
        ("tb_display_reset", ["hdl/common/multitini_display_reset.sv", "hdl/sim/tb_display_reset.sv"],
         "PASS display reset: 48 phase/mode scenarios"),
        ("tb_display_control", ["hdl/common/multitini_display_pkg.sv", "hdl/common/multitini_display_control.sv",
                                "hdl/sim/tb_display_control.sv"], "VIDEO MODE CONTROL PASS"),
    ]
    paths = {p for _, sources, _ in tests for p in sources}
    paths.add("scripts/test_display_reset.py")
    before = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sorted(paths)}
    logs = []
    for top, sources, marker in tests:
        image = build / (top + ".vvp")
        commands = [
            (top + "-compile", ["iverilog", "-g2012", "-s", top, "-o", str(image)] + [str(ROOT / p) for p in sources]),
            (top + "-run", ["vvp", str(image)]),
        ]
        for name, command in commands:
            result = subprocess.run(guard + command, cwd=build, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, text=True, timeout=60)
            (build / (name + ".log")).write_text(result.stdout)
            logs.append({"name": name, "returncode": result.returncode})
            if result.returncode or (name.endswith("-run") and marker not in result.stdout):
                print(result.stdout)
                raise RuntimeError(name + " failed")
            if name.endswith("-run"):
                print(result.stdout.strip())
    after = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sorted(paths)}
    if before != after:
        raise RuntimeError("Display reset inputs changed during the run")
    (build / "validation.json").write_text(json.dumps({"passed": True, "logs": logs,
        "source_sha256": before, "source_sha256_after": after}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
