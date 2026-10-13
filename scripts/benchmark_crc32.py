#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Validate and time the shared CRC on a host; this does not measure the FPGA card."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
LIB = ROOT / "firmware/golden/lib"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler")
    parser.add_argument("--appletini-repo", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "build/crc32-benchmark")
    args = parser.parse_args()
    compiler = args.compiler or shutil.which("gcc") or shutil.which("clang")
    if compiler is None and Path("C:/msys64/ucrt64/bin/gcc.exe").exists():
        compiler = "C:/msys64/ucrt64/bin/gcc.exe"
    if compiler is None:
        raise RuntimeError("A native C compiler is required")
    if args.appletini_repo is not None:
        for name in ("crc32.c", "crc32.h"):
            if (args.appletini_repo / "ps_sources/lib" / name).read_bytes() != (LIB / name).read_bytes():
                raise AssertionError(f"Appletini {name} differs from the shared implementation")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    executable = output / ("crc32.exe" if os.name == "nt" else "crc32")
    env = dict(os.environ, PATH=str(Path(compiler).parent) + os.pathsep + os.environ.get("PATH", ""))
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(LIB),
               str(ROOT / "tests/host/test_crc32.c"), str(LIB / "crc32.c"), "-o", str(executable)]
    subprocess.run(command, check=True, env=env)
    result = subprocess.run([str(executable), "--benchmark"], check=True, capture_output=True,
                            text=True, env=env)
    print(result.stdout, end="")
    prefix = "HOST_BENCHMARK_JSON: "
    records = [line[len(prefix):] for line in result.stdout.splitlines() if line.startswith(prefix)]
    if len(records) != 1:
        raise AssertionError("Native benchmark did not return one result")
    report = json.loads(records[0])
    data = bytes(((i * 73) ^ (i >> 7) ^ (i >> 17) ^ 0xA5) & 255 for i in range(report["bytes"]))
    expected = [zlib.crc32(data, seed) for seed in range(report["rounds"])]
    if report["crc32"] != expected:
        raise AssertionError("Native reflected CRC does not match Python zlib's independent implementation")
    report.update({
        "compiler": subprocess.check_output([compiler, "--version"], text=True, env=env).splitlines()[0],
        "compile_command": command,
        "host": platform.platform(),
        "scope": "host only; no device timing or hardware access",
        "oracle": "Python zlib.crc32, seeds 0, 1 and 2",
        "source_sha256": {name: hashlib.sha256((LIB / name).read_bytes()).hexdigest()
                          for name in ("crc32.c", "crc32.h")},
    })
    report_path = output / "benchmark.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (output / "native.log").write_text(result.stdout, encoding="utf-8")
    print(f"PASS: all three synthetic 6 MiB CRCs independently match zlib; report {report_path}")


if __name__ == "__main__":
    main()
