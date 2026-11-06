#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Boot synthetic Atari OS and BASIC through a Vivado adapter netlist."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkpoint", type=Path, help="synthesized or routed full Atari image checkpoint")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/test-atari-rom-boot")
    parser.add_argument("--compy-320", action="store_true", help="verify all 16 extended banks and ANTIC banking")
    args = parser.parse_args()
    bench = "tb_atari_xt_compy_boot" if args.compy_320 else "tb_atari_xt_rom_boot"
    checkpoint = args.checkpoint.resolve(strict=True)
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)

    def locate(name):
        binary = shutil.which(f"{name}.bat" if os.name == "nt" else name)
        if not binary:
            raise SystemExit(f"Add Vivado's {name} to PATH")
        return Path(binary)

    def run(name, *arguments):
        binary = locate(name)
        command = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--cwd", str(build), "--"]
        if binary.suffix == ".bat":
            command.extend(["cmd.exe", "/d", "/c"])
        result = subprocess.run([*command, str(binary), *map(str, arguments)], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace")
        (build / f"{name}.stdout.log").write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            print(result.stdout)
            raise SystemExit(result.returncode)
        return result.stdout

    # Preserve the address logic and primitive memories chosen by Vivado.
    script = build / "export_adapter.tcl"
    script.write_text("""\
open_checkpoint [lindex $argv 0]
set adapter [get_cells -quiet -hierarchical -filter {NAME =~ *machine_adapter && REF_NAME =~ multitini_atari_xt_adapter*}]
if {[llength $adapter] != 1} { error "Expected exactly one Atari adapter, got $adapter" }
write_verilog -force -mode funcsim -cell $adapter adapter.v
""", encoding="ascii")
    run("vivado", "-mode", "batch", "-source", script, "-tclargs", checkpoint)
    glbl = locate("xvlog").parent.parent / "data/verilog/src/glbl.v"
    run("xvlog", "--sv", "--relax", build / "adapter.v", ROOT / "hdl/sim/tb_atari_rom_boot.sv", glbl)
    run("xelab", "--relax", "--timescale", "1ns/1ps", "--mt", "off", "-L", "unisims_ver",
        "-s", bench, f"work.{bench}", "work.glbl")
    output = run("xsim", bench, "--runall")
    if "PASS Atari-XT" not in output or "Fatal:" in output:
        raise SystemExit(f"Atari ROM boot simulation failed; see {build / 'xsim.stdout.log'}")
    print(next(line for line in output.splitlines() if line.startswith("PASS Atari-XT")))


if __name__ == "__main__":
    main()
