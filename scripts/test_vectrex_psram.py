#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pin-level LY68L6400 timing/data-order regression; uses guarded tools."""
import os
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/vectrex-psram-tests"


def main():
    BUILD.mkdir(parents=True, exist_ok=True)
    manifest = BUILD / "manifest.json"
    manifest.write_text(json.dumps({"passed": False}) + "\n", encoding="utf-8")
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    compiler = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    simulator = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    launcher = [sys.executable, "-u", str(ROOT / "scripts/run_native_tool.py"), "--"]
    top = "tb_vectrex_psram_burst"
    outcomes = []
    with (BUILD / "test.log").open("wb") as log:
        def emit(message):
            print(message, end="", flush=True)
            log.write(message.encode("utf-8")); log.flush()

        # Clock delay uses v4 routed phase-clock skew, corrected by the
        # measured DRIVE8-to-12 change. Final routing remains a separate gate.
        # Device tACLK and board flight remain independent parameters; phase
        # offsets cover the new MMCM output's0.245ns clock uncertainty.
        cases = (
            dict(name="fast", taclk=2.0, warm=0, warm_b=0, forward=0.0,
                 returning=0.0, clock=1.000, ibuf=0.212, data=1.34, ce=1.34, phase=8.072917),
            dict(name="typical", taclk=4.0, warm=0, warm_b=0, forward=0.5,
                 returning=0.5, clock=2.20, ibuf=0.7, data=2.0, ce=2.0, phase=8.072917),
            dict(name="slow", taclk=6.0, warm=0, warm_b=0, forward=1.0,
                 returning=1.0, clock=3.505, ibuf=1.43, data=2.921, ce=2.921, phase=8.072917),
            dict(name="slow-early-capture", taclk=6.0, warm=1, warm_b=1, forward=1.0,
                 returning=1.0, clock=3.505, ibuf=1.43, data=2.921, ce=2.921, phase=7.827917),
            dict(name="fast-late-capture", taclk=2.0, warm=0, warm_b=1, forward=0.0,
                 returning=0.0, clock=1.000, ibuf=0.212, data=1.34, ce=1.34, phase=8.317917),
        )
        cases += (
            dict(cases[3], name="negative-too-early", phase=7.45, expected_failure=True,
                 failure_message="Read word 0 got zdz9 expected df96 delay=6.0"),
            dict(cases[4], name="negative-too-late", phase=8.65, expected_failure=True,
                 failure_message="Read word 0 got f460 expected df96 delay=2.0"),
        )
        for case in cases:
            outcome = dict(name=case["name"], compile_returncode=None, run_returncode=None,
                           expected_failure=case.get("expected_failure", False),
                           matched_failure=False, failure_message=None,
                           log_start_byte=log.tell())
            emit(f"[CASE {case['name']}]\n")
            executable = BUILD / f"psram-{case['name']}.vvp"
            commands = ([compiler, "-g2012", "-Wall", "-Wimplicit", "-s", top,
                         f"-P{top}.OUTPUT_DELAY={case['taclk']}", f"-P{top}.WARM_QPI={case['warm']}",
                         f"-P{top}.WARM_QPI_B={case['warm_b']}", f"-P{top}.FORWARD_DELAY={case['forward']}",
                         f"-P{top}.CE_OUTPUT_DELAY={case['ce']}", f"-P{top}.CAPTURE_PHASE_NS={case['phase']}",
                         f"-DMT_PSRAM_RETURN_DELAY={case['returning']}",
                         f"-DMT_PSRAM_INPUT_DELAY={case['ibuf']}",
                         f"-DMT_PSRAM_DATA_OUTPUT_DELAY={case['data']}",
                         f"-DMT_PSRAM_CLOCK_OUTPUT_DELAY={case['clock']}",
                         "-o", str(executable),
                         "hdl/sim/vectrex_psram_primitives.sv", "hdl/sim/ly68l6400_model.sv",
                         "hdl/core/vectrex/multitini_vectrex_psram_burst.sv",
                         "hdl/sim/tb_vectrex_psram_burst.sv"], [simulator, str(executable)])
            for index, command in enumerate(commands):
                stage = "compile" if index == 0 else "run"
                emit(f"[{stage.upper()} {case['name']}]\n")
                outcome[f"{stage}_output_start_byte"] = log.tell()
                result = subprocess.run(launcher + command, cwd=ROOT, text=True,
                                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                emit(result.stdout)
                outcome[f"{stage}_returncode"] = result.returncode
                outcome[f"{stage}_output_end_byte"] = log.tell()
                outcome[f"{stage}_output_sha256"] = hashlib.sha256(result.stdout.encode("utf-8")).hexdigest()
                if result.returncode:
                    fatal_lines = [line for line in result.stdout.splitlines() if "FATAL:" in line]
                    if (index == 1 and case.get("expected_failure") and result.returncode == 1 and
                            len(fatal_lines) == 1 and fatal_lines[0].startswith("FATAL:") and
                            fatal_lines[0].endswith(": " + case["failure_message"])):
                        outcome["matched_failure"] = True
                        outcome["failure_message"] = case["failure_message"]
                        emit(f"PASS: expected capture-eye failure for {case['name']}\n")
                        continue
                    return result.returncode
                if index == 1 and case.get("expected_failure"):
                    print(f"Expected capture-eye failure did not occur: {case['name']}")
                    return 1
            emit(f"[END CASE {case['name']}]\n")
            outcome["log_end_byte"] = log.tell()
            outcomes.append(outcome)
    sources = ("hdl/sim/vectrex_psram_primitives.sv", "hdl/sim/ly68l6400_model.sv",
               "hdl/core/vectrex/multitini_vectrex_psram_burst.sv",
               "hdl/sim/tb_vectrex_psram_burst.sv", "scripts/test_vectrex_psram.py")
    manifest.write_text(json.dumps({
        "passed": True,
        "datasheet": "https://www.lyontek.com.tw/pdf/ddr/LY68L6400-1.2.pdf",
        "cases": cases,
        "outcomes": outcomes,
        "log_encoding": "UTF-8",
        "log_offsets": "Zero-based byte offsets, start inclusive and end exclusive",
        "clock_delay_basis": "V4 routed phase-clock skew corrected by measured DRIVE8-to-12 delay: max3.692-0.187=3.505ns, min1.196-0.196=1.000ns; final routed validation separate",
        "clock_uncertainty_ns": 0.245,
        "capture_phase_degrees": 279,
        "capture_setup_ns": 0.012,
        "capture_hold_ns": 0.068,
        "source_sha256": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in sources},
        "log_sha256": hashlib.sha256((BUILD / "test.log").read_bytes()).hexdigest(),
    }, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
