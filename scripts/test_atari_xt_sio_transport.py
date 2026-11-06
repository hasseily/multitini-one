#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Simulate the Atari SIO MMIO queues with Vivado's XPM CDC model."""

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    build = ROOT / "build/test-atari-xt-sio-transport"
    build.mkdir(parents=True, exist_ok=True)
    suffix = ".bat" if os.name == "nt" else ""
    xvlog = shutil.which(f"xvlog{suffix}")
    if not xvlog:
        raise SystemExit("Add Vivado's bin directory to PATH")
    vivado = Path(xvlog).resolve().parents[1]

    def run(tool, *args):
        executable = vivado / "bin" / f"{tool}{suffix}"
        command = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--cwd", str(build), "--"]
        if os.name == "nt":
            command.extend(["cmd.exe", "/d", "/c"])
        result = subprocess.run([*command, executable, *map(str, args)], text=True,
                                errors="replace", stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=120)
        (build / f"{tool}.stdout.log").write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)
        return result.stdout

    run("xvlog", "--sv", vivado / "data/ip/xpm/xpm_cdc/hdl/xpm_cdc.sv",
        vivado / "data/verilog/src/glbl.v", *(ROOT / file for file in (
        "hdl/generated/multitini_regs_pkg.sv",
        "third_party/appletini/hdl/cdc_bit_sync.sv",
        "hdl/common/multitini_async_mailbox.sv",
        "hdl/board/multitini_atari_sio_transport.sv",
        "hdl/sim/tb_atari_xt_sio_transport.sv",
    )))
    run("xelab", "--timescale", "1ns/1ps", "--mt", "off", "-s", "atari_sio",
        "work.tb_atari_xt_sio_transport", "work.glbl")
    output = run("xsim", "atari_sio", "--runall")
    if "PASS Atari SIO transport" not in output or "Fatal:" in output:
        print(output)
        raise SystemExit("Atari SIO transport simulation failed")
    print("PASS Atari SIO transport queues, faults and reset recovery")


if __name__ == "__main__":
    main()
