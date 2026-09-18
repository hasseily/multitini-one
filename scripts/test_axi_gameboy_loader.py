#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise actual AXI/control/CDC/GBC upload logic with a stalled memory endpoint.

Portable XPM functional model only; this cannot validate PSRAM pin timing or
explain an intermittent physical-board failure. No Vivado is launched.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = [
    "hdl/generated/multitini_regs_pkg.sv",
    "hdl/core/gameboy/multitini_gameboy_defs_pkg.sv",
    "hdl/sim/xilinx_primitives.sv",
    "third_party/appletini/hdl/cdc_bit_sync.sv",
    "hdl/common/multitini_axi3_regs.sv",
    "hdl/common/multitini_control_regs.sv",
    "hdl/common/multitini_async_mailbox.sv",
    "hdl/core/gameboy/multitini_gameboy_loader.sv",
    "hdl/sim/tb_axi_gameboy_loader.sv",
]


def hashes():
    return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
            for name in SOURCES + ["scripts/test_axi_gameboy_loader.py", "scripts/run_native_tool.py"]}


def main():
    directory = ROOT / "build/axi-gameboy-loader-tests"
    directory.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    compiler = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    simulator = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    initial = hashes()
    commands = []

    def run(command, name):
        commands.append(guard + command)
        result = subprocess.run(guard + command, cwd=directory, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
        (directory / name).write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            print(result.stdout)
            raise subprocess.CalledProcessError(result.returncode, command)
        return result.stdout

    output = directory / "integration.vvp"
    compile_log = run([compiler, "-g2012", "-Wall", "-Wimplicit", "-s", "tb_axi_gameboy_loader",
                       "-o", str(output)] + [str(ROOT / source) for source in SOURCES], "compile.log")
    assert "implicit definition of wire" not in compile_log, compile_log
    cases = []
    for seed in (17, 90827):
        log = f"seed-{seed}.log"
        text = run([simulator, str(output), f"+SEED={seed}"], log)
        match = re.search(r"PASS: AXI to GBC loader integration bytes=(\d+) header_bytes=(\d+) reads=(\d+) aw_first=(\d+) w_first=(\d+) simultaneous=(\d+)", text)
        assert match and int(match[1]) == 98308 and int(match[2]) == 228, text
        cases.append({"seed": seed, "bytes_checked": int(match[1]), "header_bytes_checked": int(match[2]),
                      "reads": int(match[3]), "aw_first": int(match[4]), "w_first": int(match[5]),
                      "simultaneous": int(match[6]), "log": log,
                      "log_sha256": hashlib.sha256((directory / log).read_bytes()).hexdigest()})
        print(text.strip())
    assert hashes() == initial, "Tested inputs changed during validation"
    (directory / "validation.json").write_text(json.dumps({
        "passed": True, "sources": initial, "commands": commands, "cases": cases,
        "scope": "Actual AXI adapter/control bank/portable CDC mailbox/GBC loader; external memory endpoint model.",
        "limitations": "Reset is tested after mailbox/memory drain. No physical PSRAM timing/readback, real XPM, mapper, CPU execution, or board startup claim."
    }, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
