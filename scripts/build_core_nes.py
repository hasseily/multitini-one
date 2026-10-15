#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the NES image, or audit explicitly selected earlier core images.

The default builds only NES. Select additional images explicitly with --image;
retain existing compatible images with the shared --reuse-image mechanism.
"""
import os
import sys
import build_phos1

build_phos1.BUILD_ID = "4D000500"
build_phos1.DEFAULT_OUTPUT = build_phos1.ROOT / "build/core-nes/hardware"
build_phos1.MATRIX["apple-iigs"] = ("APPLE_IIGS", False, False)
build_phos1.MATRIX["nes"] = ("NES", False, False)
# A parallel Windows IP startup failed in Vivado's Tcl-app setup. Start IP
# children serially; the captured NES hook also selects serial synthesis.
os.environ.setdefault("MULTITINI_VIVADO_JOBS", "1")

if __name__ == "__main__":
    arguments = sys.argv[1:]
    if not any(value == "--image" or value.startswith("--image=") for value in arguments):
        arguments += ["--image", "nes"]
    raise SystemExit(build_phos1.main(arguments))
