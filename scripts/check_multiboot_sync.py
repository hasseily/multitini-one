#!/usr/bin/env python3
"""Verify the shared dual-boot protocol and golden source in both checkouts."""
from pathlib import Path
import argparse

ROOT = Path(__file__).resolve().parents[1]
SHARED_LIB = ("multiboot.c", "multiboot.h", "multiboot_platform.c",
              "multiboot_platform.h", "golden_self_update.c")
SHARED_BOOT = ("boot_control.c", "boot_control.h", "main.c", "updater.c", "updater.h", "updater_layout.c",
               "updater_layout.h", "serial_boot.c")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--appletini-repo", required=True, type=Path)
    args = parser.parse_args()
    app = args.appletini_repo.resolve()
    pairs = [(ROOT / "firmware/golden/lib" / name,
              app / "ps_sources/lib" / name) for name in SHARED_LIB]
    pairs += [(ROOT / "firmware/golden/bootloader" / name,
               app / "ps_sources/bootloader" / name) for name in SHARED_BOOT]
    pairs += [(ROOT / "scripts/image_manifest.py", app / "scripts/image_manifest.py")]
    errors = []
    for left, right in pairs:
        if not left.is_file() or not right.is_file() or left.read_bytes() != right.read_bytes():
            errors.append(f"shared source differs: {left} / {right}")
    # These intentional branch differences must agree with the fixed slots.
    for tree, product, capacity in ((ROOT, "multitini", "0x007E0000"),
                                    (app, "appletini", "0x00600000")):
        fw = (tree / "scripts/make_firmware_bin.bat").read_text()
        boot = (tree / "scripts/make_boot_bin.bat").read_text()
        if f"--product {product} --max-size {capacity}" not in fw:
            errors.append(f"product/slot packaging mismatch in {tree}")
        if "--role golden --recovery-capable --max-size 0x100000" not in boot:
            errors.append(f"golden slot packaging mismatch in {tree}")
    if errors:
        print("\n".join(errors))
        return 1
    print(f"PASS: {len(pairs)} shared multiboot files match; firmware product tags and capacities differ as intended")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
