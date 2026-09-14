#!/usr/bin/env python3
"""Copy only the hash-pinned, accepted release inputs into the current SD tree."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile
import zipfile


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "provenance/current_release.json"
CURRENT = ROOT / "build/current"


def below(root: Path, relative: str) -> Path:
    path = (root / relative).resolve()
    if not path.is_relative_to(root.resolve()) or path == root.resolve():
        raise ValueError(f"path escapes {root}: {relative}")
    return path


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify(path: Path, entry: dict) -> None:
    if path.stat().st_size != entry["size"] or digest(path) != entry["sha256"]:
        raise ValueError(f"release input differs from its pinned bytes: {path}")


def copy_atomic(source: Path, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        shutil.copyfile(source, temporary)
        os.replace(temporary, destination)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify inputs, current tree and aliases without writing")
    args = parser.parse_args()
    record = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if record["schema_version"] != 1:
        raise ValueError("unsupported current release manifest")
    entries = record["files"]
    destinations = [entry["destination"] for entry in entries]
    if len(destinations) != len(set(destinations)):
        raise ValueError("duplicate release destination")

    # Check every source before overwriting any current file. There is no search
    # for a newer image or fallback to another build directory.
    copies = []
    for entry in entries:
        source = below(ROOT, entry["source"])
        verify(source, entry)
        copies.append((source, below(CURRENT, entry["destination"])))
    for entry in record["notices"]:
        source = below(ROOT, entry["source"])
        if not source.is_file():
            raise ValueError(f"missing release notice: {source}")
        copies.append((source, below(CURRENT, entry["destination"])))
    copies.append((MANIFEST, CURRENT / "CURRENT_RELEASE.json"))
    if len({destination for _, destination in copies}) != len(copies):
        raise ValueError("duplicate release file or notice destination")
    expected = {destination for _, destination in copies}
    expected.add(CURRENT / "SHA256SUMS.txt")
    if CURRENT.exists():
        extras = {path.resolve() for path in CURRENT.rglob("*") if path.is_file()} - expected
        if extras:
            raise ValueError(f"current package contains unexpected files; preserve them before promotion: {sorted(extras)}")

    # These compatibility locations refer to the same accepted runtime files.
    aliases = []
    for entry in entries:
        if entry["destination"] == "FIRMWARE.BIN":
            aliases.extend([(ROOT / entry["source"], ROOT / "FIRMWARE.BIN"),
                            (ROOT / entry["source"], ROOT / "build/firmware-current/FIRMWARE.BIN")])
        elif entry["destination"].startswith("multitini/cores/"):
            relative = entry["destination"].removeprefix("multitini/cores/")
            aliases.append((ROOT / entry["source"], below(ROOT / "build/core-packages", relative)))

    if not args.check:
        for source, destination in copies + aliases:
            copy_atomic(source, destination)
    for source, destination in copies + aliases:
        if digest(source) != digest(destination):
            raise ValueError(f"promoted copy differs: {destination}")
    sums = "".join(f"{digest(path)}  {path.relative_to(CURRENT).as_posix()}\n"
                   for path in sorted(expected - {CURRENT / "SHA256SUMS.txt"}))
    sum_path = CURRENT / "SHA256SUMS.txt"
    if args.check:
        if sum_path.read_text(encoding="utf-8") != sums:
            raise ValueError("current package checksum list differs")
    else:
        sum_path.write_text(sums, encoding="utf-8", newline="\n")
        archive = ROOT / "build" / f"multitini-current-{record['release_id']}.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as output:
            for path in sorted(expected):
                output.write(path, path.relative_to(CURRENT).as_posix())
        print(f"Package: {archive}")
        print(f"SHA256: {digest(archive)}")
    print(f"Verified {len(entries)} accepted runtime files, notices and current aliases.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
