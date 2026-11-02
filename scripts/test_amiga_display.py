#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the optional display mailbox and bounded host commit offline."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["third_party/appletini/hdl/cdc_bit_sync.sv", "hdl/sim/amiga_cdc_model.sv",
           "hdl/common/multitini_async_mailbox.sv", "hdl/core/amiga/multitini_amiga_ide_block.sv",
           "hdl/core/amiga/multitini_amiga_diagnostics.sv",
           "hdl/core/amiga/multitini_amiga_host.sv",
           "hdl/core/amiga/multitini_amiga_display_mailbox.sv", "hdl/sim/tb_amiga_display_host.sv"]


def main():
    output = ROOT / "build/amiga-display-host-hdl"
    output.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        return str(runtime / (name + ".exe")) if os.name == "nt" else name
    tracked = SOURCES + ["scripts/test_amiga_display.py"]
    def hashes():
        return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in tracked}
    before = hashes()
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    executable = output / "display.vvp"
    commands = [("compile", [tool("iverilog"), "-g2012", "-s", "tb_amiga_display_host", "-o",
                              str(executable), *[str(ROOT / name) for name in SOURCES]]),
                ("simulate", [tool("vvp"), str(executable)])]
    for stage, command in commands:
        result = subprocess.run(guard + command, cwd=ROOT, capture_output=True, text=True, timeout=90)
        log = result.stdout + result.stderr
        (output / (stage + ".log")).write_text(log, encoding="utf-8")
        print(log[-12000:], flush=True)
        if result.returncode:
            return result.returncode
    if log.count("PASS AMIGA") != 2:
        raise RuntimeError("Display fixture did not complete every case")
    if before != hashes():
        raise RuntimeError("Display sources changed during verification")
    (output / "validation.json").write_text(json.dumps({"passed": True, "board_evidence": False,
        "source_sha256": before, "native_consumer_stub": True,
        "coverage": ["guest address decode and byte lanes", "native-edge acknowledgement",
                     "metadata snapshots and ACK ordering", "drain-before-commit",
                     "bounded timeout and rollback", "framing, reset and capability"]}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
