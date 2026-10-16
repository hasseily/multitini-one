#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute production USB SD disconnect lifecycle against transport/SD faults."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCE = "firmware/platform/xilinx/usb_storage_service.c"
FIXTURE = "tests/host/test_usb_storage_disconnect.c"
NAMES = ("UsbStorageQueueEmpty", "UsbResetStorageQueue", "UsbResetLocalStorageState",
         "usb_storage_service_connect", "usb_storage_service_disconnect",
         "usb_storage_service_transfer_interrupted")


def function(source, name):
    match = re.search(r"^(?:static\s+)?[A-Za-z_][\w *]*?\b" + name +
                      r"\([^;]*?\)\s*\{", source, re.M)
    if match is None:
        raise ValueError("Missing production function: " + name)
    depth, end = 1, match.end()
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def descriptor(path):
    data = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="C:/msys64/ucrt64/bin/gcc.exe" if os.name == "nt" else "cc")
    parser.add_argument("--output", type=Path, default=ROOT / "build/usb-storage-disconnect")
    parser.add_argument("--negative-before", type=Path,
                        help="Exact captured old service source; old lifecycle must fail the same cases")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    inputs = [ROOT / SOURCE, ROOT / FIXTURE, Path(__file__),
              ROOT / "firmware/platform/xilinx/usb_storage_service.h",
              ROOT / "tests/xilinx_stubs/xil_types.h"]
    before = {str(path.relative_to(ROOT)): descriptor(path) for path in inputs}
    source = (ROOT / SOURCE).read_text()
    selected = {name: function(source, name) for name in NAMES}
    declarations = []
    for name in ("UsbStopRequested", "UsbTransferInterrupted", "UsbStopRequestTick"):
        match = re.search(r"^static\s+[^;{}\n]*\b" + name + r"[^;{}\n]*;", source, re.M)
        if match is None:
            raise ValueError("Missing production lifecycle state: " + name)
        declarations.append(match[0])
    timeout = re.search(r"^#define USB_STORAGE_STOP_TIMEOUT_TICKS[^\n]*", source, re.M)
    if timeout is None:
        raise ValueError("Missing production stop timeout constant")
    prefix = timeout[0] + "\n" + "\n".join(declarations)
    env = os.environ.copy()
    if os.name == "nt":
        env["PATH"] = "C:/msys64/ucrt64/bin" + os.pathsep + env.get("PATH", "")
    log, commands = [], []

    def run_case(directory, functions, cases=()):
        directory.mkdir(exist_ok=True)
        (directory / "usb_storage_disconnect.inc").write_text(prefix + "\n\n" + "\n\n".join(functions[name] for name in NAMES))
        binary = directory / ("test.exe" if os.name == "nt" else "test")
        command = [args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-I" + str(directory), "-I" + str(ROOT / "tests/xilinx_stubs"),
                   "-I" + str(ROOT / "firmware/platform/xilinx"), str(ROOT / FIXTURE),
                   "-o", str(binary)]
        commands.append(command)
        compiled = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        log.append("$ " + " ".join(command) + "\n" + compiled.stdout + compiled.stderr)
        if compiled.returncode:
            raise ValueError("Disconnect fixture compilation failed: " + compiled.stderr)
        results = []
        for case in cases or (None,):
            command = [str(binary)] + ([case] if case else [])
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, timeout=15)
            log.append("$ " + " ".join(command) + "\n" + result.stdout + result.stderr)
            results.append(result)
        return results

    try:
        positive = run_case(out, selected)[0]
        if positive.returncode:
            raise ValueError("Production disconnect regression failed: " + positive.stdout + positive.stderr)
        negative = None
        if args.negative_before:
            old = args.negative_before.read_text()
            old_functions = dict(selected)
            for name in ("usb_storage_service_connect", "usb_storage_service_disconnect"):
                old_functions[name] = function(old, name)
            cases = ("grace_deadline", "stale_queue", "full_write_failure", "sticky_error_recorded",
                     "connect_resets_deadline")
            failures = run_case(out / "negative-before", old_functions, cases)
            if any(failed.returncode != 42 for failed in failures):
                raise ValueError("Captured old lifecycle did not fail every intended regression")
            negative = {"status": "EXPECTED_FAILURE", "source": descriptor(args.negative_before),
                        "overridden_functions": ["usb_storage_service_connect", "usb_storage_service_disconnect"],
                        "cases": {case: {"exit_code": failed.returncode, "failure": failed.stderr.strip()}
                                  for case, failed in zip(cases, failures)}}
        after = {str(path.relative_to(ROOT)): descriptor(path) for path in inputs}
        if before != after:
            raise ValueError("Disconnect test inputs changed while executing")
        record = {"status": "PASS", "files": before, "commands": commands,
                  "negative_before": negative, "result": positive.stdout.strip(),
                  "scope": "Production connect/disconnect/deadline/queue functions; class, DMA and SD failure injection. No hardware operation."}
        (out / "validation.json").write_text(json.dumps(record, indent=2) + "\n")
        print(positive.stdout.strip())
        if negative:
            print("PASS exact captured old lifecycle fails the same regression")
    finally:
        (out / "run.log").write_text("\n".join(log))


if __name__ == "__main__":
    main()
