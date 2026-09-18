#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the streaming CPU1-prepared color pipeline using guarded tools."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    build = ROOT / "build/video-effects-tests"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    compiler = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    simulator = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    sources = ["hdl/sim/xilinx_primitives.sv", "hdl/common/multitini_async_mailbox.sv",
               "hdl/common/multitini_video_effects.sv", "hdl/sim/tb_video_effects.sv"]
    for numerator, denominator in ((4, 1), (6, 1), (3, 2)):
        output = build / f"effects-{numerator}-{denominator}.vvp"
        subprocess.run(guard + [compiler, "-g2012", "-Wall", "-Wimplicit", "-s", "tb_video_effects",
                       "-P", f"tb_video_effects.SCALE_NUM={numerator}",
                       "-P", f"tb_video_effects.SCALE_DEN={denominator}", "-o", str(output)] +
                       [str(ROOT / path) for path in sources], cwd=build, check=True)
        subprocess.run(guard + [simulator, str(output)], cwd=build, check=True)
    output = build / "effects-output.vvp"
    subprocess.run(guard + [compiler, "-g2012", "-s", "tb_video_effects_output", "-o", str(output)] +
                   [str(ROOT / path) for path in sources + ["hdl/board/multitini_video_test.sv"]],
                   cwd=build, check=True)
    subprocess.run(guard + [simulator, str(output)], cwd=build, check=True)
    # Compile the same portable table builder used by CPU1, then independently
    # compare its reference pixels with the HDL's streamed output.
    cc = os.environ.get("CC", str(runtime / "gcc.exe") if os.name == "nt" else "cc")
    emitter = build / ("oracle.exe" if os.name == "nt" else "oracle")
    subprocess.run(guard + [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-I", str(ROOT / "firmware/include"),
                   str(ROOT / "firmware/common/video_effects.c"),
                   str(ROOT / "tests/host/test_video_effects.c"), "-lm", "-o", str(emitter)],
                   cwd=build, check=True)
    vectors = build / "c-vectors.txt"
    with vectors.open("w", encoding="ascii") as stream:
        subprocess.run(guard + [str(emitter), "--vectors"], cwd=build, stdout=stream, check=True)
    output = build / "effects-oracle.vvp"
    subprocess.run(guard + [compiler, "-g2012", "-s", "tb_video_effects_oracle", "-o", str(output)] +
                   [str(ROOT / path) for path in sources], cwd=build, check=True)
    subprocess.run(guard + [simulator, str(output), "+vectors=" + str(vectors)], cwd=build, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
