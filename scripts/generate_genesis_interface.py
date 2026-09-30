#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the shared Genesis/Mega Drive firmware and FPGA interface."""

from __future__ import annotations

import argparse
import difflib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SPEC_PATH = ROOT / "spec/genesis_interface_v0.json"
SV_PATH = ROOT / "hdl/core/genesis/multitini_genesis_defs_pkg.sv"
C_PATH = ROOT / "firmware/include/multitini_genesis_interface.h"


def number(value: object) -> int:
    if isinstance(value, bool):
        raise ValueError("boolean is not an interface number")
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        return int(value, 0)
    raise ValueError(f"unsupported numeric value: {value!r}")


def validate_spec(spec: dict) -> dict:
    if spec.get("schema_version") != 1:
        raise ValueError("unsupported Genesis interface schema")
    if not 0 < number(spec["core_id"]) <= 0xFFFFFFFF:
        raise ValueError("Genesis core ID must fit a nonzero 32-bit word")
    for group in ("clocks_hz", "load_kinds", "sizes", "input_namespaces", "buttons", "regions", "config"):
        if not spec.get(group):
            raise ValueError(f"empty Genesis interface group: {group}")
        for name, value in spec[group].items():
            if re.fullmatch(r"[A-Z][A-Z0-9_]*", name) is None:
                raise ValueError(f"invalid Genesis constant name: {name}")
            if not 0 <= number(value) <= 0x7FFFFFFF:
                raise ValueError(f"Genesis constant out of range: {name}")
    for group in ("load_kinds", "input_namespaces"):
        values = [number(value) for value in spec[group].values()]
        if len(values) != len(set(values)) or any(value > 255 for value in values):
            raise ValueError(f"invalid or duplicate Genesis {group}")
    if sorted(number(value) for value in spec["buttons"].values()) != list(range(12)):
        raise ValueError("Genesis buttons must occupy bits zero through eleven")
    sizes = spec["sizes"]
    minimum, maximum = (number(sizes[f"GENESIS_CART_{suffix}"])
                        for suffix in ("MIN", "MAX"))
    if minimum < 512 or minimum > maximum or maximum > 8 * 1024 * 1024:
        raise ValueError("Genesis cartridge limits exceed the loader contract")
    if number(sizes["GENESIS_SAVE"]) != 65536:
        raise ValueError("Genesis save window must be 64 KiB")
    if number(sizes["GENESIS_CONFIG"]) != 16:
        raise ValueError("Genesis config must be sixteen bytes")
    if {name: number(value) for name, value in spec["regions"].items()} != {
            "AUTO": 0, "US": 1, "EU": 2, "JP": 3}:
        raise ValueError("Genesis region values do not match the loader contract")
    return spec


def load_spec() -> dict:
    return validate_spec(json.loads(SPEC_PATH.read_text(encoding="utf-8")))


def generate_sv(spec: dict) -> str:
    lines = ["// SPDX-License-Identifier: GPL-3.0-only",
             "// Generated from spec/genesis_interface_v0.json. Do not edit.",
             "`timescale 1ns / 1ps", "package multitini_genesis_defs_pkg;",
             f"    localparam logic [31:0] MT_GENESIS_CORE_ID = 32'h{number(spec['core_id']):08X};"]
    for name, value in spec["clocks_hz"].items():
        lines.append(f"    localparam integer MT_GENESIS_{name}_HZ = {number(value)};")
    for name, value in spec["load_kinds"].items():
        lines.append(f"    localparam logic [7:0] MT_LOAD_{name} = 8'h{number(value):02X};")
    for name, value in spec["sizes"].items():
        lines.append(f"    localparam integer MT_SIZE_{name} = {number(value)};")
    for name, value in spec["input_namespaces"].items():
        lines.append(f"    localparam logic [7:0] MT_INPUT_{name} = 8'h{number(value):02X};")
    lines.append("    localparam logic [7:0] MT_INPUT_GENESIS = MT_INPUT_GENESIS_GAMEPAD;")
    lines.append("    // BUTTON constants are bit indexes; bare names are pressed-button masks.")
    for name, value in spec["buttons"].items():
        lines.append(f"    localparam integer MT_GENESIS_BUTTON_{name} = {number(value)};")
        lines.append(f"    localparam logic [11:0] MT_GENESIS_{name} = 12'h{1 << number(value):03X};")
    for name, value in spec["regions"].items():
        lines.append(f"    localparam logic [7:0] MT_GENESIS_REGION_{name} = 8'd{number(value)};")
    for name, value in spec["config"].items():
        lines.append(f"    localparam logic [31:0] MT_GENESIS_CONFIG_{name} = 32'h{number(value):08X};")
    lines.extend(["endpackage", ""])
    return "\n".join(lines)


def generate_c(spec: dict) -> str:
    lines = ["/* SPDX-License-Identifier: GPL-3.0-only */",
             "/* Generated from spec/genesis_interface_v0.json. Do not edit. */",
             "#ifndef MULTITINI_GENESIS_INTERFACE_H",
             "#define MULTITINI_GENESIS_INTERFACE_H", "", "#include <stddef.h>",
             "#include <stdint.h>", "",
             f"#define MT_GENESIS_CORE_ID UINT32_C(0x{number(spec['core_id']):08X})"]
    for name, value in spec["clocks_hz"].items():
        lines.append(f"#define MT_GENESIS_{name}_HZ UINT32_C({number(value)})")
    for group, prefix, typedef in (("load_kinds", "MT_LOAD_", "load_kind"),
                                   ("input_namespaces", "MT_INPUT_", "input_namespace"),
                                   ("buttons", "MT_GENESIS_BUTTON_", "button")):
        lines.extend(["", "typedef enum {"])
        for name, value in spec[group].items():
            lines.append(f"    {prefix}{name} = 0x{number(value):02X},")
        lines.append(f"}} mt_genesis_{typedef}_t;")
    lines.append("")
    for name, value in spec["sizes"].items():
        lines.append(f"#define MT_SIZE_{name} ((size_t){number(value)})")
    lines.extend(["", "#define MT_INPUT_GENESIS MT_INPUT_GENESIS_GAMEPAD",
                  "/* BUTTON constants are bit indexes; bare names are pressed-button masks. */"])
    for name, value in spec["buttons"].items():
        lines.append(f"#define MT_GENESIS_{name} UINT16_C(0x{1 << number(value):03X})")
    lines.extend(["", "typedef enum {"])
    for name, value in spec["regions"].items():
        lines.append(f"    MT_GENESIS_REGION_{name} = {number(value)},")
    lines.append("} mt_genesis_region_t;")
    for name, value in spec["config"].items():
        lines.append(f"#define MT_GENESIS_CONFIG_{name} UINT32_C(0x{number(value):08X})")
    lines.extend(["", "#endif", ""])
    return "\n".join(lines)


def update_or_check(path: Path, expected: str, check: bool) -> bool:
    actual = path.read_text(encoding="utf-8") if path.exists() else ""
    if actual == expected:
        return True
    if check:
        sys.stderr.writelines(difflib.unified_diff(actual.splitlines(True),
            expected.splitlines(True), fromfile=str(path), tofile=f"generated:{path}"))
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(expected, encoding="utf-8")
    print(f"generated {path.relative_to(ROOT)}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    spec = load_spec()
    results = [update_or_check(SV_PATH, generate_sv(spec), args.check),
               update_or_check(C_PATH, generate_c(spec), args.check)]
    return 0 if all(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
