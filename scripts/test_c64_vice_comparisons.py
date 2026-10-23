#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the pinned VICE 3.10 component comparisons and retain every result.

This is bounded production-RTL coverage, not a complete x64sc machine or
physical-board qualification. Historical MAME regressions run separately.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import sys

from c64_vice_oracle import COMMIT, ROOT, digest, verify_reference
from test_c64_mame_comparisons import run_child, source_snapshot as historical_snapshot


SUITES = {
    "cpu": ("test_c64_vice_cpu.py", "IRQ/NMI, BRK, CLI/SEI, branches and RDY bus traces"),
    "vic": ("test_c64_vice_vic.py", "Dynamic raster IRQ, graphics fetch and REU sprite BA"),
    "reu": ("test_c64_vice_reu.py", "512 KiB and 16 MiB REC transfers, registers and IRQ"),
    "memory": ("test_c64_vice_memory.py", "DMA writes to CPU-port backing RAM"),
    "bus": ("test_c64_vice_bus.py", "Native REU bus arbitration and completion timing"),
}


def source_snapshot() -> dict[str, str]:
    result = historical_snapshot()
    paths = set()
    for folder in ("third_party/vice_reference", "tests/c64_vice"):
        paths.update(p for p in (ROOT / folder).rglob("*")
                     if p.is_file() and "__pycache__" not in p.parts)
    paths.update((ROOT / "scripts").glob("*c64_vice*.py"))
    result.update({p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(paths)})
    return dict(sorted(result.items()))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", action="append", choices=tuple(SUITES))
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/c64-vice-comparisons")
    parser.add_argument("--timeout", type=int, default=1800, help="Seconds per child suite")
    args = parser.parse_args()
    selected = args.test or list(SUITES)
    build = args.build_dir.resolve()
    if build == ROOT / "build" or not build.is_relative_to(ROOT / "build"):
        parser.error("Use a dedicated directory under this checkout's build/")
    if build.exists():
        parser.error("Preserve prior evidence; choose a new build directory")
    if args.timeout <= 0 or len(selected) != len(set(selected)):
        parser.error("Use a positive timeout and select each suite only once")
    verify_reference()
    before = source_snapshot()
    build.mkdir(parents=True)
    started = datetime.now(timezone.utc).isoformat()
    environment = {key: value for key, value in os.environ.items() if key.upper() != "PATH"}
    environment["PATH"] = os.environ.get("PATH", os.environ.get("Path", ""))
    results = []
    for name in selected:
        script, scope = SUITES[name]
        directory = build / name
        receipt = directory / "validation.json"
        command = [sys.executable, str(ROOT / "scripts" / script), "--build-dir", str(directory)]
        log = build / (name + ".log")
        record = {"name": name, "scope": scope, "status": "FAIL", "command": command,
                  "log": log.relative_to(build).as_posix()}
        print("RUN: " + name, flush=True)
        try:
            with log.open("w", encoding="utf-8") as stream:
                code = run_child(command, environment, stream, args.timeout)
            record["returncode"] = code
            child = json.loads(receipt.read_text(encoding="utf-8"))
            record.update(receipt=receipt.relative_to(build).as_posix(),
                          receipt_sha256=digest(receipt), child_status=child.get("status"))
            if child.get("reference_commit") != COMMIT:
                raise ValueError("Child used a different VICE reference")
            hashes = child.get("source_sha256")
            if not isinstance(hashes, dict) or not hashes:
                raise ValueError("Child has no source fingerprints")
            for path, expected in hashes.items():
                resolved = (ROOT / path).resolve()
                if not resolved.is_relative_to(ROOT) or not resolved.is_file() or digest(resolved) != expected:
                    raise ValueError("Child fingerprint differs: " + path)
                if path not in before or before[path] != expected:
                    raise ValueError("Child source missing or changed since suite start: " + path)
            if code == 0 and child.get("status") == "PASS":
                record["status"] = "PASS"
            else:
                record["error"] = "Child comparison failed; see receipt and log"
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            record["error"] = str(error)
        if log.is_file():
            record["log_sha256"] = digest(log)
        results.append(record)
        print(record["status"] + ": " + name, flush=True)
    after = source_snapshot()
    changed = sorted(p for p in before.keys() | after.keys() if before.get(p) != after.get(p))
    passed = not changed and all(r["status"] == "PASS" for r in results)
    report = {"schema": 1, "status": "PASS" if passed else "FAIL",
              "reference_commit": COMMIT, "reference_release": "VICE 3.10.0 / SVN r45923",
              "started_utc": started, "finished_utc": datetime.now(timezone.utc).isoformat(),
              "complete_suite": set(selected) == set(SUITES), "checks": results,
              "source_sha256": before, "changed_sources": changed,
              "limits": ["Bounded comparisons with unchanged VICE source, not complete-machine equivalence.",
                         "Individual receipts define each scaffold, stimulus and observation boundary.",
                         "No routed FPGA timing, package installation or physical-board proof."]}
    receipt = build / "validation.json"
    receipt.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{report['status']}: {sum(r['status'] == 'PASS' for r in results)}/{len(results)} suites; {receipt}")
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        raise SystemExit(1)
