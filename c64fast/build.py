#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the Multitini fast-disk ROM pair from the stock C64 KERNAL and 1541-II ROMs.

Private generated outputs (in build/c64fast/bin/):
  kernal.bin   8 KiB patched KERNAL 901227-03  -> SD card roms/fast/kernal.bin
  1541.bin     16 KiB patched 1541-II 251968-03 -> SD card roms/fast/1541.bin
  engine.bin   the drive engine image (also embedded in kernal.bin)
  PROVENANCE.json  input/output digests and the byte ranges that changed

Every changed byte must fall inside a region listed in ALLOWED_* below; the
regions are the code retired or free in the stock images (see
docs/C64_FAST_DISK.md).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(ROOT / "scripts"))
from private_core_roms import default_rom_source
ROMS = default_rom_source() / "c64/roms"
STOCK_KERNAL = ROMS / "kernal.bin"
STOCK_DOS = ROMS / "1541.bin"
STOCK_KERNAL_SHA256 = "83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721"
STOCK_DOS_SHA256 = "326c289c38753323d7e8167897447cf61ef35189d82eb8d75210ece949adda7c"

BUILD = ROOT / "build/c64fast/work/build"
BIN = ROOT / "build/c64fast/bin"

# Inclusive address ranges that a patch may modify.
ALLOWED_KERNAL = [
    (0xED0E, 0xED10),  # TALK/LISTEN RS-232 idle wait
    (0xED19, 0xED1B),  # pending-byte flush hook
    (0xED3D, 0xED3F),  # ATN delay hook
    (0xEDBB, 0xEDBD),  # SECOND hook
    (0xEDC9, 0xEDCB),  # TKSA hook
    (0xEDE7, 0xEDE9),  # CIOUT hook
    (0xEE13, 0xEE17),  # ACPTR hook
    (0xEEBB, 0xF0BC),  # RS-232 core block
    (0xF140, 0xF141), (0xF144, 0xF145),  # GETIN branches
    (0xF175, 0xF17D),  # CHRIN device 1/2 stub
    (0xF1DB, 0xF1E4),  # CHROUT device 1/2 stub
    (0xF223, 0xF225), (0xF268, 0xF26A), (0xF2A7, 0xF2A9), (0xF384, 0xF386),
    (0xF409, 0xF49D),  # RS-232 OPEN block
    (0xF4D0, 0xF4D4),  # LOAD TKSA hook
    (0xF533, 0xF535),  # LOAD device 1/2 stub
    (0xF618, 0xF619),  # SAVE helper relocation operand
    (0xF659, 0xF68D),  # SAVE device 1/2 stub + relocated helper
    (0xF72C, 0xFCD0),  # tape blocks (merged)
    (0xFD9B, 0xFDA2),  # tape IRQ vector table
    (0xFE54, 0xFE55), (0xFE64, 0xFE65),  # NMI RS-232 dispatch bypass
    (0xFE72, 0xFEBB), (0xFEC2, 0xFF47),  # NMI RS-232 handlers, baud table
]
ALLOWED_DOS = [
    (0xC09F, 0xC0FF),  # lower filler
    (0xE780, 0xE7A2),  # init hook NOP sled
    (0xE7A3, 0xE852),  # retired ampersand loader
    (0xE86D, 0xE86F),  # ATN entry hook
    (0xE8C5, 0xE8CC),  # SA decode hook
    (0xE916, 0xE918),  # TALK loop hook
    (0xEA44, 0xEA46),  # LISTEN loop hook
    (0xEA6E, 0xEA9F),  # self-test blinker
    (0xEAA2, 0xEAA3),  # reset DDRA
    (0xEAA7, 0xEB1E),  # self test
    (0xFA10, 0xFA11),  # stepper fast-path branch
    (0xFA1C, 0xFA2D), (0xFA7B, 0xFABD),  # stepper fast path (dead)
    (0xFE0E, 0xFE2F),  # dead write routine
    (0xFE90, 0xFE90),  # command table
    (0xFF79, 0xFFE5),  # upper filler
]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def find_acme() -> str:
    for candidate in ("acme", "acme.exe"):
        path = shutil.which(candidate)
        if path:
            return path
    fallback = Path.home() / "tools" / "acme" / "acme.exe"
    if fallback.exists():
        return str(fallback)
    raise SystemExit("acme assembler not found on PATH")


def assemble(acme: str, source: str) -> Path:
    stem = Path(source).stem
    symbols = BUILD / f"{stem}.sym"
    result = subprocess.run(
        [acme, "--symbollist", str(symbols), source],
        cwd=BUILD.parent, capture_output=True, text=True)
    noise = "Segment starts inside another one"
    errors = [line for line in result.stderr.splitlines() if noise not in line]
    if result.returncode != 0:
        raise SystemExit(f"acme failed for {source}:\n" + "\n".join(errors))
    return symbols


def changed_ranges(stock: bytes, patched: bytes, base: int) -> list:
    ranges = []
    for index, (old, new) in enumerate(zip(stock, patched)):
        if old == new:
            continue
        address = base + index
        if ranges and ranges[-1][1] == address - 1:
            ranges[-1][1] = address
        else:
            ranges.append([address, address])
    return [tuple(item) for item in ranges]


def check_allowed(name: str, ranges: list, allowed: list) -> None:
    for start, end in ranges:
        if not any(a <= start and end <= b for a, b in allowed):
            raise SystemExit(
                f"{name}: changed bytes ${start:04X}-${end:04X} are outside the allowed regions")


def load_symbols(path: Path) -> dict:
    text = path.read_text(encoding="utf-8")
    return {m.group(1): int(m.group(2), 16)
            for m in re.finditer(r"^\s*(\w+)\s*=\s*\$([0-9a-fA-F]+)", text, re.M)}


def build() -> dict:
    stock_kernal = STOCK_KERNAL.read_bytes()
    stock_dos = STOCK_DOS.read_bytes()
    if sha256(stock_kernal) != STOCK_KERNAL_SHA256:
        raise SystemExit("stock KERNAL digest mismatch")
    if sha256(stock_dos) != STOCK_DOS_SHA256:
        raise SystemExit("stock 1541-II ROM digest mismatch")
    BUILD.mkdir(parents=True, exist_ok=True)
    inputs = BUILD.parent / "inputs"
    inputs.mkdir(exist_ok=True)
    (inputs / "kernal.bin").write_bytes(stock_kernal)
    (inputs / "1541.bin").write_bytes(stock_dos)
    for source in HERE.iterdir():
        if source.suffix.lower() in {".a", ".inc"}:
            text = source.read_text()
            text = text.replace('../third_party/c64/ROMs/kernal.901227-03.bin', 'inputs/kernal.bin')
            text = text.replace('../third_party/c64/ROMs/1541-II.251968-03.bin', 'inputs/1541.bin')
            (BUILD.parent / source.name).write_text(text)
    acme = find_acme()
    engine_symbols = assemble(acme, "engine.a")
    dos_symbols = assemble(acme, "dos_fast.a")
    kernal_symbols = assemble(acme, "kernal_fast.a")

    engine = (BUILD / "engine.bin").read_bytes()
    kernal = (BUILD / "kernal.bin").read_bytes()
    dos = (BUILD / "1541.bin").read_bytes()
    if len(kernal) != 8192 or len(dos) != 16384:
        raise SystemExit("unexpected output sizes")
    kernal_ranges = changed_ranges(stock_kernal, kernal, 0xE000)
    dos_ranges = changed_ranges(stock_dos, dos, 0xC000)
    check_allowed("kernal", kernal_ranges, ALLOWED_KERNAL)
    check_allowed("1541", dos_ranges, ALLOWED_DOS)

    ksym = load_symbols(kernal_symbols)
    dsym = load_symbols(dos_symbols)
    esym = load_symbols(engine_symbols)
    version = subprocess.run([acme, "--version"], capture_output=True, text=True).stdout.strip()
    report = {
        "schema_version": 1,
        "assembler": version.splitlines()[0] if version else "acme",
        "inputs": {
            "kernal": {"path": str(STOCK_KERNAL), "sha256": sha256(stock_kernal)},
            "1541": {"path": str(STOCK_DOS), "sha256": sha256(stock_dos)},
        },
        "outputs": {
            "kernal.bin": {"size": len(kernal), "sha256": sha256(kernal),
                           "changed_bytes": sum(b - a + 1 for a, b in kernal_ranges),
                           "changed_ranges": [f"${a:04X}-${b:04X}" for a, b in kernal_ranges]},
            "1541.bin": {"size": len(dos), "sha256": sha256(dos),
                         "changed_bytes": sum(b - a + 1 for a, b in dos_ranges),
                         "changed_ranges": [f"${a:04X}-${b:04X}" for a, b in dos_ranges]},
            "engine.bin": {"size": len(engine), "sha256": sha256(engine),
                           "load_address": f"${esym.get('eng_start', 0) & 0xFFFF:04X}"},
        },
        "regions": {
            "kernal_fast_code": f"${ksym['code_start']:04X}-${ksym['code_end'] - 1:04X}",
            "kernal_engine_image": f"${ksym['engine_image']:04X}-${ksym['engine_image_end'] - 1:04X}",
            "kernal_load_code": f"${ksym['code2_start']:04X}-${ksym['code2_end'] - 1:04X}",
            "dos_regions": {key: f"${dsym[key + '_start']:04X}-${dsym[key + '_end'] - 1:04X}"
                            for key in ("r1", "r2", "r3", "r4", "r5", "r6")},
        },
    }
    return {"kernal": kernal, "dos": dos, "engine": engine, "report": report}


def main() -> int:
    global STOCK_KERNAL, STOCK_DOS, BUILD, BIN
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rom-source", type=Path, default=default_rom_source())
    parser.add_argument("--output", type=Path, default=ROOT / "build/c64fast")
    parser.add_argument("--check", action="store_true",
                        help="rebuild and compare with the supplied private roms/fast pair")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build") or output == ROOT / "build":
        parser.error("Private generated outputs must stay under build/")
    STOCK_KERNAL = args.rom_source / "c64/roms/kernal.bin"
    STOCK_DOS = args.rom_source / "c64/roms/1541.bin"
    BUILD, BIN = output / "work/build", output / "bin"
    result = build()
    outputs = {"kernal.bin": result["kernal"], "1541.bin": result["dos"], "engine.bin": result["engine"]}
    if args.check:
        comparison = args.rom_source / "c64/roms/fast"
        mismatches = [name for name, data in outputs.items() if name != "engine.bin" and
                      (not (comparison / name).exists() or (comparison / name).read_bytes() != data)]
        if mismatches:
            print("private fast ROM pair differs from a fresh build:", ", ".join(mismatches))
            return 1
        print("private fast ROM pair matches a fresh build")
        return 0
    BIN.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        (BIN / name).write_bytes(data)
    (BIN / "PROVENANCE.json").write_text(json.dumps(result["report"], indent=2) + "\n",
                                         encoding="utf-8", newline="\n")
    report = result["report"]
    for name in ("kernal.bin", "1541.bin", "engine.bin"):
        info = report["outputs"][name]
        print(f"{name}: {info['size']} bytes, sha256 {info['sha256'][:16]}...")
    print("kernal changed bytes:", report["outputs"]["kernal.bin"]["changed_bytes"])
    print("1541 changed bytes:", report["outputs"]["1541.bin"]["changed_bytes"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
