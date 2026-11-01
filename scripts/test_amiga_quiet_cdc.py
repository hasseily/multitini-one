#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Test the exact production top quiescence CDC launch and a live-cone mutant."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TRACKED = ["hdl/board/multitini_one_top.sv", "third_party/appletini/hdl/cdc_bit_sync.sv",
           "hdl/sim/tb_amiga_quiet_cdc.sv", "scripts/test_amiga_quiet_cdc.py", "scripts/run_native_tool.py"]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/amiga-quiet-cdc-tests")
    args = parser.parse_args()
    output = args.build_dir.resolve()
    if not output.is_relative_to(ROOT / "build"):
        raise SystemExit("Select an output inside this repository's build directory")
    output.mkdir(parents=True, exist_ok=True)
    before = {name: digest(ROOT / name) for name in TRACKED}
    source = (ROOT / TRACKED[0]).read_text()
    # Preserve the actual top bytes from source-register declaration through
    # the destination instance. No copied behavioral implementation is tested.
    start = source.index("    reg amiga_quiet_control")
    stop = source.index("    generate if(ACTIVE_MACHINE!=7)", start)
    snippet = source[start:stop]
    if not re.search(r"always\s*@\s*\(posedge\s+control_clk\)", snippet):
        raise RuntimeError("Production launch is not registered on control_clk")
    if snippet.count(".din(amiga_quiet_control)") != 1:
        raise RuntimeError("Destination must consume only the registered source")
    if "genesis_source_quiescent && framebuffer_display_quiescent && amiga_switch_control" not in snippet:
        raise RuntimeError("Production owner/drain terms changed")
    header = """`timescale 1ns/1ps
module amiga_quiet_production(
 input wire control_clk,c64_clk,control_aresetn,c64_aresetn,
 input wire genesis_source_quiescent,framebuffer_display_quiescent,amiga_switch_control,
 output wire amiga_quiet_source);
"""
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        return str(runtime / (name + ".exe")) if os.name == "nt" else name
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    records = []
    synthetic = {}
    for mutation in (False, True):
        name = "live-cone-mutation" if mutation else "production"
        fixture = output / (name + ".sv")
        body = snippet.replace(".din(amiga_quiet_control)", ".din(genesis_source_quiescent && framebuffer_display_quiescent && amiga_switch_control)") if mutation else snippet
        fixture.write_text(header + body + "endmodule\n")
        synthetic[fixture.relative_to(ROOT).as_posix()] = digest(fixture)
        executable = output / (name + ".vvp")
        commands = [("compile", [tool("iverilog"), "-g2012", "-Wall", "-s", "tb_amiga_quiet_cdc", "-o", str(executable),
                                  str(ROOT / TRACKED[1]), str(fixture), str(ROOT / TRACKED[2])]),
                    ("simulate", [tool("vvp"), str(executable)])]
        for stage, command in commands:
            result = subprocess.run(guard + command, cwd=ROOT, capture_output=True, text=True, timeout=60)
            log = result.stdout + result.stderr
            log_path = output / (name + "-" + stage + ".log")
            log_path.write_text(log)
            records.append({"case": name, "stage": stage, "command": command,
                            "returncode": result.returncode, "log": log_path.name, "sha256": digest(log_path)})
            if stage == "compile" and result.returncode:
                raise RuntimeError(log)
            if stage == "simulate":
                if mutation:
                    if result.returncode == 0 or "Unregistered quiescent glitch reached destination acknowledgement" not in log:
                        raise RuntimeError("Live-cone mutation escaped or failed for an unrelated reason: " + log)
                elif result.returncode or "PASS Amiga registered quiescence CDC" not in log:
                    raise RuntimeError(log)
            print(name, stage, "expected result", flush=True)
    after = {name: digest(ROOT / name) for name in TRACKED}
    if before != after:
        raise RuntimeError("Source changed while the fixture ran")
    receipt = {"passed": True, "board_evidence": False, "source_sha256": before, "source_sha256_after": after,
               "synthetic_sha256": synthetic, "top_excerpt_sha256": hashlib.sha256(snippet.encode()).hexdigest(),
               "coverage": ["exact production launch excerpt", "all3owner gates", "3short pulses spanning destination edge",
                            "source/destination reset", "registered launch and2destination-stage latency", "live-cone mutation rejected"],
               "limits": "RTL simulation and source equivalence only; routed report_cdc signoff remains required.", "results": records}
    (output / "validation.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print("PASS exact production CDC launch and live-cone mutation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
