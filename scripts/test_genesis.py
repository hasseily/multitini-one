#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run Genesis loader, memory, video, save and real-chip regressions."""

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

from lint_genesis import manifest_sources

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/genesis-tests"
GUARD = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
TESTS = ("loader", "rom_cache", "save", "media_stream", "pads", "eeprom", "cartridge", "ram", "audio_lpf", "vdp_pair", "video_normalizer", "video_bridge", "machine", "adapter")


def main() -> int:
    global BUILD
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--test", choices=TESTS, action="append",
                        help="run a selected test (repeatable; default: all)")
    parser.add_argument("--build-dir", type=Path, default=BUILD,
                        help="directory for logs, binaries and input hashes")
    args = parser.parse_args()
    BUILD = args.build_dir.resolve()
    tests = args.test or TESTS
    BUILD.mkdir(parents=True, exist_ok=True)
    (BUILD / "validation.json").unlink(missing_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        value = str(runtime / f"{name}.exe") if os.name == "nt" else shutil.which(name)
        if value is None:
            raise FileNotFoundError(f"Genesis tests require {name}")
        return value
    sources = manifest_sources()
    sources += [ROOT / "hdl/sim" / name for name in
                ("xilinx_primitives.sv", "genesis_primitives.sv", "video_fifo_model.sv")]
    benches = [ROOT / f"hdl/sim/tb_genesis_{name}.sv" for name in tests]
    tracked = sources + benches + [Path(__file__).resolve(),
                ROOT / "scripts/lint_genesis.py", ROOT / "scripts/test_genesis_vdp_pair.py",
                ROOT / "hdl/hdl_sources.txt"]
    def hashes():
        return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in tracked}
    before = hashes()
    hdl = [str(path) for path in sources if path.suffix in (".v", ".sv")]
    checks = []
    def run(command, directory, stage):
        log = directory / f"{stage}.log"
        with log.open("w") as stream:
            result = subprocess.run(GUARD + list(map(str, command)), cwd=directory,
                                    stdout=stream, stderr=subprocess.STDOUT)
        text = log.read_text(errors="replace")
        if result.returncode:
            print(text[-16000:])
            raise RuntimeError(f"Genesis {directory.name}/{stage} failed: {log}")
        return text
    for name, bench in zip(tests, benches):
        directory = BUILD / name
        directory.mkdir(parents=True, exist_ok=True)
        for source in sources:
            if source.suffix in (".txt", ".mem"):
                shutil.copyfile(source, directory / source.name)
        top = f"tb_genesis_{name}"
        if name == "vdp_pair":
            output = run([sys.executable, ROOT / "scripts/test_genesis_vdp_pair.py",
                          "--build-dir", directory], directory, "proof")
        elif name in ("machine", "video_bridge", "adapter"):
            run([tool("verilator"), "--binary", "--timing", "--top-module", top,
                 "-Wno-fatal", "--build-jobs", "8", "--Mdir", directory / "obj", *hdl, bench],
                directory, "compile")
            executable = directory / "obj" / ("V" + top + (".exe" if os.name == "nt" else ""))
            output = run([executable], directory, "simulate")
        else:
            executable = directory / f"{top}.vvp"
            run([tool("iverilog"), "-g2012", "-Wall", "-s", top,
                 "-o", executable, *hdl, bench], directory, "compile")
            output = run([tool("vvp"), executable], directory, "simulate")
        if "PASS" not in output or re.search(r"\b(?:FAIL|FATAL|ERROR)\b", output):
            raise RuntimeError(f"Genesis {name} did not report a clean success: {output[-4000:]}")
        print("\n".join(line for line in output.splitlines() if "PASS" in line), flush=True)
        checks.append({"name": name, "passed": True})
    if hashes() != before:
        raise RuntimeError("Genesis test inputs changed while the tests were running")
    (BUILD / "validation.json").write_text(json.dumps({"schema": 1,
        "checks": checks, "source_sha256": before,
        "limits": "Portable simulation; vendor implementation and hardware qualification remain separate."},
        indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
