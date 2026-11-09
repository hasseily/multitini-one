#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the local Atari-XT FPGA prototype with the shared timing gates."""
import os
import sys

import build_phos1


if __name__ == "__main__":
    build_phos1.BUILD_ID = "4D000850"
    # Keep the vendor clock wizard's generated paths below Win32 MAX_PATH.
    build_phos1.DEFAULT_OUTPUT = build_phos1.ROOT / "build/a850"
    build_phos1.MATRIX = {name: ("ATARI_XT", False, False)
                         for name in ("atari-800xl", "atari-320xe", "atari-320xe-compat")}
    os.environ.setdefault("MULTITINI_VIVADO_JOBS", "1")
    raise SystemExit(build_phos1.main(sys.argv[1:], default_images=("atari-800xl",)))
