#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Ensure the shared validator cannot provide a runtime's linked identity."""

from __future__ import annotations

import argparse
from pathlib import Path

MARKERS = (
    b"APPLETINI-MULTIBOOT-V1",
    b"MULTITINI-MULTIBOOT-V1",
    b"APPLETINI-MULTIBOOT-GOLDEN-V1",
    b"APPLETINI-GOLDEN-RECOVERY-V1",
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("objects", nargs="+", type=Path,
                        help="compiled production multiboot validator objects")
    args = parser.parse_args()
    for path in args.objects:
        compiled = path.read_bytes()
        for marker in MARKERS:
            if marker in compiled:
                raise AssertionError(
                    f"Shared validator embeds linked identity {marker.decode()}: {path}"
                )
    print("PASS: shared multiboot objects contain no linked runtime capability markers")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
