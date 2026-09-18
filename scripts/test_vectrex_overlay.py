#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check exact overlay transport/cache and compositor with guarded native tools."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/vectrex-overlay-tests"
UCRT = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
GUARD = [sys.executable, "-u", str(ROOT / "scripts/run_native_tool.py"), "--"]
COMMON = ["hdl/sim/xilinx_primitives.sv", "third_party/appletini/hdl/cdc_bit_sync.sv",
          "hdl/common/multitini_async_mailbox.sv", "hdl/core/vectrex/multitini_vectrex_compositor.sv"]
CASES = {
    "tb_vectrex_compositor": COMMON,
    "tb_vectrex_overlay": COMMON + ["hdl/core/vectrex/multitini_vectrex_overlay.sv",
                                   "hdl/board/multitini_vectrex_video_bridge.sv"],
}

def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    selected = sys.argv[1:] or list(CASES)
    evidence = []
    for name in selected:
        sources = CASES[name] + ["hdl/sim/" + name + ".sv"]
        output = BUILD / (name + ".vvp")
        compiler = str(UCRT / "iverilog.exe") if os.name == "nt" else "iverilog"
        runtime = str(UCRT / "vvp.exe") if os.name == "nt" else "vvp"
        commands = [[compiler, "-g2012", "-Wall", "-s", name, "-o", str(output)] +
                    [str(ROOT / path) for path in sources], [runtime, str(output)]]
        transcript = ""
        for command in commands:
            result = subprocess.run(GUARD + command, cwd=BUILD, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            transcript += result.stdout
            print(result.stdout, end="", flush=True)
            (BUILD / (name + ".log")).write_text(transcript, encoding="utf-8")
            if result.returncode:
                raise SystemExit(result.returncode)
        evidence.append({"test": name, "passed": True,
                         "source_sha256": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources}})
    (BUILD / "results.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")

if __name__ == "__main__":
    main()
