#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the production 6510 port RTL against compiled pinned MAME methods."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/c64-mame-port")
    parser.add_argument("--vivado-bin", type=Path,
                        default=Path(os.environ.get("XILINX_VIVADO", "C:/Xilinx/2025.2/Vivado")) / "bin")
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if not build.is_relative_to((ROOT / "build").resolve()):
        parser.error("--build-dir must remain inside this checkout's ignored build/")
    build.mkdir(parents=True, exist_ok=True)
    receipt = build / "validation.json"
    receipt.unlink(missing_ok=True)
    manifest = ROOT / "third_party/mame_reference/import_manifest.json"
    files = [Path(__file__).resolve(), ROOT / "scripts/c64_mame_oracle.py", manifest,
             ROOT / "tests/c64_mame/oracle_harness.cpp", ROOT / "tests/c64_mame/memory_harness.cpp",
             ROOT / "hdl/core/c64/multitini_c64_6510_port.vhd", ROOT / "hdl/sim/tb_c64_mame_port.sv"]
    files += [manifest.parent / "upstream" / row["path"]
              for row in json.loads(manifest.read_text(encoding="utf-8"))["files"]]

    def hashes() -> dict[str, str]:
        return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in files}

    initial = hashes()
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    environment = {key: value for key, value in os.environ.items() if key.upper() != "PATH"}
    environment["PATH"] = str(runtime) + os.pathsep + os.environ.get("PATH", os.environ.get("Path", ""))

    def run(name: str, command: list[object], timeout: int = 300) -> str:
        args = [value.as_posix() if isinstance(value, Path) else str(value) for value in command]
        result = subprocess.run(args, cwd=build, env=environment, capture_output=True,
                                text=True, errors="replace", timeout=timeout)
        output = result.stdout + result.stderr
        (build / (name + ".log")).write_text(output, encoding="utf-8")
        if result.returncode or re.search(r"\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b", output, re.I):
            raise RuntimeError(f"{name} failed ({result.returncode}):\n{output[-12000:]}")
        return output

    oracle = build / "oracle"
    run("oracle-build", [sys.executable, ROOT / "scripts/c64_mame_oracle.py", "--build-dir", oracle])
    suffix = ".exe" if os.name == "nt" else ""
    guard = [sys.executable, ROOT / "scripts/run_native_tool.py", "--"]
    run("oracle-port-vectors", [*guard, oracle / ("mame6510_oracle" + suffix),
                                "--port-vectors", build / "port_vectors.txt"])
    vendor_suffix = ".bat" if os.name == "nt" else ""
    run("xvhdl", [args.vivado_bin / ("xvhdl" + vendor_suffix), "--nolog", "--2008", files[5]])
    run("xvlog", [args.vivado_bin / ("xvlog" + vendor_suffix), "--nolog", "--sv", files[6]])
    run("xelab", [args.vivado_bin / ("xelab" + vendor_suffix), "--nolog", "--timescale", "1ns/1ps",
                   "--snapshot", "tb_c64_mame_port_sim", "tb_c64_mame_port"])
    output = run("simulate", [args.vivado_bin / ("xsim" + vendor_suffix), "--nolog",
                              "tb_c64_mame_port_sim", "--runall"])
    markers = [line for line in output.splitlines() if line.startswith("PASS: C64 MAME 6510 port ")]
    if len(markers) != 1:
        raise RuntimeError("Missing the single explicit reference comparison pass marker")
    if hashes() != initial:
        raise RuntimeError("Port reference or production source changed during validation")
    receipt.write_text(json.dumps({"status": "PASS", "summary": markers[0], "source_sha256": initial,
        "vectors_sha256": hashlib.sha256((build / "port_vectors.txt").read_bytes()).hexdigest(),
        "reference_commit": "122719e8d13e2fdb7dd33886455484207de59fc8",
        "scope": "Production port vs actual MAME port methods with full C64 pulls and floating decay",
        "limits": ["Native tick wiring and whole-machine behavior are separately tested.",
                   "Simulation does not establish FPGA timing or board acceptance."]}, indent=2) + "\n", encoding="utf-8")
    print(markers[0])
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        raise SystemExit(1)
