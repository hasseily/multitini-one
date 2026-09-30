#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify the exact licensed Genesis machine subset and its provenance."""

from __future__ import annotations

import argparse
import csv
import fnmatch
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "third_party/genesis/import_manifest.json"
SOURCE_NAME = "MiSTer Mega Drive (Nuked-MD)"


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_manifest(manifest: dict) -> dict[str, str]:
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported Genesis import manifest")
    source = manifest["source"]
    if not source.get("url") or re.fullmatch(r"[0-9a-f]{40}", source["commit"]) is None:
        raise ValueError("Genesis source must have a URL and exact commit")
    if manifest.get("destination") != "third_party/genesis/upstream":
        raise ValueError("Genesis import destination is not the approved subtree")
    if manifest.get("unresolved"):
        raise ValueError("Genesis import contains unresolved license grants")
    paths = {}
    for group in manifest["resolved_groups"]:
        if not group.get("license") or not group.get("basis"):
            raise ValueError("Genesis license group has no grant or basis")
        for path in group["paths"]:
            relative = PurePosixPath(path)
            if (relative.is_absolute() or ".." in relative.parts or "\\" in path or
                    str(relative) != path or path in paths):
                raise ValueError(f"unsafe or duplicate Genesis import: {path}")
            if any(fnmatch.fnmatchcase(path, pattern)
                   for pattern in manifest["excluded_globs"]):
                raise ValueError(f"excluded Genesis import: {path}")
            paths[path] = group["license"]
    if not paths:
        raise ValueError("empty Genesis import")
    return paths


def audit(manifest: dict, source: Path | None = None) -> int:
    expected = validate_manifest(manifest)
    destination = ROOT / manifest["destination"]
    actual = {path.relative_to(destination).as_posix()
              for path in destination.rglob("*") if path.is_file()}
    if actual != set(expected):
        raise ValueError(f"Genesis import file census changed: {sorted(actual ^ set(expected))}")
    imports = json.loads((ROOT / "provenance/imports.lock.json").read_text())["imports"]
    imports = {entry["source_path"]: entry for entry in imports
               if entry.get("source") == SOURCE_NAME}
    if set(imports) != set(expected):
        raise ValueError("Genesis exact import lock does not match its manifest")
    with (ROOT / "licenses/files.csv").open(newline="") as stream:
        rows = {row["path"]: row for row in csv.DictReader(stream)}
    for relative, license_name in expected.items():
        path = destination / relative
        repository_path = path.relative_to(ROOT).as_posix()
        entry = imports[relative]
        if entry.get("path") != repository_path or entry.get("sha256") != digest(path):
            raise ValueError(f"Genesis exact import hash changed: {relative}")
        row = rows.get(repository_path, {})
        if (row.get("commit") != manifest["source"]["commit"] or
                row.get("license") != license_name):
            raise ValueError(f"Genesis per-file license is missing or inconsistent: {relative}")
    if source is not None:
        head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"],
                                       text=True).strip()
        if head != manifest["source"]["commit"]:
            raise ValueError("Genesis checkout does not match the pinned commit")
        for relative in expected:
            original = subprocess.check_output(["git", "-C", str(source), "show", f"{head}:{relative}"])
            if hashlib.sha256(original).hexdigest() != digest(destination / relative):
                raise ValueError(f"Genesis file differs from its pinned Git blob: {relative}")
    return len(expected)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, help="compare all bytes to this pinned upstream checkout")
    args = parser.parse_args()
    count = audit(json.loads(MANIFEST_PATH.read_text(encoding="utf-8")), args.source)
    print(f"Genesis import: {count} pinned files; exact hashes and per-file licenses verified")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
