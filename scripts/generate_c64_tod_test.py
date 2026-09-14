#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Wrap the actual FPGA64 TOD process for its focused VHDL regression."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "hdl/core/c64/ports/fpga64_sid_iec.vhd"


def generate(destination: Path) -> None:
    source = SOURCE.read_text(encoding="utf-8")
    matches = re.findall(
        r"(?ims)^tod_clock:\s*process\(clk32\).*?^end process;", source
    )
    if len(matches) != 1:
        raise ValueError("expected exactly one labeled production TOD process")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(
        "-- Generated from hdl/core/c64/ports/fpga64_sid_iec.vhd.\n"
        "-- The production process is copied verbatim; do not edit.\n"
        "library ieee;\nuse ieee.std_logic_1164.all;\n"
        "entity c64_tod_test is\n"
        "  port(clk32, reset, ntscMode, sysEnable : in std_logic;\n"
        "       todclk : out std_logic);\n"
        "end entity;\n"
        "architecture rtl of c64_tod_test is\nbegin\n"
        + matches[0]
        + "\nend architecture;\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    generate(args.output)
