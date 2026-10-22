#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise production IIgs MMU soft-switch retirement and state transitions."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
from test_apple_iigs_ports import machine_sources

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/apple-iigs-mmu-state-tests")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    # Windows environment keys are case-insensitive; Python may expose both
    # PATH and Path, which makes the child process's tool lookup ambiguous.
    env = {key: value for key, value in os.environ.items() if key.upper() != "PATH"}
    env["PATH"] = str(runtime) + os.pathsep + os.environ.get("PATH", os.environ.get("Path", ""))
    if os.name == "nt":
        env["VERILATOR_ROOT"] = (runtime.parent / "share/verilator").as_posix()

    def tool(name):
        return runtime / (name + ".exe") if os.name == "nt" else shutil.which(name)

    def run(command, name):
        log = build / (name + ".log")
        with log.open("w") as stream:
            result = subprocess.run([str(item).replace("\\", "/") for item in command], cwd=build,
                                    env=env, stdout=stream, stderr=subprocess.STDOUT)
        output = log.read_text(errors="replace")
        if result.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed", output):
            raise ValueError(name + " failed:\n" + output[-14000:])
        return output

    bench = ROOT / "hdl/sim/tb_apple_iigs_mmu_state.sv"
    sources = machine_sources() + [bench]
    captured = sources + [ROOT / "third_party/apple-iigs/upstream/rtl/dhr_lut.vh",
                          ROOT / "scripts/port_apple_iigs_core.py", ROOT / "scripts/test_apple_iigs_ports.py",
                          Path(__file__).resolve()]
    def hashes():
        return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in captured}
    before = hashes()
    (build / "inputs.json").write_text(json.dumps({"schema_version": 1, "files": before}, indent=2) + "\n")
    top = bench.stem
    directory = build / "obj"
    run([tool("verilator_bin") if os.name == "nt" else tool("verilator"), "--cc", "--exe", "--main",
         "--timing", "-Wno-fatal", "--top-module", top, "-I" + str(ROOT / "third_party/apple-iigs/upstream/rtl"),
         "--Mdir", directory, *sources], "generate")
    executable = directory / ("V" + top + (".exe" if os.name == "nt" else ""))
    if os.name == "nt":
        include = runtime.parent / "share/verilator/include"
        run([tool("g++"), "-std=c++20", "-O1", "-pthread", "-DVL_TIME_CONTEXT", "-I" + str(include),
             "-I" + str(include / "vltstd"), "-I" + str(directory), *sorted(directory.glob("*.cpp")),
             include / "verilated.cpp", include / "verilated_timing.cpp", include / "verilated_threads.cpp",
             "-o", executable], "compile")
    else:
        run([tool("make"), "-C", directory, "-f", "V" + top + ".mk", "-j8"], "compile")
    output = run([executable], "simulate")
    match = re.search(r"PASS: IIgs MMU state checks=(\d+) transactions=(\d+)", output)
    if not match:
        raise ValueError("Missing MMU state completion marker")
    if hashes() != before:
        raise ValueError("MMU sources changed during validation")
    (build / "validation.json").write_text(json.dumps({
        "schema_version": 1, "files": before, "checks": int(match[1]), "transactions": int(match[2]),
        "coverage": ["four bank aliases, C000-C00F write-only behavior", "C029 all values, reserved bits and warm-reset preservation", "C035 all values and cross-alias readback", "C068 all values and LC latch preservation",
                     "KEGS C00A/C00B/C017/C02D shared slot3 state", "every LC read/write access pair and cross-alias sequence",
                     "PAGE2/HIRES read and write switches through four aliases", "held uncommitted I/O cycles", "IOLC guard",
                     "C036 reserved bits, ROM versions, dirty video and LC reset"],
        "references": ["KEGS 1.38 moremem.c io_read/io_write (LC and soft switches), sim65816.c do_reset, video.c video_reset"],
        "limits": ["CPU bus is forced; production decode and I/O state machinery are used.",
                   "KEGS state semantics take precedence; its diagnostics on reserved-bit writes are not reproduced.",
                   "No system ROM, full CPU program, synthesis, timing or board acceptance is implied."]}, indent=2) + "\n")
    print(output.strip())
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
