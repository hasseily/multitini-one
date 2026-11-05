#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Simulate the Spectrum adapter with its real MiSTer ULA, Z80 and PSG."""

import os
import shutil
import subprocess
import sys

from port_zx_spectrum_core import ROOT, UPSTREAM


def main() -> None:
    subprocess.run([sys.executable, str(ROOT / "scripts/port_zx_spectrum_core.py"), "--check"], check=True)
    build = ROOT / "build/test-zx-spectrum"
    build.mkdir(parents=True, exist_ok=True)
    machine = ROOT / "hdl/core/zx_spectrum"
    t80 = UPSTREAM / "rtl/T80"
    vhdl = [t80 / f"{name}.vhd" for name in ("T80_Pack", "T80_ALU", "T80_MCode", "T80_Reg", "T80", "T80pa")]
    sv = [machine / name for name in (
        "ports/ula.sv", "ports/ym2149.sv", "multitini_zx_spectrum_keyboard.sv",
        "multitini_zx_spectrum_tape.sv", "multitini_zx_spectrum_adapter.sv"
    )] + [ROOT / "hdl/sim/tb_zx_spectrum.sv"]

    def run(name, *args):
        binary = shutil.which(f"{name}.bat" if os.name == "nt" else name)
        if not binary:
            raise SystemExit(f"Add Vivado's {name} to PATH")
        command = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--cwd", str(build), "--"]
        if binary.endswith(".bat"):
            command.extend(["cmd.exe", "/d", "/c"])
        result = subprocess.run([*command, binary, *map(str, args)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace")
        (build / f"{name}.stdout.log").write_text(result.stdout)
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)
        return result.stdout

    run("xvhdl", "--2008", *vhdl)
    run("xvlog", "--sv", "--relax", *sv)
    run("xelab", "--relax", "--timescale", "1ns/1ps", "--mt", "off", "-s", "zx_spectrum", "tb_zx_spectrum")
    output = run("xsim", "zx_spectrum", "--runall")
    if "PASS ZX Spectrum" not in output or "Fatal:" in output:
        raise SystemExit(f"Spectrum simulation failed; see {build / 'xsim.stdout.log'}")
    print("PASS ZX Spectrum Z80, paging, raster, audio, input and TAP")


if __name__ == "__main__":
    main()
