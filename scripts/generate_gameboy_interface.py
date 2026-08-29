#!/usr/bin/env python3
"""Generate the C and SystemVerilog Game Boy Color interface constants."""

from __future__ import annotations

import argparse
import difflib
import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC_PATH = ROOT / "spec" / "gameboy_interface_v0.json"
SV_PATH = ROOT / "hdl" / "core" / "gameboy" / "multitini_gameboy_defs_pkg.sv"
C_PATH = ROOT / "firmware" / "include" / "multitini_gameboy_interface.h"


def number(value: object) -> int:
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"unsupported numeric value: {value!r}")


def load_spec() -> dict:
    spec = json.loads(SPEC_PATH.read_text(encoding="utf-8"))
    if spec.get("schema_version") != 1:
        raise ValueError("unsupported Game Boy-interface schema")
    load_values = [number(value) for value in spec["load_kinds"].values()]
    input_values = [number(value) for value in spec["input_namespaces"].values()]
    button_values = [number(value) for value in spec["buttons"].values()]
    if len(load_values) != len(set(load_values)):
        raise ValueError("duplicate Game Boy load kind")
    if len(input_values) != len(set(input_values)):
        raise ValueError("duplicate Game Boy input namespace")
    if sorted(button_values) != list(range(8)):
        raise ValueError("Game Boy buttons must be bits zero through seven")
    if number(spec["sizes"]["GB_CART_MAX"]) > number(
            spec["memory_map"]["CART_RAM_BASE"]):
        raise ValueError("Game Boy ROM region overlaps cartridge RAM")
    return spec


def generate_sv(spec: dict) -> str:
    lines = [
        "// SPDX-License-Identifier: GPL-3.0-only",
        "// Generated from spec/gameboy_interface_v0.json. Do not edit.",
        "`timescale 1ns / 1ps",
        "package multitini_gameboy_defs_pkg;",
        f"    localparam logic [31:0] MT_GBC_CORE_ID = 32'h{number(spec['core_id']):08X};",
        "",
    ]
    for name, value in spec["clocks_hz"].items():
        lines.append(f"    localparam integer MT_GBC_{name}_HZ = {number(value)};")
    lines.append("")
    for name, value in spec["load_kinds"].items():
        lines.append(
            f"    localparam logic [7:0] MT_LOAD_{name} = 8'h{number(value):02X};")
    lines.append("")
    for name, value in spec["sizes"].items():
        lines.append(f"    localparam integer MT_SIZE_{name} = {number(value)};")
    lines.append("")
    for name, value in spec["input_namespaces"].items():
        lines.append(
            f"    localparam logic [7:0] MT_INPUT_{name} = 8'h{number(value):02X};")
    lines.append("")
    for name, value in spec["buttons"].items():
        lines.append(f"    localparam integer MT_GB_BUTTON_{name} = {number(value)};")
    lines.append("")
    for name, value in spec["memory_map"].items():
        lines.append(
            f"    localparam logic [23:0] MT_GB_MEMORY_{name} = 24'h{number(value):06X};")
    lines.extend(["endpackage", ""])
    return "\n".join(lines)


def generate_c(spec: dict) -> str:
    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* Generated from spec/gameboy_interface_v0.json. Do not edit. */",
        "#ifndef MULTITINI_GAMEBOY_INTERFACE_H",
        "#define MULTITINI_GAMEBOY_INTERFACE_H",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"#define MT_GBC_CORE_ID UINT32_C(0x{number(spec['core_id']):08X})",
    ]
    for name, value in spec["clocks_hz"].items():
        lines.append(f"#define MT_GBC_{name}_HZ UINT32_C({number(value)})")
    lines.extend(["", "typedef enum {"])
    for name, value in spec["load_kinds"].items():
        lines.append(f"    MT_LOAD_{name} = 0x{number(value):02X},")
    lines.extend(["} mt_gameboy_load_kind_t;", "", "typedef enum {"])
    for name, value in spec["input_namespaces"].items():
        lines.append(f"    MT_INPUT_{name} = 0x{number(value):02X},")
    lines.extend(["} mt_gameboy_input_namespace_t;", "", "typedef enum {"])
    for name, value in spec["buttons"].items():
        lines.append(f"    MT_GB_BUTTON_{name} = {number(value)},")
    lines.extend(["} mt_gameboy_button_t;", ""])
    for name, value in spec["sizes"].items():
        lines.append(f"#define MT_SIZE_{name} ((size_t){number(value)})")
    lines.append("")
    for name, value in spec["memory_map"].items():
        lines.append(
            f"#define MT_GB_MEMORY_{name} UINT32_C(0x{number(value):06X})")
    lines.extend(["", "#endif", ""])
    return "\n".join(lines)


def update_or_check(path: Path, expected: str, check: bool) -> bool:
    actual = path.read_text(encoding="utf-8") if path.exists() else ""
    if actual == expected:
        return True
    if check:
        sys.stderr.writelines(difflib.unified_diff(
            actual.splitlines(True), expected.splitlines(True),
            fromfile=str(path), tofile=f"generated:{path}"))
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(expected, encoding="utf-8")
    print(f"generated {path.relative_to(ROOT)}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    spec = load_spec()
    results = (
        update_or_check(SV_PATH, generate_sv(spec), args.check),
        update_or_check(C_PATH, generate_c(spec), args.check),
    )
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
