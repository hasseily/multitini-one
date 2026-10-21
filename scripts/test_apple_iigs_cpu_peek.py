#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check independent primary-CPU/native-DMA read decoding and raw byte lanes."""
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
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/core-gs3/cpu-peek-final-tests")
    parser.add_argument("--hdd-rom", type=Path, default=ROOT / "build/apple-iigs-assets/hdd.rom")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    rom = args.hdd_rom.read_bytes()
    if len(rom) != 2304:
        raise ValueError("Licensed assembled SmartPort ROM must contain2304 bytes")
    fixture = build / "hdd.hex"
    fixture.write_text("\n".join(f"{byte:02x}" for byte in rom) + "\n")
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    env = os.environ.copy()
    if os.name == "nt":
        env["PATH"] = str(runtime) + os.pathsep + env.get("PATH", "")
        env["VERILATOR_ROOT"] = (runtime.parent / "share/verilator").as_posix()

    def tool(name):
        return runtime / (name + ".exe") if os.name == "nt" else shutil.which(name)

    def run(command, name):
        log = build / (name + ".log")
        with log.open("w") as stream:
            result = subprocess.run([str(item).replace("\\", "/") for item in command],
                                    cwd=build, env=env, stdout=stream, stderr=subprocess.STDOUT)
        output = log.read_text(errors="replace")
        if result.returncode or re.search(r"%Warning-(?:IMPLICIT|MULTIDRIVEN|UNOPTFLAT|LATCH):|%Fatal|\bFATAL\b|Assertion failed", output):
            raise ValueError(name + " failed:\n" + output[-14000:])
        return output

    core = ROOT / "hdl/core/apple_iigs"
    bench = ROOT / "hdl/sim/tb_apple_iigs_cpu_peek.sv"
    top = bench.stem
    sources = machine_sources() + [core / (name + ".sv") for name in (
        "multitini_apple_iigs_loader", "multitini_apple_iigs_cache", "multitini_apple_iigs_adapter")]
    sources += [ROOT / "hdl/sim/apple_iigs_bufgce.sv", bench]
    captured = sources + [ROOT / "third_party/apple-iigs/upstream/rtl/dhr_lut.vh",
        ROOT / "scripts/test_apple_iigs_ports.py", ROOT / "scripts/port_apple_iigs_core.py",
        ROOT / "scripts/capture_vectrex_build_inputs.py", Path(__file__).resolve()]

    def hashes():
        return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in captured}

    before = hashes()
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
    expected = {
        "CPU-only MMU bank/config comparisons": 589824,
        "independent CPU/DMA IO class comparisons": 24576,
        "CPU/native independent Zip selector comparisons": 16384,
        "CPU/native fallback lanes cached+bypass valid+invalid comparisons": 256,
    }
    measurements = []
    for skew in (0, 1):
        output = run([executable, "+HDD_ROM=" + fixture.as_posix()] +
                     (["+DELAYED_BUFG"] if skew else []), "simulate-" + str(skew) + "ps")
        actual = {}
        for label, count in expected.items():
            found = re.findall(r"^PASS: " + re.escape(label) + r"=(\d+)$", output, re.MULTILINE)
            if found != [str(count)]:
                raise ValueError("Missing or incorrect CPU peek coverage: " + label)
            actual[label] = count
        if output.count("PASS: raw logical mapping ") != 8 or "PASS: raw logical mapping total=7" not in output:
            raise ValueError("CPU peek did not complete all seven hot-mapping/floating-bus groups")
        measurements.append({"clock_skew_ps": skew, "counts": actual, "hot_mapping_groups": 7})
        print(output, flush=True)
    if hashes() != before:
        raise ValueError("Production or test inputs changed during CPU peek qualification")
    (build / "validation.json").write_text(json.dumps({
        "schema_version": 1, "status": "PASS", "files": before, "measurements": measurements,
        "hdd_rom_sha256": hashlib.sha256(rom).hexdigest(),
        "checks": ["every CPU bank, slot-selection byte, IOLC/all-bank-shadow/RW mode",
            "ROM1/ROM3, VPB, complete MMU input state and Zip visibility vectors",
            "CPU and DMA in deliberately different IO/bank classes",
            "independent CPU/native Zip read selectors over256 register-state patterns",
            "all eight CPU/native fallback lanes, cached/bypass and valid/invalid CPU cycles",
            "HDD DMA holds CPU and retained floating bus",
            "LC read-soft-switch, RAMRD, IOLC RAM vectors, mapped ROM vectors, invalid-cycle, reset and hot-ROM mapping"],
        "limits": ["Synthetic fixtures and forced P65 bus outputs; real instruction execution is tested separately.",
                   "No synthesis, routed timing, Apple ROM boot or physical hardware claim is implied."]}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
