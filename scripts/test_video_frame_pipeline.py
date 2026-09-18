#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the actual autonomous two-port DDR video pipeline, optionally with XPM."""
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
    parser.add_argument("--xpm", action="store_true", help="Use Vivado's real XPM models")
    parser.add_argument("--scale", choices=("4", "6", "3:2"), action="append")
    parser.add_argument("--source-pause", action="store_true", help="Hold source readiness low for four complete frames")
    parser.add_argument("--expect-source-failure", action="store_true", help="Record the pre-fix readiness fault through recovery")
    args = parser.parse_args()
    if args.expect_source_failure:
        args.source_pause = True
    build = ROOT / "build/video-frame-pipeline-tests"
    if args.source_pause:
        build /= "source-pause-before-fix" if args.expect_source_failure else "source-pause"
    build /= "xpm" if args.xpm else "portable"
    build.mkdir(parents=True, exist_ok=True)
    sources = ["third_party/appletini/hdl/cdc_bit_sync.sv", "hdl/common/multitini_async_mailbox.sv",
               "hdl/common/multitini_cdc_pulse_toggle.sv", "hdl/common/multitini_video_capture.sv",
               "hdl/common/multitini_video_frame_reader.sv", "hdl/common/multitini_video_frame_filter.sv",
               "hdl/common/multitini_video_frame_engine.sv", "hdl/common/multitini_video_frame_scheduler.sv",
               "hdl/common/multitini_video_framebuffer.sv", "hdl/sim/tb_video_frame_pipeline.sv"]
    if not args.xpm:
        sources = ["hdl/sim/xilinx_primitives.sv", "hdl/sim/video_fifo_model.sv"] + sources
    hashed = sources + ["scripts/test_video_frame_pipeline.py", "scripts/run_native_tool.py"]
    hashes = lambda: {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in hashed}
    before = hashes()
    logs = []
    vendor_hashes = {}
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]

    def run(arguments, name):
        result = subprocess.run(guard + [str(arg) for arg in arguments], cwd=build,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
        (build / (name + ".log")).write_text(result.stdout, encoding="utf-8")
        logs.append({"command": [str(arg) for arg in arguments], "returncode": result.returncode,
                     "log": name + ".log", "output": result.stdout})
        if result.returncode or re.search(r"\b(?:Fatal|FATAL|ERROR|Error):", result.stdout):
            print(result.stdout)
            raise RuntimeError(name + " failed")
        return result.stdout

    success = False
    try:
        if args.xpm:
            vivado = Path(os.environ.get("XILINX_VIVADO", "C:/Xilinx/2025.2/Vivado"))
            tool = lambda name: vivado / "bin" / (name + (".bat" if os.name == "nt" else ""))
            vendor = [vivado / "data/ip/xpm" / path for path in (
                "xpm_cdc/hdl/xpm_cdc.sv", "xpm_memory/hdl/xpm_memory.sv", "xpm_fifo/hdl/xpm_fifo.sv")]
            vendor_hashes = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in vendor}
            run([tool("xvlog"), "--sv", "--work", "multitini_xpm"] + vendor, "vendor-compile")
            wrappers = build / "wrappers.sv"
            wrappers.write_text("\n".join(
                f"module pipeline_{n}_{d}; tb_video_frame_pipeline #(.SCALE_NUM({n}),.SCALE_DEN({d}),"
                f".SOURCE_PAUSE_TEST({int(args.source_pause)}),.EXPECT_SOURCE_FAILURE({int(args.expect_source_failure)})) test(); endmodule"
                for n, d in ((4, 1), (6, 1), (3, 2))) + "\n", encoding="ascii")
            run([tool("xvlog"), "--sv"] + [ROOT / path for path in sources] +
                [wrappers, vivado / "data/verilog/src/glbl.v"], "design-compile")
        else:
            runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
            tool = lambda name: runtime / (name + ".exe") if os.name == "nt" else name
        for scale in args.scale or ("4", "6", "3:2"):
            n, d = (3, 2) if scale == "3:2" else (int(scale), 1)
            name = f"pipeline_{n}_{d}"
            if args.xpm:
                run([tool("xelab"), "--timescale", "1ns/1ps", "-L", "multitini_xpm", "-L", "unisims_ver",
                     "-s", name + "_sim", "work." + name, "work.glbl"], name + "-elaborate")
                output = run([tool("xsim"), name + "_sim", "-R"], name + "-run")
            else:
                simulation = build / (name + ".vvp")
                run([tool("iverilog"), "-g2012", "-Wall", "-Wimplicit", "-s", "tb_video_frame_pipeline",
                     "-P", f"tb_video_frame_pipeline.SCALE_NUM={n}", "-P", f"tb_video_frame_pipeline.SCALE_DEN={d}",
                     "-P", f"tb_video_frame_pipeline.SOURCE_PAUSE_TEST={int(args.source_pause)}",
                     "-P", f"tb_video_frame_pipeline.EXPECT_SOURCE_FAILURE={int(args.expect_source_failure)}",
                     "-o", simulation] + [ROOT / path for path in sources], name + "-compile")
                output = run([tool("vvp"), simulation], name + "-run")
            marker = "PASS source-unready failure reproduced:" if args.expect_source_failure else "PASS autonomous frame pipeline:"
            if marker not in output or re.search(r"\b(?:Fatal|FATAL|ERROR|Error|FAIL)\b", output):
                raise RuntimeError("Missing success marker for " + name)
            print("\n".join(line for line in output.splitlines() if line.startswith("PASS ")), flush=True)
        success = True
    finally:
        after = hashes()
        (build / "validation.json").write_text(json.dumps({"passed": success and before == after,
            "source_pause": args.source_pause, "expected_source_failure": args.expect_source_failure,
            "source_sha256": before, "source_sha256_after": after, "vendor_sha256": vendor_hashes,
            "logs": logs}, indent=2) + "\n", encoding="utf-8")
    if before != after:
        raise RuntimeError("Pipeline validation sources changed during the run")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
