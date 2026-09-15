#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture or verify exact source bytes for a selected Multitini FPGA build."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def inputs(machine: str) -> list[Path]:
    selected = {"common", machine.lower()}
    paths = {
        ROOT / "hdl/hdl_sources.txt",
        ROOT / "scripts/create_project.tcl",
        ROOT / "scripts/build_and_export_xsa.tcl",
        ROOT / "scripts/run_native_tool.py",
        Path(__file__).resolve(),
    }
    section = "common"
    for line in (ROOT / "hdl/hdl_sources.txt").read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
        elif section in selected:
            paths.add((ROOT / "hdl" / line.split("|")[0].strip()).resolve())
    for path in paths:
        if not path.is_relative_to(ROOT) or not path.is_file():
            raise ValueError(f"Missing or external build input: {path}")
    return sorted(paths)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--machine", choices=("C64", "GBC", "VECTREX", "SUPERVISOR"))
    parser.add_argument("--c64-video-standard", choices=("PAL", "NTSC"), default="PAL")
    parser.add_argument("--gbc-psram", action="store_true")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("The generated manifest must stay in this checkout's build directory")
    if args.verify:
        record = json.loads(output.read_text())
        expected = record["files"]
        current = {str(path.relative_to(ROOT).as_posix()): digest(path)
                   for path in inputs(record["machine"])}
        if current != expected:
            changed = sorted(path for path in set(current) | set(expected)
                             if current.get(path) != expected.get(path))
            raise ValueError(f"FPGA build inputs changed: {', '.join(changed)}")
        print(f"Verified {len(current)} unchanged {record['machine']} build inputs")
        return 0
    if not args.machine:
        parser.error("--machine is required when capturing inputs")
    if args.gbc_psram and args.machine != "GBC":
        parser.error("--gbc-psram requires --machine GBC")
    record = {
        "schema": 1,
        "machine": args.machine,
        "configuration": {
            "c64_video_standard": args.c64_video_standard if args.machine == "C64" else None,
            "gbc_psram": args.gbc_psram,
        },
        "captured_utc": datetime.now(timezone.utc).isoformat(),
        "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "files": {path.relative_to(ROOT).as_posix(): digest(path)
                  for path in inputs(args.machine)},
        "note": "Source bytes, not a clean-commit assertion; retain the matching source archive with the image.",
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    print(f"Captured {len(record['files'])} {args.machine} build inputs in {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
