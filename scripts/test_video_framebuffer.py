#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Native-frame video DMA regressions. Every native tool uses the DLL guard."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    build = ROOT / "build/video-framebuffer-tests"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    compiler = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    simulator = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    sources = ["hdl/sim/xilinx_primitives.sv", "hdl/sim/video_fifo_model.sv",
               "third_party/appletini/hdl/cdc_bit_sync.sv", "hdl/common/multitini_async_mailbox.sv",
               "hdl/common/multitini_video_capture.sv", "hdl/sim/tb_video_capture.sv"]
    for numerator, denominator in ((4, 1), (6, 1), (3, 2)):
        output = build / f"capture-{numerator}-{denominator}.vvp"
        subprocess.run(guard + [compiler, "-g2012", "-Wall", "-s", "tb_video_capture",
                       "-P", f"tb_video_capture.SCALE_NUM={numerator}",
                       "-P", f"tb_video_capture.SCALE_DEN={denominator}", "-o", str(output)] +
                       [str(ROOT / path) for path in sources], cwd=build, check=True)
        subprocess.run(guard + [simulator, str(output)], cwd=build, check=True)
    output = build / "framebuffer-registers.vvp"
    integration_sources = sources[:-1] + ["hdl/common/multitini_video_frame_reader.sv",
                          "hdl/common/multitini_cdc_pulse_toggle.sv",
                          "hdl/common/multitini_video_frame_filter.sv", "hdl/common/multitini_video_frame_engine.sv",
                          "hdl/common/multitini_video_frame_scheduler.sv",
                          "hdl/common/multitini_video_framebuffer.sv", "hdl/sim/tb_video_framebuffer.sv"]
    subprocess.run(guard + [compiler, "-g2012", "-Wall", "-s", "tb_video_framebuffer", "-o", str(output)] +
                   [str(ROOT / path) for path in integration_sources], cwd=build, check=True)
    subprocess.run(guard + [simulator, str(output)], cwd=build, check=True)
    subprocess.run([sys.executable, str(ROOT / "scripts/test_video_frame_reader.py")], cwd=ROOT, check=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
