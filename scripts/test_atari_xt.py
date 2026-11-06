#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Simulate the private Atari-XT FPGA adapter with a synthetic XL ROM."""

import os
import shutil
import subprocess
import sys

from port_atari_xt_core import HDL, PORT_CHANGES, ROOT, UPSTREAM


def main() -> None:
    subprocess.run([sys.executable, str(ROOT / "scripts/port_atari_xt_core.py"), "--check"], check=True)
    build = ROOT / "build/test-atari-xt"
    build.mkdir(parents=True, exist_ok=True)
    machine = ROOT / "hdl/core/atari_xt"
    sources = [UPSTREAM / "hdl" / path for path in HDL if path.endswith(".sv") and path not in PORT_CHANGES]
    sources.extend(machine / "ports" / name for name in PORT_CHANGES)
    sources.extend(machine / name for name in (
        "ports/a8_core.sv", "multitini_atari_xt_keyboard.sv", "multitini_atari_xt_sio.sv",
        "multitini_atari_xt_adapter.sv"
    ))
    benches = ("tb_atari_xt", "tb_atari_xt_sio", "tb_atari_xt_pm_dma")
    sources.extend(ROOT / "hdl/sim" / f"{name}.sv" for name in benches)
    shutil.copyfile(UPSTREAM / "hdl/palette/atari_ntsc.hex", build / "atari_ntsc.hex")

    def run(name, *args, label=None):
        binary = shutil.which(f"{name}.bat" if os.name == "nt" else name)
        if not binary:
            raise SystemExit(f"Add Vivado's {name} to PATH")
        command = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--cwd", str(build), "--"]
        if binary.endswith(".bat"):
            command.extend(["cmd.exe", "/d", "/c"])
        result = subprocess.run([*command, binary, *map(str, args)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace")
        (build / f"{label or name}.stdout.log").write_text(result.stdout)
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)
        return result.stdout

    run("xvlog", "--sv", "--relax", *sources)
    for bench in (*benches, "tb_atari_320xe"):
        snapshot = f"sim_{bench}"
        run("xelab", "--relax", "--timescale", "1ns/1ps", "--mt", "off", "-s", snapshot, f"work.{bench}",
            label=f"{bench}-xelab")
        output = run("xsim", snapshot, "--runall", label=f"{bench}-xsim")
        if "PASS Atari-XT" not in output or "Fatal:" in output:
            print(output)
            raise SystemExit(f"Atari-XT simulation failed; see {build / (bench + '-xsim.stdout.log')}")
        print(next(line for line in output.splitlines() if line.startswith("PASS Atari-XT")))


if __name__ == "__main__":
    main()
