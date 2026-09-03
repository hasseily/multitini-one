#!/usr/bin/env python3
"""Convert the pinned open-source MiSTer CGB bootstrap MIF to XPM/readmemh."""

from __future__ import annotations

import argparse
import difflib
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party" / "gameboy" / "upstream" / "BootROMs" / "cgb_boot.mif"
OUTPUT = ROOT / "hdl" / "core" / "gameboy" / "boot" / "cgb_boot.mem"


def convert() -> str:
    memory: list[int | None] = [None] * 4096
    line_pattern = re.compile(r"^\s*([0-9A-Fa-f]+)\s*:\s*([^;]+);\s*$")
    for line in SOURCE.read_text(encoding="utf-8").splitlines():
        match = line_pattern.match(line)
        if match is None:
            continue
        address = int(match.group(1), 16)
        values = [int(value, 16) for value in match.group(2).split()]
        for offset, value in enumerate(values):
            index = address + offset
            if index >= len(memory) or memory[index] is not None or value > 0xFF:
                raise ValueError(f"invalid or duplicate MIF byte at 0x{index:04X}")
            memory[index] = value
    if any(value is None for value in memory):
        raise ValueError("CGB bootstrap MIF does not define all 4096 bytes")
    return "\n".join(f"{value:02x}" for value in memory) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    expected = convert()
    actual = OUTPUT.read_text(encoding="ascii") if OUTPUT.exists() else ""
    if actual == expected:
        return 0
    if args.check:
        sys.stderr.writelines(difflib.unified_diff(
            actual.splitlines(True), expected.splitlines(True),
            fromfile=str(OUTPUT), tofile=f"generated:{OUTPUT}"))
        return 1
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(expected, encoding="ascii")
    print(f"generated {OUTPUT.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
