#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the atomic RTC host extension entirely in offline HDL simulation."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["third_party/appletini/hdl/cdc_bit_sync.sv", "hdl/sim/amiga_cdc_model.sv",
           "hdl/common/multitini_async_mailbox.sv", "hdl/core/amiga/multitini_amiga_host.sv",
           "hdl/sim/tb_amiga_rtc_host.sv"]


def main():
    output = ROOT / "build/amiga-rtc-host-tests"
    output.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        return str(runtime / (name + ".exe")) if os.name == "nt" else name
    tracked = SOURCES + ["scripts/test_amiga_rtc_host.py"]
    def hashes():
        return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in tracked}
    before = hashes()
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    executable = output / "rtc.vvp"
    commands = [("compile", [tool("iverilog"), "-g2012", "-s", "tb_amiga_rtc_host", "-o",
                              str(executable), *[str(ROOT / name) for name in SOURCES]]),
                ("simulate", [tool("vvp"), str(executable)])]
    for stage, command in commands:
        result = subprocess.run(guard + command, cwd=ROOT, capture_output=True, text=True, timeout=60)
        log = result.stdout + result.stderr
        (output / (stage + ".log")).write_text(log, encoding="utf-8")
        print(log[-12000:], flush=True)
        if result.returncode:
            return result.returncode
    if log.count("PASS AMIGA") != 3:
        raise RuntimeError("RTC fixture did not complete all framing/timeout/reset cases")
    if before != hashes():
        raise RuntimeError("RTC sources changed during verification")
    (output / "validation.json").write_text(json.dumps({"passed": True, "board_evidence": False,
        "source_sha256": before, "native_consumer_stub": True,
        "coverage": ["atomic framing", "negedge publication", "accepted-edge acknowledgement",
                     "second-commit timeout rollback/retry", "clear/error/order", "pending reset"]}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
