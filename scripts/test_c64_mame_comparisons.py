#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run bounded C64/MAME differential suites and retain failures as evidence.

This does not run a complete MAME machine or qualify a physical FPGA image.
Every selected child must finish, return zero and publish a passing receipt.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
from typing import TextIO

from c64_mame_oracle import COMMIT, REFERENCE, ROOT


SUITES = {
    "cpu-baseline": ("test_c64_acceleration.py", ["--test", "cpu"],
                     "CPU accepted bus trace at native and interrupted fast cadences"),
    "cpu-boundaries": ("test_c64_mame_cpu_boundaries.py", [],
                       "Interrupt and RDY boundary bus traces"),
    "port": ("test_c64_mame_port.py", [], "6510 pins and floating-charge expiry"),
    "memory": ("test_c64_mame_memory.py", [],
               "Memory-handler routing and effects conditional on supplied PLA outputs"),
    "vic": ("test_c64_mame_vic.py", [], "VIC native bus ownership"),
}


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def source_snapshot() -> dict[str, str]:
    """Capture immutable references plus test and C64 implementation sources."""
    paths = set()
    for folder in ("hdl/core/c64", "third_party/c64/upstream",
                   "third_party/mame_reference", "tests/c64_mame"):
        paths.update(path for path in (ROOT / folder).rglob("*")
                     if path.is_file() and "__pycache__" not in path.parts)
    for pattern in ("test_c64*.py", "c64_mame*.py"):
        paths.update((ROOT / "scripts").glob(pattern))
    paths.update((ROOT / "hdl/sim").glob("tb_c64*.sv"))
    paths.update(ROOT / path for path in (
        "scripts/run_native_tool.py", "scripts/port_c64_vic.py",
        "hdl/hdl_sources.txt", "provenance/imports.lock.json"))
    return {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(paths)}


def verify_reference() -> None:
    manifest = json.loads((REFERENCE / "import_manifest.json").read_text(encoding="utf-8"))
    if manifest.get("commit") != COMMIT or manifest.get("repository") != "https://github.com/mamedev/mame":
        raise ValueError("MAME manifest differs from the pinned reference")
    seen = set()
    for entry in manifest["files"]:
        relative = Path(entry["path"])
        path = (REFERENCE / "upstream" / relative).resolve()
        if (relative.is_absolute() or not path.is_relative_to((REFERENCE / "upstream").resolve())
                or entry["path"] in seen or digest(path) != entry["sha256"]):
            raise ValueError("Invalid or changed MAME reference: " + entry["path"])
        seen.add(entry["path"])
    if not seen:
        raise ValueError("Empty MAME reference manifest")


def run_child(command: list[str], environment: dict[str, str], stream: TextIO,
              timeout: int) -> int:
    """Wait for a suite; stop native descendants before recording a timeout."""
    options = ({"creationflags": subprocess.CREATE_NO_WINDOW | subprocess.CREATE_NEW_PROCESS_GROUP}
               if os.name == "nt" else {"start_new_session": True})
    with subprocess.Popen(command, cwd=ROOT, env=environment, stdout=stream,
                          stderr=subprocess.STDOUT, **options) as process:
        try:
            return process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            if os.name == "nt":
                cleanup = subprocess.run(["taskkill.exe", "/PID", str(process.pid), "/T", "/F"],
                    stdout=stream, stderr=subprocess.STDOUT, timeout=30,
                    creationflags=subprocess.CREATE_NO_WINDOW)
                if cleanup.returncode:
                    # Do not publish evidence or start another suite when a native
                    # descendant might still be writing files in this directory.
                    process.kill()
                    raise RuntimeError("Timed out; process-tree cleanup failed. No aggregate receipt published.")
            else:
                os.killpg(process.pid, signal.SIGKILL)
            process.wait(timeout=30)
            raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", action="append", choices=tuple(SUITES),
                        help="Select suites; default runs every comparison")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/c64-mame-comparisons")
    parser.add_argument("--vivado-bin", type=Path)
    parser.add_argument("--timeout", type=int, default=1800, help="Seconds allowed per suite")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if build == ROOT / "build" or not build.is_relative_to((ROOT / "build").resolve()):
        parser.error("--build-dir must be a dedicated directory under this checkout's build/")
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    selected = args.test or list(SUITES)
    if len(selected) != len(set(selected)):
        parser.error("Each suite may be selected only once")
    build.mkdir(parents=True, exist_ok=True)
    receipt = build / "validation.json"
    receipt.unlink(missing_ok=True)
    started = datetime.now(timezone.utc).isoformat()
    verify_reference()
    before = source_snapshot()
    results = []
    # Normalize Path/PATH once for Windows tools and their child compilers.
    environment = {key: value for key, value in os.environ.items() if key.upper() != "PATH"}
    environment["PATH"] = os.environ.get("PATH", os.environ.get("Path", ""))
    for name in selected:
        script, extra, scope = SUITES[name]
        directory = build / name
        directory.mkdir(exist_ok=True)
        child_receipt = directory / "validation.json"
        child_receipt.unlink(missing_ok=True)
        command = [sys.executable, str(ROOT / "scripts" / script),
                   "--build-dir", str(directory), *extra]
        if args.vivado_bin:
            command += ["--vivado-bin", str(args.vivado_bin.resolve())]
        log = build / (name + ".log")
        record = {"name": name, "scope": scope, "command": command,
                  "status": "FAIL", "log": log.relative_to(build).as_posix()}
        print("RUN: " + name, flush=True)
        try:
            with log.open("w", encoding="utf-8") as stream:
                returncode = run_child(command, environment, stream, args.timeout)
            record["returncode"] = returncode
            if child_receipt.is_file():
                child = json.loads(child_receipt.read_text(encoding="utf-8"))
                record.update(receipt=child_receipt.relative_to(build).as_posix(),
                              receipt_sha256=digest(child_receipt), child_status=child.get("status"))
                if name == "cpu-baseline":
                    # This older runner records its MAME pin in the nested oracle
                    # receipt, rather than at the top level.
                    oracle_receipt = directory / "cpu/mame-oracle/receipt.json"
                    oracle = json.loads(oracle_receipt.read_text(encoding="utf-8"))
                    reference_commit = oracle.get("reference_commit")
                    record.update(oracle_receipt=oracle_receipt.relative_to(build).as_posix(),
                                  oracle_receipt_sha256=digest(oracle_receipt))
                else:
                    reference_commit = child.get("reference_commit")
                if reference_commit != COMMIT:
                    raise ValueError("Child used a different MAME reference")
                hashes = child.get("source_sha256")
                if not isinstance(hashes, dict) or not hashes:
                    raise ValueError("Child receipt has no source fingerprints")
                for path, expected in hashes.items():
                    resolved = (ROOT / path).resolve()
                    if not resolved.is_relative_to(ROOT) or not resolved.is_file() or digest(resolved) != expected:
                        raise ValueError("Child source fingerprint differs: " + path)
                    if path in before and before[path] != expected:
                        raise ValueError("Child used sources changed since suite start: " + path)
                if returncode == 0 and child.get("status") == "PASS":
                    record["status"] = "PASS"
                else:
                    record["error"] = "Child comparison failed; see its receipt and log"
            else:
                record["error"] = "Child did not publish a validation receipt"
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            record["error"] = str(error)
        if log.is_file():
            record["log_sha256"] = digest(log)
        results.append(record)
        print(record["status"] + ": " + name, flush=True)
    after = source_snapshot()
    changed = sorted(path for path in before.keys() | after.keys() if before.get(path) != after.get(path))
    passed = not changed and all(record["status"] == "PASS" for record in results)
    report = {"schema": 1, "status": "PASS" if passed else "FAIL",
        "started_utc": started, "finished_utc": datetime.now(timezone.utc).isoformat(),
        "reference_commit": COMMIT, "complete_suite": set(selected) == set(SUITES),
        "checks": results, "source_sha256": before, "changed_sources": changed,
        "limits": ["Bounded production-RTL comparisons, not a complete MAME C64 execution.",
                   "Memory-handler comparisons do not independently validate the PLA truth table.",
                   "No FPGA rebuild, packaging or physical-board validation is performed."]}
    receipt.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"{report['status']}: {sum(item['status'] == 'PASS' for item in results)}/{len(results)} suites; {receipt}")
    return 0 if passed else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        raise SystemExit(1)
