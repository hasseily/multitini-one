#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check production IIgs MMU effects against an independent KEGS-rule oracle."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/apple-iigs-mmu-decode-tests")
    parser.add_argument("--mmu-source", type=Path, help="Explicit derivative fixture for negative controls")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    env = {key: value for key, value in os.environ.items() if key.upper() != "PATH"}
    env["PATH"] = str(runtime) + os.pathsep + os.environ.get("PATH", os.environ.get("Path", ""))
    if os.name == "nt":
        env["VERILATOR_ROOT"] = (runtime.parent / "share/verilator").as_posix()
    bench = ROOT / "hdl/sim/tb_apple_iigs_mmu_decode.sv"
    mmu = (args.mmu_source or ROOT / "hdl/core/apple_iigs/ports/mmu.sv").resolve()
    captured = [bench, mmu, Path(__file__).resolve(), ROOT / "scripts/port_apple_iigs_core.py"]

    def hashes():
        return {path.relative_to(ROOT).as_posix() if path.is_relative_to(ROOT) else str(path):
                hashlib.sha256(path.read_bytes()).hexdigest() for path in captured}

    def tool(name):
        return runtime / (name + ".exe") if os.name == "nt" else shutil.which(name)

    def run(command, name):
        result = subprocess.run([str(item).replace("\\", "/") for item in command], cwd=build,
                                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (build / (name + ".log")).write_text(result.stdout)
        if result.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed", result.stdout):
            raise RuntimeError(name + " failed:\n" + result.stdout[-14000:])
        return result.stdout

    before = hashes()
    top = bench.stem
    obj = build / "obj"
    run([tool("verilator_bin") if os.name == "nt" else tool("verilator"), "--cc", "--exe", "--main",
         "--timing", "-Wno-fatal", "--top-module", top, "--Mdir", obj, mmu, bench], "generate")
    executable = obj / ("V" + top + (".exe" if os.name == "nt" else ""))
    if os.name == "nt":
        include = runtime.parent / "share/verilator/include"
        run([tool("g++"), "-std=c++20", "-O1", "-pthread", "-DVL_TIME_CONTEXT", "-I" + str(include),
             "-I" + str(include / "vltstd"), "-I" + str(obj), *sorted(obj.glob("*.cpp")),
             include / "verilated.cpp", include / "verilated_timing.cpp", include / "verilated_threads.cpp",
             "-o", executable], "compile")
    else:
        run([tool("make"), "-C", obj, "-f", "V" + top + ".mk", "-j8"], "compile")
    output = run([executable], "simulate")
    match = re.search(r"PASS IIgs MMU decode: (\d+) checks; shadow=(\d+) aux=(\d+) LC=(\d+) IO/slots=(\d+) ROM=(\d+) scenes=(\d+)", output)
    if not match:
        raise RuntimeError("Missing full MMU decode PASS marker")
    if hashes() != before:
        raise RuntimeError("Inputs changed during MMU verification")
    counts = dict(zip(("total", "shadow", "aux", "language_card", "io_slots", "rom", "background_scenes"), map(int, match.groups())))
    (build / "validation.json").write_text(json.dumps({
        "passed": True, "source_sha256": before, "checks": counts,
        "reference": "KEGS 1.38 moremem.c; independent architectural address/effect oracle",
        "limits": ["Combinatorial MMU only: no real CPU execution, FPGA timing or physical-board acceptance.",
                   "NEWVIDEO latch-off retains the existing port behavior; KEGS does not qualify that mode.",
                   "No GSSquared-only linearization, extended aux/IOLC aliases or unpopulated-bank shadowing is asserted."]
    }, indent=2) + "\n")
    print(output.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
