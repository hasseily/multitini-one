#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check native DDR reader scaling, buffer ownership, faults, and reset drain."""
from pathlib import Path
import os
import argparse
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dynamic-display", action="store_true")
    args=parser.parse_args()
    build = ROOT / "build/video-effects-tests"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    compiler = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    simulator = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    sources = ["hdl/sim/xilinx_primitives.sv", "hdl/common/multitini_async_mailbox.sv",
               "hdl/common/multitini_video_frame_reader.sv", "hdl/sim/tb_video_frame_reader.sv"]
    for numerator, denominator in (((1,1),(2,1),(3,1),(4,1),(5,1),(6,1),(3,2)) if args.dynamic_display else ((4,1),(6,1),(3,2))):
        output = build / f"reader-{numerator}-{denominator}-dynamic{int(args.dynamic_display)}.vvp"
        subprocess.run(guard + [compiler, "-g2012", "-Wall", "-Wimplicit", "-s", "tb_video_frame_reader",
                       "-P", f"tb_video_frame_reader.DYNAMIC_DISPLAY={int(args.dynamic_display)}",
                       "-P", f"tb_video_frame_reader.SHARED_PIXEL_RESET={int(args.dynamic_display)}",
                       "-P", f"tb_video_frame_reader.WIDTH={67 if args.dynamic_display else 68}",
                       "-P", f"tb_video_frame_reader.SCALE_NUM={numerator}",
                       "-P", f"tb_video_frame_reader.SCALE_DEN={denominator}", "-o", str(output)] +
                       [str(ROOT / path) for path in sources], cwd=build, check=True)
        subprocess.run(guard + [simulator, str(output)], cwd=build, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
