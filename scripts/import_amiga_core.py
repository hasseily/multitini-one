#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Import the pinned, redistributable Minimig machine source from a checkout.

Platform implementation is excluded from the build. Minimig.sv is retained
only as the licensed reference for its CPU clock-phase wiring. No system ROM,
media, generated Intel IP or drive samples are imported. The checkout is only
an input; all retained files are copied byte-for-byte.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PIN = "e47c3457b486d09daaf47c8934bda90cf7723045"
URL = "https://github.com/MiSTer-devel/Minimig-AGA_MiSTer.git"
EXCLUDED = {
    "bram.vhd", "denise_colortable_ram_mf.v", "sdram_ctrl.v",
    "ddram_ctrl.v", "cpu_cache_new.v", "fastchip.v", "rtg.v", "pll.v",
    "akiko.v", "akiko_hps_bridge.v", "akiko_nvram.v", "cdda.v",
    "drive_sounds.v", "chipdma_arb.v", "cdtv_bridge.v", "cdtv_nvram.v",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def selected(cache: Path) -> list[str]:
    paths = ["README.md", "gpl.txt", "Minimig.sv"]
    for p in sorted((cache / "rtl").glob("*.v")):
        if p.name not in EXCLUDED and not p.name.startswith("MiSTerFloppy"):
            paths.append(p.relative_to(cache).as_posix())
    paths.append("rtl/minimig_version.vh")
    for directory in ("rtl/fx68k", "rtl/tg68k"):
        for p in sorted((cache / directory).iterdir()):
            if p.suffix in (".vhd", ".sv", ".vh", ".mem", ".mif", ".txt"):
                paths.append(p.relative_to(cache).as_posix())
    return paths


def file_license(name: str, data: bytes) -> tuple[str, str]:
    text = data.decode("utf-8")
    if "GNU Lesser General Public License" in text:
        return "LGPL-3.0-or-later", "File-local grant; project GPL also retained"
    if "Redistribution and use in source and binary forms" in text:
        return "BSD-2-Clause", "File-local two-clause BSD grant retained"
    if name.startswith("rtl/fx68k/"):
        return "GPL-3.0-or-later", "Grant in retained rtl/fx68k/fx68k.txt"
    return "GPL-3.0-or-later", "Project-wide grant in retained README.md License section"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("checkout", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    pin = subprocess.check_output(["git", "-C", str(args.checkout), "rev-parse", "HEAD"], text=True).strip()
    if pin != PIN:
        raise SystemExit(f"Expected {PIN}, found {pin}")
    imported = []
    rows = []
    for name in selected(args.checkout):
        data = (args.checkout / name).read_bytes()
        target = ROOT / "third_party/amiga/upstream" / name
        if args.check:
            if not target.is_file() or target.read_bytes() != data:
                raise SystemExit(f"Import mismatch: {name}")
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        lic, notes = file_license(name, data)
        imported.append({"path": name, "sha256": digest(data), "license": lic, "license_evidence": notes})
        rows.append([target.relative_to(ROOT).as_posix(), "MiSTer Minimig", PIN, lic, "source-reviewed", notes])
    manifest = {
        "schema_version": 1, "source": URL, "commit": PIN,
        "purpose": "ECS A600 chipset and stock FX68K / accelerated TG68K CPU; external memory and host services",
        "exclusions": ["Intel-generated IP and bram.vhd", "MiSTer HPS/platform implementation excluded from build; Minimig.sv is reference only", "system ROMs and media", "drive samples", "RTG/CD32/CDTV/A2065/Toccata", "physical/flux floppy interfaces"],
        "files": imported,
    }
    manifest_path = ROOT / "provenance/amiga.import.json"
    expected = json.dumps(manifest, indent=2) + "\n"
    if args.check:
        if manifest_path.read_text() != expected:
            raise SystemExit("Amiga import manifest mismatch")
    else:
        manifest_path.write_text(expected)
        lock_path = ROOT / "provenance/sources.lock.json"
        lock = json.loads(lock_path.read_text())
        lock["sources"] = [s for s in lock["sources"] if s["name"] != "MiSTer Minimig ECS Amiga"]
        lock["sources"].append({"name": "MiSTer Minimig ECS Amiga", "url": URL, "commit": PIN,
                                "license": "GPL-3.0-or-later, LGPL-3.0-or-later TG68K, BSD-2-Clause IDE",
                                "use": "Selected machine source and CPU microcode only; no Intel IP, MiSTer platform, Kickstart, disks, samples or binary FPGA images. Per-file provenance/amiga.import.json."})
        lock_path.write_text(json.dumps(lock, indent=2) + "\n")
        csv_path = ROOT / "licenses/files.csv"
        with csv_path.open(newline="", encoding="utf-8") as stream:
            existing = list(csv.reader(stream))
        existing = [r for r in existing if not r[0].startswith("third_party/amiga/")]
        with csv_path.open("w", newline="", encoding="utf-8") as stream:
            csv.writer(stream, lineterminator="\n").writerows(existing + rows)
    print(f"Verified {len(imported)} pinned Minimig source files" if args.check else f"Imported {len(imported)} pinned Minimig source files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
