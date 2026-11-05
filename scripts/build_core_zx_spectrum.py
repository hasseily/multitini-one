#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the ZX Spectrum image with the shared source and timing gates."""
import os
import sys

import build_phos1


if __name__ == "__main__":
    build_phos1.BUILD_ID = "4D000845"
    build_phos1.DEFAULT_OUTPUT = build_phos1.ROOT / "build/zx-spectrum/hardware"
    build_phos1.MATRIX = {"zx-spectrum": ("ZX_SPECTRUM", False, False)}
    os.environ.setdefault("MULTITINI_VIVADO_JOBS", "1")
    raise SystemExit(build_phos1.main(sys.argv[1:]))
