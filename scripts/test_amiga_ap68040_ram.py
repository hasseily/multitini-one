#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Measure AP68040 Fast RAM in simulation and check cache-line wrapping."""

from __future__ import annotations

import argparse
from pathlib import Path
import subprocess
import sys

from test_amiga_ports import ROOT, ap68040_sources

sys.path.insert(0, str(ROOT / "software/amiga"))
from m68k import Assembler


def guest_code(movem_read: bool, fast16: bool) -> bytes:
    asm = Assembler()
    ram_base = 0x40000000 if fast16 else 0
    asm.ins("move.l", "#$80008000", "d0")
    asm.word(0x4e7b)  # MOVEC D0,CACR. Enable both AP68040 caches.
    asm.word(0x0002)
    for start in range(4):
        asm.ins("move.l", f"${ram_base + 0x210000 + 20 * start:x}.l", "d0")
    asm.ins("move.l", f"#${ram_base + 0x211000:x}", "a0")
    asm.ins("move.l", f"#${ram_base + 0x230000:x}", "a1")
    asm.word(0xf620)  # MOVE16 (A0)+,(A1)+. The line write must remain inhibited.
    asm.word(0x9000)
    asm.ins("move.l", "#1", "$260000.l")
    if movem_read:
        # The SysInfo 4.5b12 workload reads 261,120 bytes per sweep using MOVEM.
        asm.ins("move.l", f"#${ram_base + 0x220000:x}", "a6")
        asm.ins("move.w", "#815", "d4")
        asm.label("read_loop")
        for _ in range(8):
            asm.word(0x4cde)  # MOVEM.L (A6)+,D0-D3/A0-A5
            asm.word(0x3f0f)
        asm.ins("dbra", "d4", "read_loop")
    else:
        asm.ins("move.l", f"#${ram_base + 0x220000:x}", "a0")
        asm.ins("move.w", "#2047", "d1")
        asm.label("read_loop")
        for _ in range(8):
            asm.ins("move.l", "(a0)+", "d0")
        asm.ins("dbra", "d1", "read_loop")
    asm.ins("move.l", "#2", "$260000.l")
    asm.ins("move.l", f"#${ram_base + 0x220000:x}", "a0")
    asm.ins("move.l", f"#${ram_base + 0x240000:x}", "a1")
    asm.ins("move.w", "#2047", "d1")
    asm.label("copy_loop")
    for _ in range(8):
        asm.ins("move.l", "(a0)+", "(a1)+")
    asm.ins("dbra", "d1", "copy_loop")
    asm.ins("move.l", "#3", "$260000.l")
    asm.label("done")
    asm.ins("bra", "done")
    return asm.finish()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/amiga-ap68040-ram-test")
    parser.add_argument("--vivado", type=Path, default=Path("C:/Xilinx/2025.2/Vivado"))
    parser.add_argument("--latency", type=int, default=8, help="Modeled DDR response delay in native clocks")
    parser.add_argument("--expect-burst", action="store_true", help="Require a single TS per AP68040 cache line")
    parser.add_argument("--cpu-2x", action="store_true", help="Double PCLK while keeping the native bus speed")
    parser.add_argument("--movem-read", action="store_true", help="Use the longer SysInfo-style MOVEM read sweep")
    parser.add_argument("--fast16", action="store_true", help="Use the 16 MiB Zorro III bank for the data sweep")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if not build.is_relative_to(ROOT / "build") or args.latency < 0:
        parser.error("Use a directory under build/ and a nonnegative latency")
    build.mkdir(parents=True, exist_ok=True)
    if args.fast16 and not args.expect_burst:
        parser.error("--fast16 requires --expect-burst")
    (build / "guest.hex").write_text("".join(f"{byte:02x}\n" for byte in guest_code(args.movem_read, args.fast16).ljust(512, b"\x00")))

    def run(tool: str, arguments: list[str]) -> str:
        executable = args.vivado / "bin" / f"{tool}.bat"
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--cwd", str(build), "--",
             "cmd.exe", "/d", "/c", str(executable), *arguments],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
        )
        (build / f"{tool}.stdout.log").write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            print(result.stdout[-12000:])
            raise SystemExit(result.returncode)
        return result.stdout

    ap_rtl = ROOT / "third_party/amiga/ap68040/rtl"
    sources = ap68040_sources() + [ROOT / "hdl/core/amiga/multitini_amiga_memory.sv",
                                  ROOT / "hdl/sim/tb_amiga_ap68040_ram.sv"]
    top = "tb_amiga_ap68040_ram"
    run("xvlog", ["--sv", "--relax", "-i", str(ap_rtl), "-i", str(ap_rtl / "fpu"),
                  *map(str, sources)])
    run("xelab", ["--relax", "--debug", "typical", "--mt", "auto", f"work.{top}", "-s", top])
    # cmd.exe splits an unquoted equals sign in arguments to the Vivado batch file.
    (build / "xsim.args").write_text(
        f"--testplusarg latency={args.latency}\n--testplusarg expect_burst={int(args.expect_burst)}\n"
        f"--testplusarg cpu_2x={int(args.cpu_2x)}\n--testplusarg movem_read={int(args.movem_read)}\n"
        f"--testplusarg fast16={int(args.fast16)}\n"
    )
    result = run("xsim", [top, "--runall", "--file", "xsim.args"])
    print(result)
    return int("PASS AP68040 RAM" not in result or "Fatal:" in result)


if __name__ == "__main__":
    raise SystemExit(main())
