#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Verify empty keyboard TALK completes and releases a Doom-style ADB poller."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

import port_apple_iigs_core as port

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/apple-iigs-adb-empty-talk-tests")
    parser.add_argument("--upstream-negative-control", action="store_true",
                        help="require the pinned original ADB to fail at the empty completion check")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    env = os.environ.copy()
    if os.name == "nt":
        old_path = next((value for key, value in env.items() if key.lower() == "path"), "")
        env = {key: value for key, value in env.items() if key.lower() != "path"}
        env["PATH"] = str(runtime) + os.pathsep + str(runtime.parents[1] / "usr/bin") + os.pathsep + old_path
        env["VERILATOR_ROOT"] = (runtime.parent / "share/verilator").as_posix()

    def tool(name):
        return runtime / (name + ".exe") if os.name == "nt" else shutil.which(name)

    def run(command, name, expected_failure=False):
        log = build / (name + ".log")
        with log.open("w") as stream:
            result = subprocess.run([str(item).replace("\\", "/") for item in command],
                                    cwd=build, env=env, stdout=stream, stderr=subprocess.STDOUT)
        output = log.read_text(errors="replace")
        if expected_failure:
            if not result.returncode or "C027 data full expected=1 comb=50 reg=50" not in output:
                raise ValueError("Original ADB did not fail at the expected empty-TALK assertion:\n" + output[-8000:])
        elif result.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed", output):
            raise ValueError(name + " failed:\n" + output[-8000:])
        return output

    ports = ROOT / "hdl/core/apple_iigs/ports"
    bench = ROOT / "hdl/sim/tb_apple_iigs_adb_empty_talk.sv"
    sources = [ports / "adb.sv", ports / "runtime_rom.sv", ports / "bram.sv", bench]
    captured = sources + [ROOT / "third_party/apple-iigs/upstream/rtl/adb.v",
                          ROOT / "scripts/port_apple_iigs_core.py", Path(__file__).resolve()]
    hashes = lambda: {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                      for path in captured}
    before = hashes()
    if args.upstream_negative_control:
        # Apply only the original runtime-ROM adaptation. Keep all controller
        # behavior from the pinned source, including the empty-TALK defect.
        original = port.upstream("rtl/adb.v")
        original = port.replace(original, "module adb(\n", "module adb(\n" + port.rom_inputs() + "input load_rom_bank,\n")
        for bank, name in ((0, "adb_rom_inst"), (1, "adb_rom1_inst")):
            rom = "adb_rom3" if bank == 0 else "adb_rom1"
            original = port.replace(original, f'rom #(12, 8, "rtl/roms/{rom}.hex") {name} (\n',
                f"apple_iigs_runtime_rom #(.AW(12)) {name} (\n  .load_clk(load_rom_clk),\n"
                f"  .load_we(load_rom_we & (load_rom_bank==1'b{bank})),\n"
                "  .load_addr(load_rom_addr),\n  .load_data(load_rom_data),\n")
        reference = build / "adb_upstream_reference.sv"
        reference.write_text(original, encoding="utf-8")
        sources[0] = reference

    directory = build / "obj"
    top = bench.stem
    run([tool("verilator_bin") if os.name == "nt" else tool("verilator"), "--cc", "--exe", "--main",
         "--timing", "-Wno-fatal", "--top-module", top, "--Mdir", directory, *sources], "generate")
    executable = directory / ("V" + top + (".exe" if os.name == "nt" else ""))
    if os.name == "nt":
        include = runtime.parent / "share/verilator/include"
        run([tool("g++"), "-std=c++20", "-O1", "-pthread", "-DVL_TIME_CONTEXT", "-I" + str(include),
             "-I" + str(include / "vltstd"), "-I" + str(directory), *sorted(directory.glob("*.cpp")),
             include / "verilated.cpp", include / "verilated_timing.cpp", include / "verilated_threads.cpp",
             "-o", executable], "compile")
    else:
        run([tool("make"), "-C", directory, "-f", "V" + top + ".mk", "-j8"], "compile")
    output = run([executable], "simulate", args.upstream_negative_control)
    if not args.upstream_negative_control and output.count("PASS ") != 5:
        raise ValueError("All five empty-TALK protocol checks must pass")
    if hashes() != before:
        raise ValueError("ADB regression inputs changed during validation; rerun")
    (build / "validation.json").write_text(json.dumps({"schema_version": 1, "result": "PASS",
        "files": before, "upstream_negative_control": args.upstream_negative_control,
        "checks": ["Empty header80 is visible in both C027 paths and enabled IRQ",
                   "Header remains stable for eight read clocks and acknowledges on falling edge",
                   "Absent address and nonempty keyboard response are unchanged",
                   "Doom-style polling accepts a key after four mouse reports and an empty backup TALK",
                   "A key arriving on the empty-TALK clock retains its service request"],
        "limits": ["Directed ADB simulation; no complete game, timing closure, or hardware claim."]}, indent=2) + "\n")
    print("PASS: original upstream ADB fails the empty-TALK data-full assertion" if args.upstream_negative_control else output.strip())
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
