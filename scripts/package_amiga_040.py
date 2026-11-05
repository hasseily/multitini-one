#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Stage a timing-qualified A1200 + AP68040 image beside the standard Amiga."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

import core_artifacts

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hardware", type=Path, help="Completed amiga-aga-040 image directory")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--bootgen", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build") or output.exists():
        parser.error("--output must be a fresh directory under build/")

    hardware = args.hardware.resolve()
    artifact = core_artifacts.inspect_artifact(hardware, "AMIGA", amiga_aga_pal=True,
                                               amiga_ap68040=True, require_current_inputs=True)
    major, minor = artifact["abi"].split(".")
    core = output / "sd/multitini/cores/amiga-040"
    # The existing firmware chooses the A1200 ROM and chipset by variant ID.
    bit = hardware / "multitini_one.runs/impl_1/multitini_one_top.bit"
    command = ["--id", "amiga-040", "--name", "Amiga 1200 AGA + 68040",
               "--core-id", "0x414D4947", "--abi-major", major, "--abi-minor", minor,
               "--content-root", "0:/multitini/cores/amiga-040/media", "--output", str(core),
               "--variant", f"aga-pal|Amiga 1200 AGA + 68040 (28.375 MHz)|{bit}"]
    if args.bootgen is not None:
        command += ["--bootgen", str(args.bootgen)]
    subprocess.run([sys.executable, str(ROOT / "scripts/make_core_package.py"), *command],
                   cwd=ROOT, check=True)

    (core / "media").mkdir(exist_ok=True)
    (core / "roms").mkdir(exist_ok=True)
    (core / "roms/readme.txt").write_text(
        "Supply an A1200 Kickstart ROM as kick-a1200.rom.\n"
        "Use a Workbench disk with 68040 support in the media folder.\n", encoding="utf-8")
    evidence = output / "evidence"
    evidence.mkdir()
    for name in ("source-inputs.json", "source-inputs.sources.tar.gz", "source-inputs.zip", "build.log"):
        shutil.copy2(hardware / name, evidence / name)
    shutil.copytree(hardware / "reports", evidence / "reports")
    (evidence / "artifact.json").write_text(json.dumps(artifact, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
