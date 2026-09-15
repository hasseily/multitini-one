#!/usr/bin/env python3
"""Check source provenance, license coverage, and restricted-content policy."""

from __future__ import annotations

import csv
import fnmatch
import hashlib
import json
import re
import subprocess
import sys
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESTRICTED_SUFFIXES = {
    ".2mg", ".bin", ".bit", ".crt", ".d64", ".d81", ".elf", ".g64",
    ".hex", ".img", ".mem", ".mif", ".po", ".prg", ".rbf", ".reu",
    ".rom", ".tap", ".t64", ".woz",
}
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".hpp", ".py", ".sv", ".tcl", ".v", ".vhd", ".vhdl", ".xdc"}
GENERATED_PREFIXES = ("build/", "build-", "project/", "vitis_workspace/", ".Xil/")
FONT_PATH = "hdl/common/multitini_font7x8.mem"
FONT_SHA256 = "be62a9d5ced50db55482084b61a6afd8ceec7e8a40f5cdd8506931718060e389"
C64_ROM_PATHS = (
    "third_party/c64/ROMs/basic.901226-01.bin",
    "third_party/c64/ROMs/kernal.901227-03.bin",
    "third_party/c64/ROMs/characters.901225-01.bin",
)
ALLOWED_RESTRICTED_FILES = {
    # Open-source SameBoy-derived bootstrap, plus its deterministic Vivado/
    # readmemh representation. Both are pinned and covered by the MIT notice.
    "third_party/gameboy/upstream/BootROMs/cgb_boot.mif",
    "hdl/core/gameboy/boot/cgb_boot.mem",
    # GPL font derived from Appletini fb16 at the commit in licenses/files.csv.
    FONT_PATH,
    # Exact C64 ROM set covered by provenance/c64_roms.json and the explicit
    # repository-owner inclusion decision recorded there.
    *C64_ROM_PATHS,
}


def candidate_files() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    # git ls-files keeps tracked paths in its output until a deletion is
    # committed. Policy applies to files present in the candidate tree, not
    # obsolete paths that this change intentionally removes.
    return sorted(
        path
        for path in result.stdout.decode().split("\0")
        if path and (ROOT / path).is_file()
    )


def load_license_patterns() -> list[str]:
    with (ROOT / "licenses" / "files.csv").open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    required = {"path", "origin", "commit", "license", "status", "notes"}
    if not rows or set(rows[0]) != required:
        raise ValueError("licenses/files.csv has the wrong columns")
    return [row["path"] for row in rows]


def sha256(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            result.update(block)
    return result.hexdigest()


def covered(path: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)


def check_c64_rom_provenance(errors: list[str]) -> None:
    try:
        record = json.loads(
            (ROOT / "provenance" / "c64_roms.json").read_text(encoding="utf-8")
        )
    except (OSError, ValueError) as exc:
        errors.append(f"invalid C64 ROM provenance: {exc}")
        return

    if record.get("schema_version") != 1:
        errors.append("C64 ROM provenance has unsupported schema")

    roms = record.get("roms")
    if not isinstance(roms, list) or not all(isinstance(item, dict) for item in roms):
        errors.append("C64 ROM provenance has no valid ROM list")
        return
    if [item.get("path") for item in roms] != list(C64_ROM_PATHS):
        errors.append("C64 ROM provenance paths or order do not match the allowlist")

    bundle_parts: list[bytes] = []
    for item in roms:
        relative = item.get("path")
        if not isinstance(relative, str) or not relative:
            errors.append("C64 ROM provenance contains an invalid path")
            continue
        target = ROOT / relative
        expected_digest = item.get("sha256")
        if not target.is_file():
            errors.append(f"provenance-locked C64 ROM is absent: {relative}")
            continue
        if target.stat().st_size != item.get("size"):
            errors.append(f"provenance-locked C64 ROM has wrong size: {relative}")
        if (
            not isinstance(expected_digest, str)
            or re.fullmatch(r"[0-9a-f]{64}", expected_digest) is None
        ):
            errors.append(f"C64 ROM provenance has invalid digest: {relative}")
        elif sha256(target) != expected_digest:
            errors.append(f"provenance-locked C64 ROM changed: {relative}")
        bundle_parts.append(target.read_bytes())

    bundle = record.get("bundle")
    if not isinstance(bundle, dict):
        errors.append("C64 ROM provenance has no valid bundle record")
        return
    if bundle.get("order") != list(C64_ROM_PATHS):
        errors.append("C64 ROM bundle order does not match the allowlist")
    if len(bundle_parts) != len(C64_ROM_PATHS):
        return
    bundle_bytes = b"".join(bundle_parts)
    if len(bundle_bytes) != bundle.get("size"):
        errors.append("C64 ROM bundle size does not match its provenance")
    crc32 = f"0x{zlib.crc32(bundle_bytes) & 0xFFFFFFFF:08X}"
    if crc32 != bundle.get("crc32"):
        errors.append("C64 ROM bundle CRC32 does not match its provenance")
    digest = hashlib.sha256(bundle_bytes).hexdigest()
    if digest != bundle.get("sha256"):
        errors.append("C64 ROM bundle SHA-256 does not match its provenance")


def main() -> int:
    errors: list[str] = []
    files = candidate_files()

    for path in files:
        lowered = path.lower()
        suffix = Path(lowered).suffix
        if suffix in RESTRICTED_SUFFIXES and path not in ALLOWED_RESTRICTED_FILES:
            errors.append(f"restricted content or generated binary: {path}")
        if any(path == prefix.rstrip("/") or path.startswith(prefix) for prefix in GENERATED_PREFIXES):
            errors.append(f"generated directory is tracked: {path}")

    try:
        patterns = load_license_patterns()
    except (OSError, ValueError) as exc:
        errors.append(str(exc))
        patterns = []

    for path in files:
        if Path(path).suffix.lower() in SOURCE_SUFFIXES and not covered(path, patterns):
            errors.append(f"source has no license-list row: {path}")

    check_c64_rom_provenance(errors)

    font = ROOT / FONT_PATH
    if not font.is_file():
        errors.append(f"pinned overlay font is absent: {FONT_PATH}")
    elif sha256(font) != FONT_SHA256:
        errors.append(f"pinned overlay font changed: {FONT_PATH}")

    try:
        lock = json.loads((ROOT / "provenance" / "sources.lock.json").read_text(encoding="utf-8"))
        if lock.get("schema_version") != 1 or not lock.get("sources"):
            errors.append("source lock has no supported schema or sources")
        for source in lock.get("sources", []):
            commit = source.get("commit", "")
            if re.fullmatch(r"[0-9a-f]{40}", commit) is None:
                errors.append(f"source is not pinned to a full commit: {source.get('name')}")
            if not source.get("url") or not source.get("license"):
                errors.append(f"source lacks URL or license record: {source.get('name')}")
    except (OSError, ValueError) as exc:
        errors.append(f"invalid source lock: {exc}")

    try:
        imports_lock = json.loads(
            (ROOT / "provenance" / "imports.lock.json").read_text(encoding="utf-8")
        )
        if imports_lock.get("schema_version") != 1:
            errors.append("import lock has unsupported schema")
        import_paths: set[str] = set()
        for imported in imports_lock.get("imports", []):
            path = imported.get("path", "")
            if path in import_paths:
                errors.append(f"duplicate import-lock path: {path}")
                continue
            import_paths.add(path)
            target = ROOT / path
            expected = imported.get("sha256", "")
            if not target.is_file():
                errors.append(f"locked import is absent: {path}")
            elif re.fullmatch(r"[0-9a-f]{64}", expected) is None:
                errors.append(f"locked import has invalid digest: {path}")
            elif sha256(target) != expected:
                errors.append(f"locked import changed without provenance update: {path}")
            if not imported.get("source") or not imported.get("source_path"):
                errors.append(f"locked import lacks source identity: {path}")
        tracked_imports = {
            path for path in files
            if path.startswith("third_party/appletini/") or
            path.startswith("third_party/c64/upstream/") or
            path.startswith("third_party/gameboy/upstream/")
        }
        if tracked_imports != import_paths:
            for path in sorted(tracked_imports - import_paths):
                errors.append(f"third-party source is not import-locked: {path}")
            for path in sorted(import_paths - tracked_imports):
                errors.append(f"import lock names an untracked third-party file: {path}")
    except (OSError, ValueError) as exc:
        errors.append(f"invalid import lock: {exc}")

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(f"repository policy passed for {len(files)} files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
