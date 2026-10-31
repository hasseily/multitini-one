#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the production Amiga adapter input queue with a controlled native stub."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["third_party/appletini/hdl/cdc_bit_sync.sv", "hdl/sim/amiga_cdc_model.sv",
           "hdl/common/multitini_async_mailbox.sv", "hdl/core/amiga/multitini_amiga_memory.sv",
           "hdl/core/amiga/multitini_amiga_host.sv", "hdl/core/amiga/multitini_amiga_loader.sv",
           "hdl/core/amiga/multitini_amiga_adapter.sv", "hdl/board/multitini_amiga_video_normalizer.sv",
           "hdl/sim/tb_amiga_adapter_input.sv"]


def main():
    output = ROOT / "build/amiga-input-tests"
    output.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        return str(runtime / (name + ".exe")) if os.name == "nt" else name
    tracked = SOURCES + ["scripts/test_amiga_input.py"]
    def hashes():
        return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in tracked}
    before = hashes()
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    executable = output / "input.vvp"
    commands = [("compile", [tool("iverilog"), "-g2012", "-s", "tb_amiga_adapter_input",
                              "-o", str(executable), *[str(ROOT / source) for source in SOURCES]]),
                ("simulate", [tool("vvp"), str(executable)])]
    for stage, command in commands:
        result = subprocess.run(guard + command, cwd=ROOT, capture_output=True, text=True, timeout=120)
        log = result.stdout + result.stderr
        (output / (stage + ".log")).write_text(log, encoding="utf-8")
        print(log[-12000:], flush=True)
        if result.returncode:
            return result.returncode
    if "PASS AMIGA adapter input FIFO" not in log:
        raise RuntimeError("Input simulation did not reach completion")
    if before != hashes():
        raise RuntimeError("Amiga input sources changed during verification")
    (output / "validation.json").write_text(json.dumps({"passed": True,
        "source_sha256": before, "board_evidence": False, "native_machine_stub": True}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
