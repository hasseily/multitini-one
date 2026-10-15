#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Import or audit the exact licensed Apple IIgs source subset (no Apple ROMs)."""
from __future__ import annotations
import argparse
import csv
import fnmatch
import hashlib
import json
from pathlib import Path, PurePosixPath
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DESTINATION = "third_party/apple-iigs/upstream"
MANIFEST = ROOT / "third_party/apple-iigs/import_manifest.json"
SOURCE_NAME = "MiSTer Apple IIgs"
COMMIT = "9adbba8622f378253765b7d438b6cbcc4d03fc57"
URL = "https://github.com/MiSTer-devel/Apple-IIgs_MiSTer.git"
CPU_FILES = ["P65C816_pkg.sv", "P65C816.sv", "AddrGen.sv", "BCDAdder.v",
             "AddSubBCD.sv", "ALU.sv", "bit_adder.v", "adder4.v", "mcode.sv",
             "65C816.qip", "README.md"]
RTL_FILES = ["iigs.sv", "mmu.sv", "dpram.sv", "bram.sv", "dpram_bram.vhd",
             "rom.v", "syncram.v", "vgc.v", "dhr_lut.vh", "video_timing.v",
             "sound.v", "soundglu.v", "es5503.v", "adb.v", "prtc.v",
             "paddle_timer.v", "iwm_woz.v", "iwm_flux.v", "woz_floppy_controller.sv",
             "flux_drive.v", "smartport_dev.v", "hdd.v", "scc_iigs_wrapper.v",
             "scc8530.v", "clock_divider.v", "zipgs_regs.sv", "twgs_card.sv",
             "twgs_nvram.sv", "twgs_regs.sv", "twgs_rom.sv", "uart/rxuart.v",
             "uart/txuart.v"]
EXCLUDES = ["releases/**", "roms/**", "rtl/roms/*.hex", "sys/**/*.rbf", "rtl/pll/**",
            "**/*.bin", "**/*.rom", "**/*.hex", "**/*.mem", "**/*.mif", "**/*.rbf",
            "**/*.bit", "**/*.png", "**/*.jpg", "**/*.woz", "**/*.hdv"]

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def git_blob(source: Path, relative: str) -> bytes:
    return subprocess.check_output(["git", "-C", str(source), "show", f"{COMMIT}:{relative}"])

def source_pin(source: Path) -> None:
    head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if head != COMMIT:
        raise ValueError(f"IIgs checkout is not pinned to {COMMIT}: {head}")

def create_import(source: Path) -> None:
    source_pin(source)
    files = ["LICENSE", "README.md", "files.qip", "rtl/65C816/LICENSE", "sys/iir_filter.v",
             "rtl/roms/hdd.a65"] + ["rtl/65C816/" + name for name in CPU_FILES] + ["rtl/" + name for name in RTL_FILES]
    groups = {}
    imports = []
    rows = []
    for relative in sorted(files):
        data = git_blob(source, relative)
        path = ROOT / DESTINATION / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        text = data.decode("utf-8", errors="replace")
        if relative.endswith("LICENSE"):
            license_name = "License-text"
            basis = "Exact upstream GPL version 3 license text and distribution notice."
        elif relative in ("sys/iir_filter.v", "rtl/roms/hdd.a65"):
            license_name = "GPL-2.0-or-later"
            basis = "Explicit file-local GNU GPL version 2 or later grant, with author attribution preserved."
        elif "either version 3" in text:
            license_name = "GPL-3.0-or-later"
            basis = "Explicit file-local GNU GPL version 3 or later grant; upstream notices retained."
        else:
            license_name = "GPL-3.0"
            basis = "Repository GPL version 3 LICENSE covers otherwise unmarked subsidiary machine logic and documentation; imported without changes. The DHR table is the source-licensed GSSquared decoder table, not system ROM content."
        groups.setdefault((license_name, basis), []).append(relative)
        repository_path = f"{DESTINATION}/{relative}"
        imports.append({"path": repository_path, "source": SOURCE_NAME, "source_path": relative, "sha256": sha(data)})
        rows.append({"path": repository_path, "origin": SOURCE_NAME, "commit": COMMIT,
                     "license": license_name, "status": "source-reviewed", "notes": basis})
    manifest = {"schema_version": 1, "source": {"name": SOURCE_NAME, "url": URL, "commit": COMMIT},
                "destination": DESTINATION,
                "resolved_groups": [{"license": name, "basis": basis, "paths": paths}
                                    for (name, basis), paths in groups.items()],
                "unresolved": [], "excluded_globs": EXCLUDES,
                "notes": ["No Apple system, character, ADB, PRAM-default, or TransWarp ROM bytes are imported.",
                          "rtl/roms/hdd.a65 is GPL AppleWin controller firmware source. Build its 256-byte runtime ROM with ACME; do not copy upstream generated hex.",
                          "Intel BRAM wrapper is retained only as the latency/read-during-write reference; the active Multitini source is inferred RAM.",
                          "65C816 implementation microcode and the DHR decoder table are HDL source, distinct from Apple system firmware.",
                          "Platform PLL, SDRAM, MiSTer emu/hps_io and binaries are replaced by Multitini adapters; TWGS is absent in the initial port."]}
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    lock_path = ROOT / "provenance/imports.lock.json"
    lock = json.loads(lock_path.read_text())
    lock["imports"] = [row for row in lock["imports"] if row["source"] != SOURCE_NAME] + imports
    lock_path.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8")
    source_path = ROOT / "provenance/sources.lock.json"
    sources = json.loads(source_path.read_text())
    sources["sources"] = [row for row in sources["sources"] if row["name"] != SOURCE_NAME] + [
        {"name": SOURCE_NAME, "url": URL, "commit": COMMIT, "license": "GPL-3.0 and GPL-3.0-or-later; GPL-2.0-or-later HDD firmware and IIR filter notices retained",
         "use": "Exact machine logic and CPU subset; all Apple firmware supplied at runtime, inferred RAM and narrowly generated Multitini ports."}]
    source_path.write_text(json.dumps(sources, indent=2) + "\n", encoding="utf-8")
    csv_path = ROOT / "licenses/files.csv"
    with csv_path.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        fields = reader.fieldnames
        old_rows = [row for row in reader if row["origin"] != SOURCE_NAME]
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(old_rows + rows)

def audit(source: Path | None) -> int:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if (manifest["source"]["commit"] != COMMIT or manifest["source"]["url"] != URL or
            manifest["destination"] != DESTINATION or manifest["schema_version"] != 1 or manifest["unresolved"]):
        raise ValueError("IIgs import identity or rights resolution changed")
    expected = {}
    for group in manifest["resolved_groups"]:
        if not group["license"] or not group["basis"]:
            raise ValueError("IIgs license group lacks evidence")
        for relative in group["paths"]:
            path = PurePosixPath(relative)
            if (path.is_absolute() or ".." in path.parts or "\\" in relative or str(path) != relative or
                    relative in expected or any(fnmatch.fnmatchcase(relative, pattern) for pattern in EXCLUDES)):
                raise ValueError(f"Unsafe or forbidden IIgs import {relative}")
            expected[relative] = group["license"]
    destination = ROOT / DESTINATION
    actual = {path.relative_to(destination).as_posix() for path in destination.rglob("*") if path.is_file()}
    if actual != set(expected):
        raise ValueError(f"IIgs import census changed: {sorted(actual ^ set(expected))}")
    imports = {row["source_path"]: row for row in json.loads((ROOT / "provenance/imports.lock.json").read_text())["imports"]
               if row["source"] == SOURCE_NAME}
    if set(imports) != set(expected):
        raise ValueError("IIgs import lock differs from manifest")
    with (ROOT / "licenses/files.csv").open(newline="", encoding="utf-8") as stream:
        licenses = {row["path"]: row for row in csv.DictReader(stream)}
    if source:
        source_pin(source)
    for relative, license_name in expected.items():
        path = destination / relative
        repository_path = path.relative_to(ROOT).as_posix()
        row = imports[relative]
        if row["path"] != repository_path or sha(path.read_bytes()) != row["sha256"]:
            raise ValueError(f"IIgs source hash changed: {relative}")
        licensed = licenses.get(repository_path, {})
        if licensed.get("license") != license_name or licensed.get("commit") != COMMIT:
            raise ValueError(f"IIgs license row differs: {relative}")
        if source and sha(git_blob(source, relative)) != row["sha256"]:
            raise ValueError(f"IIgs source differs from upstream Git blob: {relative}")
    return len(expected)

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="compare to the pinned upstream Git checkout")
    parser.add_argument("--import", action="store_true", dest="do_import", help="copy approved source and update the exact provenance ledgers")
    args = parser.parse_args()
    if args.do_import:
        if not args.source:
            parser.error("--import requires --source")
        create_import(args.source)
    count = audit(args.source)
    print(f"Apple IIgs import: {count} files; exact hashes, commit, census and per-file licenses verified")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
