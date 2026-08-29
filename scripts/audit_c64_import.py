#!/usr/bin/env python3
"""Audit or stage the exact, rights-cleared subset of MiSTer C64."""

from __future__ import annotations

import argparse
import fnmatch
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "third_party" / "c64" / "import_manifest.json"


def load_manifest() -> dict:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    if manifest.get("schema_version") != 1:
        raise ValueError("unsupported C64 import-manifest schema")
    resolved = [path for group in manifest["resolved_groups"]
                for path in group["paths"]]
    unresolved = [entry["path"] for entry in manifest["unresolved"]]
    all_paths = resolved + unresolved
    if len(all_paths) != len(set(all_paths)):
        raise ValueError("a C64 path appears more than once")
    for path in all_paths:
        if Path(path).is_absolute() or ".." in Path(path).parts:
            raise ValueError(f"unsafe C64 path: {path}")
        if any(fnmatch.fnmatchcase(path, pattern)
               for pattern in manifest["excluded_globs"]):
            raise ValueError(f"planned path matches exclusion rule: {path}")
    for group in manifest["resolved_groups"]:
        if not group.get("license") or not group.get("basis"):
            raise ValueError("resolved group lacks license or basis")
    return manifest


def git_output(source: Path, *arguments: str) -> str:
    return subprocess.run(["git", "-C", str(source), *arguments], check=True,
                          text=True, stdout=subprocess.PIPE).stdout.strip()


def verify_source(source: Path, manifest: dict) -> None:
    if git_output(source, "rev-parse", "HEAD") != manifest["source"]["commit"]:
        raise ValueError("C64 checkout does not match the pinned commit")
    if git_output(source, "status", "--porcelain"):
        raise ValueError("C64 checkout is dirty")
    planned = [path for group in manifest["resolved_groups"]
               for path in group["paths"]]
    planned += [entry["path"] for entry in manifest["unresolved"]]
    missing = [path for path in planned if not (source / path).is_file()]
    if missing:
        raise FileNotFoundError(f"planned C64 files are absent: {missing}")


def digest(path: Path) -> str:
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(65536), b""):
            result.update(block)
    return result.hexdigest()


def stage(source: Path, manifest: dict) -> None:
    unresolved = manifest["unresolved"]
    if unresolved:
        raise RuntimeError(
            f"rights gate is closed: {len(unresolved)} planned files unresolved")
    destination = ROOT / manifest["destination"]
    for group in manifest["resolved_groups"]:
        for relative in group["paths"]:
            source_file = source / relative
            destination_file = destination / relative
            if destination_file.exists():
                if digest(source_file) != digest(destination_file):
                    raise FileExistsError(
                        f"refusing to overwrite changed file: {destination_file}")
                continue
            destination_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source_file, destination_file)
            print(f"staged {relative}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
                        help="path to the pinned C64_MiSTer checkout")
    parser.add_argument("--stage", action="store_true",
                        help="copy only after every planned file is resolved")
    args = parser.parse_args()
    manifest = load_manifest()
    if args.stage and args.source is None:
        parser.error("--stage requires --source")
    if args.source is not None:
        verify_source(args.source.resolve(), manifest)
    if args.stage:
        stage(args.source.resolve(), manifest)

    resolved_count = sum(len(group["paths"])
                         for group in manifest["resolved_groups"])
    unresolved_count = len(manifest["unresolved"])
    print(f"C64 import manifest: {resolved_count} resolved, "
          f"{unresolved_count} unresolved")
    if unresolved_count:
        for entry in manifest["unresolved"]:
            print(f"BLOCKED {entry['path']}: {entry['reason']}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError,
            subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
