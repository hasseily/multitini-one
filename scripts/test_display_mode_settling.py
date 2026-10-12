#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the complete mode mailbox, registered geometry, and reset contract."""
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
    build = ROOT / "build/display-mode-settling-tests"
    build.mkdir(parents=True, exist_ok=True)
    sources = ["hdl/common/multitini_display_pkg.sv",
               "third_party/appletini/hdl/cdc_bit_sync.sv", "hdl/sim/xilinx_primitives.sv",
               "hdl/common/multitini_async_mailbox.sv", "hdl/sim/video_fifo_model.sv",
               "hdl/common/multitini_display_control.sv", "hdl/common/multitini_display_reset.sv",
               "hdl/common/multitini_display_geometry.sv", "hdl/common/multitini_video_capture.sv",
               "hdl/sim/tb_display_mode_settling.sv"]
    tracked = sources + ["scripts/test_display_mode_settling.py",
                         "hdl/board/multitini_one_top.sv", "hdl/hdl_sources.txt"]
    before = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in tracked}
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    commands = []
    for name, machine, width, height in (("c64", 0, 382, 270), ("gbc", 1, 160, 144),
                                        ("vectrex", 3, 540, 720), ("genesis", 4, 320, 240)):
        image = build / (name + ".vvp")
        command = [args.iverilog, "-g2012", "-s", "tb_display_mode_settling"]
        for key, value in (("MACHINE", machine), ("WIDTH", width), ("HEIGHT", height)):
            command += ["-P", f"tb_display_mode_settling.{key}={value}"]
        commands += [(name + "-compile", command + ["-o", str(image)] + [str(ROOT / p) for p in sources]),
                     (name + "-run", [args.vvp, str(image)])]
    logs = []
    for name, command in commands:
        result = subprocess.run(guard + command, cwd=build, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=60)
        (build / (name + ".log")).write_text(result.stdout)
        logs.append({"name": name, "returncode": result.returncode})
        if result.returncode:
            print(result.stdout)
            raise RuntimeError(name + " failed")
        if name.endswith("-run"):
            if "PASS display mode settling: 6 modes, 5 multipliers, 30 atomic transactions," not in result.stdout:
                raise RuntimeError("display settling coverage marker missing")
            print(result.stdout.strip())
    after = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in tracked}
    if before != after:
        raise RuntimeError("Display settling inputs changed during the run")
    (build / "validation.json").write_text(json.dumps({
        "passed": True, "logs": logs, "source_sha256": before,
        "source_sha256_after": after}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
