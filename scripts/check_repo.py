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
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESTRICTED_SUFFIXES = {
    ".2mg", ".bin", ".bit", ".crt", ".d64", ".d81", ".elf", ".g64",
    ".hex", ".img", ".mem", ".mif", ".po", ".prg", ".rbf", ".reu",
    ".rom", ".tap", ".t64", ".woz",
}
SOURCE_SUFFIXES = {".c", ".h", ".py", ".sv", ".tcl", ".v", ".vhd", ".vhdl", ".xdc"}
GENERATED_PREFIXES = ("build/", "build-", "project/", "vitis_workspace/", ".Xil/")
ALLOWED_RESTRICTED_FILES = {
    # Open-source SameBoy-derived bootstrap, plus its deterministic Vivado/
    # readmemh representation. Both are pinned and covered by the MIT notice.
    "third_party/gameboy/upstream/BootROMs/cgb_boot.mif",
    "hdl/core/gameboy/boot/cgb_boot.hex",
}


def candidate_files() -> list[str]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--others", "--exclude-standard", "-z"],
        cwd=ROOT,
        check=True,
        stdout=subprocess.PIPE,
    )
    return sorted(path for path in result.stdout.decode().split("\0") if path)


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
