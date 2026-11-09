#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Simulate the Spectrum adapter in RTL or as a mapped ROM boot regression."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

from port_zx_spectrum_core import ROOT, UPSTREAM


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--mapped-rom", action="store_true", help="test the synthesized adapter")
    modes.add_argument("--checkpoint", type=Path, help="test the adapter from a full Vivado checkpoint")
    args = parser.parse_args()
    mapped = args.mapped_rom or args.checkpoint is not None
    subprocess.run([sys.executable, str(ROOT / "scripts/port_zx_spectrum_core.py"), "--check"], check=True)
    build = ROOT / "build" / ("test-zx-mapped" if mapped else "test-zx-spectrum")
    build.mkdir(parents=True, exist_ok=True)
    machine = ROOT / "hdl/core/zx_spectrum"
    t80 = UPSTREAM / "rtl/T80"
    vhdl = [t80 / f"{name}.vhd" for name in ("T80_Pack", "T80_ALU", "T80_MCode", "T80_Reg", "T80", "T80pa")]
    sv = [machine / name for name in (
        "ports/ula.sv", "ports/ym2149.sv", "multitini_zx_spectrum_keyboard.sv",
        "multitini_zx_spectrum_tzx.sv", "multitini_zx_spectrum_tape.sv", "multitini_zx_spectrum_adapter.sv"
    )]

    def tool(name):
        binary = shutil.which(f"{name}.bat" if os.name == "nt" else name)
        if not binary:
            raise SystemExit(f"Add Vivado's {name} to PATH")
        return binary

    def run(name, *args):
        binary = tool(name)
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

    if mapped:
        sources = ("checkpoint", args.checkpoint.resolve(strict=True)) if args.checkpoint else ("synth", *vhdl, *sv)
        run("vivado", "-mode", "batch", "-source", ROOT / "hdl/sim/check_zx_spectrum_rom_synthesis.tcl",
            "-tclargs", *sources)
        glbl = Path(tool("vivado")).resolve().parents[1] / "data/verilog/src/glbl.v"
        run("xvlog", "--sv", "--relax", build / "mapped_spectrum.v", ROOT / "hdl/sim/tb_zx_spectrum_mapped_rom.sv", glbl)
        run("xelab", "--relax", "--timescale", "1ns/1ps", "--mt", "off", "-L", "unisims_ver",
            "-s", "zx_spectrum_mapped", "tb_zx_spectrum_mapped_rom", "glbl")
        output = run("xsim", "zx_spectrum_mapped", "--runall")
        if "PASS mapped Spectrum ROM" not in output or "Fatal:" in output:
            raise SystemExit(f"Mapped Spectrum ROM test failed; see {build / 'xsim.stdout.log'}")
        print("PASS mapped Spectrum ROM writes and CPU reads in both banks")
        return

    run("xvhdl", "--2008", *vhdl)
    run("xvlog", "--sv", "--relax", *sv, ROOT / "hdl/sim/tb_zx_spectrum.sv")
    run("xelab", "--relax", "--timescale", "1ns/1ps", "--mt", "off", "-s", "zx_spectrum", "tb_zx_spectrum")
    output = run("xsim", "zx_spectrum", "--runall")
    if "PASS ZX Spectrum" not in output or "Fatal:" in output:
        raise SystemExit(f"Spectrum simulation failed; see {build / 'xsim.stdout.log'}")
    print("PASS ZX Spectrum Z80, paging, raster, audio, input and TAP")


if __name__ == "__main__":
    main()
