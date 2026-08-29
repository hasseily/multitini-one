#!/usr/bin/env python3
"""Create the deterministic Xilinx-compatible Game Boy machine source port."""

from __future__ import annotations

import argparse
import difflib
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIR = ROOT / "third_party" / "gameboy" / "upstream" / "rtl"
OUTPUT_DIR = ROOT / "hdl" / "core" / "gameboy" / "ports"


def replace_once(source: str, old: str, new: str, description: str) -> str:
    if source.count(old) != 1:
        raise ValueError(f"pinned Game Boy {description} changed")
    return source.replace(old, new)


def normalized_port(source: str) -> str:
    """Keep product ports deterministic without copying upstream whitespace."""
    return "\n".join(line.rstrip() for line in source.splitlines()) + "\n"


def port_gb() -> str:
    source = (SOURCE_DIR / "gb.v").read_text(encoding="utf-8")
    old = 'dpram_dif #(12,8,11,16,"BootROMs/cgb_boot.mif") boot_rom ('
    new = 'dpram_dif #(12,8,11,16,"cgb_boot.hex") boot_rom ('
    result = replace_once(source, old, new, "boot-memory instance")
    result = replace_once(
        result,
        "wire cpu_wr_vram = sel_vram && !cpu_wr_n && vram_cpu_allow;\n\n"
        "wire hdma_rd;",
        "wire cpu_wr_vram = sel_vram && !cpu_wr_n && vram_cpu_allow;\n\n"
        "// hdma_rd declared before its first use.",
        "HDMA declaration")
    result = replace_once(
        result, "// when hdma is enabled stop CPU (GBC).\n",
        "// Portable forward declarations for strict Verilog tools.\n"
        "wire hdma_rd;\nwire hdma_read_wram_bus;\n"
        "wire hdma_read_ext_bus;\n\n"
        "// when hdma is enabled stop CPU (GBC).\n",
        "HDMA insertion point")
    result = replace_once(result,
        "wire hdma_read_wram_bus = (isGBC & hdma_rd & hdma_sel_wram);",
        "assign hdma_read_wram_bus = (isGBC & hdma_rd & hdma_sel_wram);",
        "HDMA WRAM declaration")
    result = replace_once(result,
        "wire hdma_read_ext_bus = (isGBC & hdma_rd & hdma_sel_ext_bus);",
        "assign hdma_read_ext_bus = (isGBC & hdma_rd & hdma_sel_ext_bus);",
        "HDMA external-bus declaration")
    result = replace_once(result, "\nalways begin\n\tcase (boot_addr)",
                          "\nalways @* begin\n\tcase (boot_addr)",
                          "bootstrap combinational block")
    marker = ("// Multitini port: open bootstrap path plus strict-tool "
              "forward declarations.\n")
    insertion = result.index("module gb")
    return normalized_port(result[:insertion] + marker + result[insertion:])


def port_cart() -> str:
    result = (SOURCE_DIR / "cart.v").read_text(encoding="utf-8")
    forward = """// Multitini port: strict-tool forward declarations.
reg [7:0] cart_mbc_type;
reg [2:0] mapper_sel_r;
reg [8:0] rom_mask;
reg mbc1m, mmm01;
reg sachen;
wire [3:0] ram_mask;
wire mbc1, mbc2, mbc3, mbc30, mbc5, mbc6, mbc7;
wire rocket, megaduck_en, gb_camera, tama, HuC3, HuC1;
wire wisdom_tree, mani161, cart_rumbling;
wire [7:0] cram_q;

"""
    replacements = {
        "reg [7:0] cart_mbc_type;": "// cart_mbc_type declared before mapper instance",
        "wire [3:0] ram_mask =": "assign ram_mask =",
        "reg [8:0] rom_mask; // get mask from file size":
            "// rom_mask declared before mapper instance",
        "wire mbc1 =": "assign mbc1 =",
        "wire mbc2 =": "assign mbc2 =",
        "wire mbc3 =": "assign mbc3 =",
        "wire mbc30 =": "assign mbc30 =",
        "wire mbc5 =": "assign mbc5 =",
        "wire mbc6 =": "assign mbc6 =",
        "wire mbc7 =": "assign mbc7 =",
        "wire rocket =": "assign rocket =",
        "wire megaduck_en =": "assign megaduck_en =",
        "wire gb_camera =": "assign gb_camera =",
        "wire tama =": "assign tama =",
        "wire HuC3 =": "assign HuC3 =",
        "wire HuC1 =": "assign HuC1 =",
        "wire wisdom_tree =": "assign wisdom_tree =",
        "wire mani161     =": "assign mani161     =",
        "wire cart_rumbling;": "// cart_rumbling declared before mapper instance",
        "reg mbc1m, mmm01;": "// mbc1m and mmm01 declared before mapper instance",
        "reg sachen, sachen_t1, sachen_t2;": "reg sachen_t1, sachen_t2;",
        "reg [2:0] mapper_sel_r;": "// mapper_sel_r declared before mapper instance",
        "wire [7:0] cram_q =": "assign cram_q =",
    }
    for old, new in replacements.items():
        result = replace_once(result, old, new, f"cart declaration {old}")
    result = replace_once(result, "mappers mappers (", forward + "mappers mappers (",
                          "mapper insertion point")
    return normalized_port(result)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    outputs = {
        OUTPUT_DIR / "gb.v": port_gb(),
        OUTPUT_DIR / "cart.v": port_cart(),
    }
    changed = False
    for output, expected in outputs.items():
        actual = output.read_text(encoding="utf-8") if output.exists() else ""
        if actual == expected:
            continue
        changed = True
        if args.check:
            sys.stderr.writelines(difflib.unified_diff(
                actual.splitlines(True), expected.splitlines(True),
                fromfile=str(output), tofile=f"generated:{output}"))
        else:
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(expected, encoding="utf-8")
            print(f"generated {output.relative_to(ROOT)}")
    return 1 if args.check and changed else 0


if __name__ == "__main__":
    raise SystemExit(main())
