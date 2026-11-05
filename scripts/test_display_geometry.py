#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check legacy wrapping and the opt-in wide full-canvas clipping contract."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iverilog", default="iverilog")
    parser.add_argument("--vvp", default="vvp")
    parser.add_argument("--output", type=Path, default=ROOT / "build/display-geometry-tests")
    args = parser.parse_args()
    build = args.output.resolve()
    if not build.is_relative_to(ROOT / "build"):
        parser.error("Output must remain in the repository build directory")
    build.mkdir(parents=True, exist_ok=True)
    sources = ["hdl/common/multitini_display_pkg.sv",
               "hdl/common/multitini_display_geometry.sv",
               "hdl/common/multitini_display_geometry_pair.sv",
               "hdl/sim/tb_display_geometry.sv"]
    tracked = sources + ["scripts/test_display_geometry.py"]
    before = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in tracked}
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    image = build / "geometry.vvp"
    commands = [
        ("compile", [args.iverilog, "-g2012", "-s", "tb_display_geometry", "-o", str(image)]
         + [str(ROOT / p) for p in sources]),
        ("run", [args.vvp, str(image)]),
    ]
    logs = []
    for name, command in commands:
        result = subprocess.run(guard + command, cwd=build, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=60)
        (build / (name + ".log")).write_text(result.stdout)
        logs.append({"name": name, "returncode": result.returncode})
        if result.returncode:
            print(result.stdout)
            raise RuntimeError(name + " failed")
        if name == "run":
            if ("PASS display geometry: 2304 cases;" not in result.stdout or
                "PASS Spectrum geometry: 344x280 raster" not in result.stdout or
                result.stdout.count("PASS geometry profiles:512 selections") != 3 or
                "PASS wide geometry:30 full-canvas cases" not in result.stdout or
                "PASS virtual capture:6 fullwideAuto modes,120 selections,118 bit-identical" not in result.stdout):
                raise RuntimeError("geometry coverage marker missing")
            pixel_checks = int(re.search(r"(\d+) pixel-seed checks", result.stdout)[1])
            print(result.stdout.strip())
    # Both errors can appear plausible in a 12-bit viewport. Require the
    # independent wide oracle to reject them rather than comparing two DUTs
    # that share the same mistaken truncation or crop-phase implementation.
    original = (ROOT / sources[1]).read_text()
    mutations = []
    for name, old, new in (
        ("wide_width_truncation", "full_width=(WIDTH*int'(numerator))/int'(denominator);",
         "full_width=12'((WIDTH*int'(numerator))/int'(denominator));"),
        ("wide_crop_phase_zero", "start_phase_x=4'((crop_x*int'(denominator))%int'(numerator));",
         "start_phase_x=0;"),
    ):
        if original.count(old) != 1:
            raise RuntimeError("mutation site changed: " + name)
        mutant = build / (name + ".sv")
        mutant.write_text(original.replace(old, new))
        mutant_image = build / (name + ".vvp")
        command = [args.iverilog, "-g2012", "-s", "tb_display_geometry", "-o", str(mutant_image)]
        command += [str(mutant) if p == sources[1] else str(ROOT / p) for p in sources]
        result = subprocess.run(guard + command, cwd=build, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=60)
        (build / (name + "-compile.log")).write_text(result.stdout)
        if result.returncode:
            raise RuntimeError("mutation compile failed: " + name)
        result = subprocess.run(guard + [args.vvp, str(mutant_image)], cwd=build,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=60)
        (build / (name + "-run.log")).write_text(result.stdout)
        if result.returncode == 0 or "wide canvas/origin/seed mismatch" not in result.stdout:
            raise RuntimeError("wide geometry mutation escaped: " + name)
        mutations.append({"name": name, "rejected": True,
                          "mutant_sha256": hashlib.sha256(mutant.read_bytes()).hexdigest()})
    after = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in tracked}
    if before != after:
        raise RuntimeError("Geometry inputs changed during the run")
    (build / "validation.json").write_text(json.dumps({
        "passed": True, "legacy_geometry_cases": 2304, "spectrum_geometry_cases": 16, "profile_selections": 1536,
        "wide_full_canvas_cases": 30, "wide_pixel_seed_checks": pixel_checks,
        "virtual_capture_modes": 6, "virtual_capture_selections": 120,
        "virtual_capture_bit_identical_physical_bundles": 118,
        "mutation_checks": mutations,
        "logs": logs, "source_sha256": before,
        "source_sha256_after": after}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
