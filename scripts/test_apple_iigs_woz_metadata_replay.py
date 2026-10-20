#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Trace stale block-zero replay into actual WOZ metadata and track requests."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if not build.is_relative_to(ROOT / "build") or build.exists():
        raise ValueError("Select a fresh repository build directory")
    build.mkdir(parents=True)
    sources = [ROOT / "hdl/core/apple_iigs/ports/bram.sv",
               ROOT / "hdl/core/apple_iigs/ports/woz_floppy_controller.sv",
               ROOT / "hdl/sim/tb_apple_iigs_woz_metadata_replay.sv"]
    inputs = sources + [Path(__file__).resolve()]
    hashes = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
    for p in inputs:
        target = build / "captured" / p.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(p.read_bytes())
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    env = {k: v for k, v in os.environ.items() if k.upper() != "PATH"}
    env["PATH"] = str(runtime) + os.pathsep + str(runtime.parents[1] / "usr/bin") + os.pathsep + os.environ.get("PATH", "")
    env["VERILATOR_ROOT"] = (runtime.parent / "share/verilator").as_posix()
    commands = {}

    def run(command, name):
        commands[name] = [str(p).replace("\\", "/") for p in command]
        with (build / (name + ".log")).open("w") as stream:
            result = subprocess.run(commands[name], cwd=build, env=env, stdout=stream, stderr=subprocess.STDOUT)
        text = (build / (name + ".log")).read_text(errors="replace")
        if result.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed", text):
            raise ValueError(name + " failed: " + text[-8000:])
        return text

    obj = build / "obj"
    top = "tb_apple_iigs_woz_metadata_replay"
    run([runtime / "verilator_bin.exe", "--cc", "--exe", "--main", "--timing", "-Wno-fatal",
         "--top-module", top, "--Mdir", obj, *sources], "generate")
    include = runtime.parent / "share/verilator/include"
    executable = obj / ("V" + top + ".exe")
    run([runtime / "g++.exe", "-std=c++20", "-O1", "-pthread", "-DVL_TIME_CONTEXT",
         "-I" + str(include), "-I" + str(include / "vltstd"), "-I" + str(obj),
         *sorted(obj.glob("*.cpp")), include / "verilated.cpp", include / "verilated_timing.cpp",
         include / "verilated_threads.cpp", "-o", executable], "compile")
    cases = {}
    for mode in range(3):
        for replay in range(2):
            name = f"mode{mode}-replay{replay}"
            text = run([executable, f"+MODE={mode}", f"+REPLAY={replay}"], name)
            if text.count("PASS:") != 1:
                raise ValueError(name + " did not pass")
            cases[name] = next(line for line in text.splitlines() if line.startswith("PASS:"))
    if hashes != {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}:
        raise ValueError("Metadata test sources changed during execution")
    record = {"status": "PASS", "files": hashes, "commands": commands, "cases": cases,
              "limits": ["Original WOZ2 fixture; no Apple ROM or user media.",
                         "Controller-only sparse/continuous/transitioning backend CE and explicit stale-sector injection.",
                         "The injected cases establish the corruption mechanism; transport prevention is covered separately."]}
    (build / "validation.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(cases, indent=2))


if __name__ == "__main__":
    main()
