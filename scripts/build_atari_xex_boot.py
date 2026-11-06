#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the built-in Atari XEX bootstrap and its page relocations."""

import argparse
from pathlib import Path
import re
import subprocess
from tempfile import TemporaryDirectory


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "firmware/atari_xex_boot.a65"
HEADER = ROOT / "firmware/common/atari_xex_boot.h"
BOOT_ADDRESS = 0x0700
SECTOR_SIZE = 128


def assemble(acme, work, address):
    output = work / "boot.bin"
    labels = work / "boot.labels"
    subprocess.run([acme, "-f", "plain", "-o", str(output), "-l", str(labels),
                    f"-DLOAD_ADDRESS={address}", str(SOURCE)], check=True)
    symbols = {name: int(value, 16) for name, value in re.findall(
        r"^\s*(\w+)\s*=\s*\$([0-9a-f]+)", labels.read_text(encoding="ascii"), re.M)}
    return output.read_bytes(), symbols


def generate(acme):
    with TemporaryDirectory() as directory:
        work = Path(directory)
        data, symbols = assemble(acme, work, BOOT_ADDRESS)
        next_page, _ = assemble(acme, work, BOOT_ADDRESS + 0x100)
        final_page, _ = assemble(acme, work, 0x9700)
    if len(data) % SECTOR_SIZE or data[1] != len(data) // SECTOR_SIZE:
        raise ValueError("Invalid boot sector length")
    if len(next_page) != len(data) or len(final_page) != len(data):
        raise ValueError("Loader length changes with page")
    relocations = []
    for index, (base, next_byte, final_byte) in enumerate(zip(data, next_page, final_page)):
        if base == next_byte == final_byte:
            continue
        if next_byte != base + 1 or final_byte != base + (0x9700 - BOOT_ADDRESS) // 256:
            raise ValueError(f"Loader byte {index} is not page relocatable")
        relocations.append(index)

    def lines(values, formatter):
        return "\n".join("    " + ", ".join(map(formatter, values[i:i + 12])) + ","
                         for i in range(0, len(values), 12))

    return f"""/* SPDX-License-Identifier: GPL-3.0-only */
/* Generated from firmware/atari_xex_boot.a65 by scripts/build_atari_xex_boot.py. */
#ifndef MULTITINI_ATARI_XEX_BOOT_H
#define MULTITINI_ATARI_XEX_BOOT_H

#include <stdint.h>

#define MT_ATARI_XEX_BOOT_ADDRESS 0x{BOOT_ADDRESS:04x}U
#define MT_ATARI_XEX_BOOT_BYTES {len(data)}U
#define MT_ATARI_XEX_BOOT_SECTORS {len(data) // SECTOR_SIZE}U
#define MT_ATARI_XEX_LENGTH_OFFSET {symbols['file_length'] - BOOT_ADDRESS}U
#define MT_ATARI_XEX_RUN_OFFSET {symbols['default_run'] - BOOT_ADDRESS}U

static const uint8_t mt_atari_xex_boot[MT_ATARI_XEX_BOOT_BYTES] = {{
{lines(data, lambda value: f'0x{value:02x}')}
}};

static const uint16_t mt_atari_xex_relocations[] = {{
{lines(relocations, lambda value: f'{value}U')}
}};

#endif
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--acme", default="acme", help="ACME cross-assembler executable")
    parser.add_argument("--check", action="store_true", help="verify the checked-in generated header")
    args = parser.parse_args()
    expected = generate(args.acme)
    if args.check:
        if not HEADER.is_file() or HEADER.read_text(encoding="ascii") != expected:
            parser.exit(1, "Atari XEX boot header needs regeneration\n")
    else:
        HEADER.write_text(expected, encoding="ascii", newline="\n")


if __name__ == "__main__":
    main()
