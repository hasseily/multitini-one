#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise real C64 fast VIC/SID ports and the ordered FIO consumer.

This component suite is separate from the native golden replay, complete
machine lockstep, routed timing and board acceptance. It does not claim the
complete mapped-cycle T2 oracle or compatibility with a physical SID waveform.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

from test_c64_turbo_native_ports import SID, observed_vic

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/c64-turbo-fast-ports")
    parser.add_argument("--suites", nargs="+", choices=("fio", "sid", "edges", "vic"),
                        default=["fio", "sid", "edges", "vic"])
    parser.add_argument("--vivado-bin", type=Path, default=Path("C:/Xilinx/2025.2/Vivado/bin"))
    parser.add_argument("--iverilog-bin", type=Path, default=Path("C:/msys64/ucrt64/bin"))
    args = parser.parse_args()
    build = args.build_dir.resolve()
    if not build.is_relative_to(ROOT / "build") or build == ROOT / "build":
        raise ValueError("Use a dedicated directory beneath repository build/")
    if build.exists() or len(set(args.suites)) != len(args.suites):
        raise ValueError("Require a fresh build directory and unique nonempty suites")
    build.mkdir(parents=True)
    sources = {Path(__file__).resolve(), ROOT / "scripts/test_c64_turbo_native_ports.py",
               ROOT / "scripts/run_native_tool.py", ROOT / "scripts/port_c64_vic.py"}
    for path in SID:
        sources.add(ROOT / path)
    sources.update(ROOT / path for path in (
        "hdl/core/c64/multitini_c64_fio_port.sv", "hdl/core/c64/ports/video_vicII_656x.vhd",
        "hdl/sim/tb_c64_turbo_fio.sv", "hdl/sim/tb_c64_turbo_sid_fast.sv",
        "hdl/sim/tb_c64_turbo_sid_edges.sv", "hdl/sim/tb_c64_turbo_vic_fast.sv"))
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    before = {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(sources)}
    receipt = {"schema": 1, "status": "RUNNING", "source_sha256": before, "cases": [],
               "limits": __doc__}
    env = {key: value for key, value in os.environ.items() if key.upper() != "PATH"}
    env["PATH"] = str(args.iverilog_bin) + os.pathsep + os.environ.get("PATH", os.environ.get("Path", ""))
    suffix = ".bat" if os.name == "nt" else ""

    def run(directory: Path, name: str, argv: list[object]) -> str:
        command = [str(value) for value in argv]
        if os.name == "nt":
            command = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--", *command]
        result = subprocess.run(command, cwd=directory, env=env, capture_output=True,
                                text=True, errors="replace", timeout=600,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        output = result.stdout + result.stderr
        (directory / (name + ".log")).write_text(output, encoding="utf-8")
        if result.returncode or re.search(r"\b(?:ERROR|FATAL)\s*:|\bFatal:", output, re.I):
            raise RuntimeError(name + " failed:\n" + output[-8000:])
        return output

    try:
        for suite in args.suites:
            directory = build / suite
            directory.mkdir()
            if suite == "fio":
                for depth in (1, 2, 4, 8):
                    for ack in (1, 2, 3):
                        name = f"depth{depth}-ack{ack}"
                        executable = directory / (name + ".vvp")
                        run(directory, name + "-compile", [args.iverilog_bin / "iverilog.exe", "-g2012",
                            "-s", "tb_c64_turbo_fio", "-P", f"tb_c64_turbo_fio.DEPTH={depth}",
                            "-P", f"tb_c64_turbo_fio.ACK_EDGES={ack}", "-o", executable,
                            ROOT / "hdl/core/c64/multitini_c64_fio_port.sv",
                            ROOT / "hdl/sim/tb_c64_turbo_fio.sv"])
                        output = run(directory, name + "-simulate", [args.iverilog_bin / "vvp.exe", executable])
                        marker = f"PASS FIO depth={depth} ack={ack} entries=1200"
                        if marker not in output:
                            raise RuntimeError("Missing explicit FIO pass marker")
                        receipt["cases"].append({"suite": suite, "depth": depth, "ack_edges": ack,
                                                 "summary": marker})
                        print(marker, flush=True)
                continue
            top = "tb_c64_turbo_" + ("sid_edges" if suite == "edges" else suite + "_fast")
            bench = ROOT / "hdl/sim" / (top + ".sv")
            if suite == "vic":
                observed = directory / "vic_observed.vhd"
                observed.write_text(observed_vic((ROOT / "hdl/core/c64/ports/video_vicII_656x.vhd").read_text()), encoding="utf-8")
                run(directory, "xvhdl", [args.vivado_bin / ("xvhdl" + suffix), "--nolog", "--2008", observed])
                sv = [bench]
            else:
                sv = [ROOT / path for path in SID] + [bench]
            run(directory, "xvlog", [args.vivado_bin / ("xvlog" + suffix), "--nolog", "--sv", "--relax", *sv])
            run(directory, "xelab", [args.vivado_bin / ("xelab" + suffix), "--nolog", "--relax",
                "--timescale", "1ns/1ps", "--snapshot", "fast_sim", top])
            configurations = [(0, 0)] if suite == "edges" else [(n, m) for n in (0, 1) for m in ((0, 1) if suite == "sid" else (0,))]
            prefix = "PASS SID edges " if suite == "edges" else "PASS " + suite.upper() + " fast "
            for ntsc, model in configurations:
                (directory / "case.hex").write_text(f"{ntsc:02x}\n{model:02x}\n")
                output = run(directory, f"simulate-{ntsc}-{model}",
                             [args.vivado_bin / ("xsim" + suffix), "--nolog", "fast_sim", "--runall"])
                lines = [line for line in output.splitlines() if line.startswith(prefix)]
                if len(lines) != 1:
                    raise RuntimeError("Missing unique fast-port pass marker")
                receipt["cases"].append({"suite": suite, "ntsc": ntsc, "model": model, "summary": lines[0]})
                print(lines[0], flush=True)
        receipt["sources_unchanged"] = before == {path.relative_to(ROOT).as_posix(): digest(path) for path in sorted(sources)}
        if not receipt["sources_unchanged"]:
            raise RuntimeError("Sources changed during component validation")
        receipt["status"] = "PASS"
    except Exception as error:
        receipt["status"] = "FAIL"
        receipt["error"] = str(error)
        raise
    finally:
        (build / "validation.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        print("ERROR: " + str(error), file=sys.stderr)
        raise SystemExit(1)
