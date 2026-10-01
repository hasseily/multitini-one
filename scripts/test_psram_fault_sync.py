#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the PSRAM sticky-fault crossing's reset and two-clock latency."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    build = ROOT / "build/psram-fault-tests"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    cc = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    sim = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    sources = [ROOT / p for p in (
        "hdl/board/multitini_gameboy_psram_path.sv",
        "third_party/appletini/hdl/cdc_bit_sync.sv",
        "hdl/sim/tb_psram_fault_sync.sv")]
    tracked = sources + [Path(__file__).resolve()]
    hashes = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
              for p in tracked}
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    # Unrelated physical-memory modules are intentionally black boxes here.
    commands = [([cc, "-g2012", "-i", "-s", "tb_psram_fault_sync", "-o",
                  str(build / "fault.vvp")] + list(map(str, sources)), "compile"),
                ([sim, str(build / "fault.vvp")], "run")]
    for command, name in commands:
        result = subprocess.run(guard + command, cwd=ROOT, text=True,
                                capture_output=True, timeout=60)
        output = result.stdout + result.stderr
        (build / (name + ".log")).write_text(output)
        if result.returncode:
            print(output)
            return result.returncode
        if name == "run":
            if "PASS PSRAM fault crossing:" not in output:
                raise RuntimeError("fault-crossing success marker missing")
            print(output, end="")
    after = {str(p.relative_to(ROOT)): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in tracked}
    if hashes != after:
        raise RuntimeError("fault-crossing sources changed during validation")
    (build / "validation.json").write_text(json.dumps(
        {"passed": True, "source_sha256": hashes, "source_sha256_after": after},
        indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
