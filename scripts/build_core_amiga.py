#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build Amiga ECS, AGA, or AGA + AP68040 with routed timing gates.

Only Amiga is built by default. Other images require an explicit --image.
"""
import os
import sys
import build_phos1

build_phos1.BUILD_ID = "4D000811"
build_phos1.DEFAULT_OUTPUT = build_phos1.ROOT / "build/amiga-ecs/hardware"
build_phos1.MATRIX["apple-iigs"] = ("APPLE_IIGS", False, False)
build_phos1.MATRIX["nes"] = ("NES", False, False)
build_phos1.MATRIX["amiga"] = ("AMIGA", False, False)
build_phos1.MATRIX["amiga-aga"] = ("AMIGA", False, False)
build_phos1.MATRIX["amiga-aga-040"] = ("AMIGA", False, False)
os.environ.setdefault("MULTITINI_VIVADO_JOBS", "1")

if __name__ == "__main__":
    arguments = sys.argv[1:]
    if not any(value == "--image" or value.startswith("--image=") for value in arguments):
        arguments += ["--image", "amiga"]
    raise SystemExit(build_phos1.main(arguments))
