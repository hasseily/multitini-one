#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthesize the IIgs VGC and verify mapped legacy/SHR pixels in Xsim."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
CASES = ["hgr-white", "hgr-black", "hgr-with-80col-an3", "lores-lower-nibble",
         "lores-upper-nibble", "double-lores", "dhgr-mono-white", "dhgr-mono-black",
         "dhgr-color-white", "dhgr-color-black", "text40-white", "text40-black",
         "text80-white", "text80-black", "mixed-hgr-top", "mixed-text-bottom",
         "shr320-orange", "shr640-blue"]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/apple-iigs-legacy-video")
    parser.add_argument("--vivado-bin", type=Path,
                        default=Path(os.environ.get("MULTITINI_VIVADO_BIN", "C:/Xilinx/2025.2/Vivado/bin")))
    args = parser.parse_args()
    out = args.build_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)  # Preserve every earlier result.
    sources = ["hdl/core/apple_iigs/ports/vgc.sv", "hdl/core/apple_iigs/ports/runtime_rom.sv",
               "third_party/apple-iigs/upstream/rtl/dhr_lut.vh",
               "hdl/sim/tb_apple_iigs_legacy_video.sv", "scripts/port_apple_iigs_core.py",
               "scripts/synth_apple_iigs_legacy_video.tcl", "scripts/test_apple_iigs_legacy_video.py",
               "scripts/run_native_tool.py"]
    hashes = {name: sha(ROOT / name) for name in sources}
    guard = [sys.executable, "-B", str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    suffix = ".bat" if os.name == "nt" else ""
    commands = []

    def run(name, argv, marker=None, timeout=300):
        argv = [str(value) for value in argv]
        commands.append({"name": name, "argv": guard + argv})
        result = subprocess.run(guard + argv, cwd=out, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=timeout)
        (out / (name + "-console.log")).write_text(result.stdout, encoding="utf-8")
        # Xsim can return zero after $fatal: process success alone is insufficient.
        bad = re.search(r"(?im)^\s*(?:fatal|error)\s*:", result.stdout)
        if result.returncode or bad or (marker and marker not in result.stdout):
            raise RuntimeError(f"{name} failed; see {out / (name + '-console.log')}\n{result.stdout[-6000:]}")
        return result.stdout

    receipt = {"schema": 1, "status": "FAIL", "passed": False, "source_sha256": hashes,
               "commands": commands, "cases": CASES, "checked_pixels": 9198,
               "limits": ["Out-of-context synthesized VGC functional simulation; no SDF or routed timing.",
                          "Synthetic ROM/video RAM; no CPU, MMU, DDR, disk or physical-hardware acceptance.",
                          "Checks settled scanline interiors; full-frame edges are covered separately."]}
    try:
        run("synthesize", [args.vivado_bin / ("vivado" + suffix), "-mode", "batch", "-source",
                           ROOT / "scripts/synth_apple_iigs_legacy_video.tcl", "-tclargs", out],
            "PASS LEGACY_VIDEO_SYNTHESIS")
        run("compile", [args.vivado_bin / ("xvlog" + suffix), "--sv", "--work", "work",
                        ROOT / "hdl/sim/tb_apple_iigs_legacy_video.sv", out / "synthesized.v"])
        run("elaborate", [args.vivado_bin / ("xelab" + suffix), "-L", "unisims_ver",
                          "work.tb_apple_iigs_legacy_video", "work.glbl", "-s", "legacy_video"])
        log = run("simulate", [args.vivado_bin / ("xsim" + suffix), "legacy_video", "-runall",
                               "-onfinish", "quit"], "PASS LEGACY_VIDEO_COMPLETE cases=18 samples=9198")
        actual = re.findall(r"^PASS LEGACY_VIDEO_CASE (\S+) samples=511$", log, re.MULTILINE)
        if actual != CASES:
            raise RuntimeError(f"Missing or duplicate mode checks: {actual}")
        if hashes != {name: sha(ROOT / name) for name in sources}:
            raise RuntimeError("Source inputs changed during verification")
        receipt.update(status="PASS", passed=True, outputs={name: sha(out / name) for name in
                       ["synthesized.v", "synthesized.dcp", "utilization.rpt", "simulate-console.log"]})
        print("PASS mapped IIgs video: 18 cases, 9198 exact pixels")
    except Exception as exc:
        receipt["error"] = str(exc)
        raise
    finally:
        (out / "validation.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
