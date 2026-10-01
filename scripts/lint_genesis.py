#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Elaborate the complete Genesis chip model and its common-shell interface."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/genesis-lint"
GUARD = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]


def manifest_sources() -> list[Path]:
    section = "common"
    paths = []
    for line in (ROOT / "hdl/hdl_sources.txt").read_text().splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("["):
            section = line[1:-1].lower()
        elif section in ("common", "genesis"):
            path = (ROOT / "hdl" / line.split("|")[0].strip()).resolve()
            if not path.is_file():
                raise ValueError(f"Missing selected Genesis source: {path}")
            paths.append(path)
    if not any(path.name == "multitini_genesis_adapter.sv" for path in paths):
        raise ValueError("Genesis adapter is absent from the build manifest")
    return paths


def hashes(paths: list[Path]) -> dict[str, str]:
    return {path.relative_to(ROOT).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in paths}


def run(command: list[str], name: str) -> None:
    with (BUILD / f"{name}.log").open("w") as stream:
        result = subprocess.run(GUARD + command, cwd=BUILD, stdout=stream, stderr=subprocess.STDOUT)
    diagnostic = (BUILD / f"{name}.log").read_text()
    # Preserve this gate even if a tool version treats -Wno-fatal as dominant.
    critical_warning = name == "machine" and re.search(
        r"%Warning-(?:UNOPTFLAT|MULTIDRIVEN):", diagnostic)
    if result.returncode or critical_warning:
        print(diagnostic[-16000:])
        raise RuntimeError(f"Genesis {name} failed; see {BUILD / f'{name}.log'}")


def main() -> int:
    global BUILD
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=BUILD,
                        help="directory for logs, elaboration and input hashes")
    BUILD = parser.parse_args().build_dir.resolve()
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        return str(runtime / f"{name}.exe") if os.name == "nt" else shutil.which(name)
    verilator, iverilog = tool("verilator"), tool("iverilog")
    if not verilator or not iverilog:
        raise FileNotFoundError("Genesis lint requires Verilator and Icarus Verilog")
    BUILD.mkdir(parents=True, exist_ok=True)
    (BUILD / "validation.json").unlink(missing_ok=True)
    sources = manifest_sources()
    fixtures = [ROOT / "hdl/sim/xilinx_primitives.sv"]
    for name in ("video_fifo_model.sv", "genesis_primitives.sv"):
        path = ROOT / "hdl/sim" / name
        if path.exists():
            fixtures.append(path)
    captured = sources + fixtures + [Path(__file__).resolve(), ROOT / "hdl/hdl_sources.txt"]
    before = hashes(captured)
    for source in sources:
        if source.suffix == ".txt":
            shutil.copyfile(source, BUILD / source.name)
    hdl = [str(path) for path in sources + fixtures if path.suffix in (".v", ".sv")]
    # No missing-module or unsupported-construct suppression is allowed for the
    # machine path. The full 68000/Z80/VDP/YM models are elaborated here.
    run([verilator, "--lint-only", "--timing", "--top-module", "multitini_genesis_adapter",
         "-Wno-fatal", "-Werror-UNOPTFLAT", "-Wno-TIMESCALEMOD", "-Wno-DECLFILENAME", *hdl], "machine")
    # The only absent sources may be the generated PS wrapper and real Xilinx
    # I/O primitives. Check the unresolved census before allowing Icarus -i,
    # so a missing Genesis/common transport module cannot silently pass.
    allowed_missing = {"zynq_ps_bd_wrapper", "IDELAYCTRL", "IOBUF", "IDELAYE2", "IDDR"}
    for pal in (0, 1):
        command = [iverilog, "-g2012", "-Wall", "-Wimplicit", "-s", "multitini_one_top",
                   "-P", "multitini_one_top.ACTIVE_MACHINE=4",
                   "-P", f"multitini_one_top.GENESIS_PAL_MODE={pal}",
                   "-o", str(BUILD / f"genesis-pal{pal}.vvp"), *hdl]
        census = subprocess.run(GUARD + command, cwd=BUILD, capture_output=True, text=True)
        diagnostic = census.stdout + census.stderr
        (BUILD / f"shell-pal{pal}-boundary.log").write_text(diagnostic)
        missing = set(re.findall(r"Unknown module type: (\w+)", diagnostic))
        other_errors = [line for line in diagnostic.splitlines()
                        if "error:" in line and "Unknown module type:" not in line]
        if missing != allowed_missing or other_errors or census.returncode == 0:
            raise RuntimeError(f"Unexpected Genesis shell boundary: missing={missing}; errors={other_errors}")
        run(command[:1] + ["-i"] + command[1:], f"shell-pal{pal}")
    after = hashes(captured)
    if before != after:
        raise RuntimeError("Genesis lint inputs changed while the checks were running")
    result = {"schema": 1, "files": before,
              "verilator": subprocess.check_output(GUARD + [verilator, "--version"], text=True).strip(),
              "checks": ["complete chip-model elaboration", "NTSC shell", "PAL shell"],
              "limits": ["PS wrapper and vendor timing require Vivado; this is not board qualification"]}
    (BUILD / "validation.json").write_text(json.dumps(result, indent=2) + "\n")
    print("PASS: complete Genesis chip model and NTSC/PAL shell elaboration")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
