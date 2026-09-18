#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run video CDC/FIFO regressions with Vivado's actual XPM implementation."""
from pathlib import Path
import argparse
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--top", action="append", choices=("effects", "capture", "frame_reader", "framebuffer"))
    parser.add_argument("--reset-mode", choices=("both", "local", "shared"), default="both",
                        help="Exercise local reset synchronization and/or the shared production-top reset")
    args = parser.parse_args()
    vivado = Path(os.environ.get("XILINX_VIVADO", "C:/Xilinx/2025.2/Vivado"))
    build = ROOT / "build/video-xpm-tests"
    build.mkdir(parents=True, exist_ok=True)
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]

    def run(tool, arguments, log):
        executable = vivado / "bin" / (tool + (".bat" if os.name == "nt" else ""))
        result = subprocess.run(guard + [str(executable)] + arguments, cwd=build,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                text=True, errors="replace")
        (build / log).write_text(result.stdout, encoding="utf-8")
        if result.returncode or re.search(r"\b(?:Fatal|FATAL|ERROR|Error):", result.stdout):
            print(result.stdout)
            raise RuntimeError(f"{tool} failed; see {build / log}")
        return result.stdout

    vendor = vivado / "data/ip/xpm"
    # A private library keeps generated simulator files out of the tool install.
    run("xvlog", ["--sv", "--work", "multitini_xpm"] + [str(vendor / source) for source in (
        "xpm_cdc/hdl/xpm_cdc.sv", "xpm_memory/hdl/xpm_memory.sv", "xpm_fifo/hdl/xpm_fifo.sv")], "vendor-compile.log")
    sources = ["third_party/appletini/hdl/cdc_bit_sync.sv", "hdl/common/multitini_async_mailbox.sv",
               "hdl/common/multitini_cdc_pulse_toggle.sv",
               "hdl/common/multitini_video_frame_filter.sv", "hdl/common/multitini_video_frame_engine.sv",
               "hdl/common/multitini_video_frame_scheduler.sv",
               "hdl/common/multitini_video_effects.sv", "hdl/common/multitini_video_capture.sv",
               "hdl/common/multitini_video_frame_reader.sv", "hdl/common/multitini_video_framebuffer.sv",
               "hdl/sim/tb_video_effects.sv", "hdl/sim/tb_video_capture.sv",
               "hdl/sim/tb_video_frame_reader.sv", "hdl/sim/tb_video_framebuffer.sv"]
    wrappers=build / "scale_wrappers.sv"
    wrapper_sources = [
        f"module {top}_{num}_{den}{'_shared' if shared else ''}; "
        f"{top} #(.SCALE_NUM({num}),.SCALE_DEN({den}),.SHARED_PIXEL_RESET({shared})) test(); endmodule"
        for top in ("tb_video_effects", "tb_video_capture", "tb_video_frame_reader")
        for num,den in ((4,1),(6,1),(3,2)) for shared in (0,1)]
    wrapper_sources.append("module tb_video_framebuffer_shared; "
                           "tb_video_framebuffer #(.SHARED_PIXEL_RESET(1)) test(); endmodule")
    wrappers.write_text("\n".join(wrapper_sources)+"\n", encoding="ascii")
    run("xvlog", ["--sv"] + [str(ROOT / path) for path in sources]+[str(wrappers),str(vivado / "data/verilog/src/glbl.v")], "design-compile.log")
    for suffix in args.top or ("effects", "capture", "frame_reader", "framebuffer"):
        top="tb_video_"+suffix
        scales = ((4, 1), (6, 1), (3, 2)) if top != "tb_video_framebuffer" else ((None, None),)
        reset_modes = (0,1) if args.reset_mode == "both" else (int(args.reset_mode == "shared"),)
        for numerator, denominator, shared in ((n,d,s) for n,d in scales for s in reset_modes):
            name = (top if numerator is None else f"{top}_{numerator}_{denominator}") + ("_shared" if shared else "")
            run("xelab", ["--timescale", "1ns/1ps", "-L", "multitini_xpm", "-L", "unisims_ver", "-s", name+"_sim", "work."+name, "work.glbl"], name + "-elaborate.log")
            output = run("xsim", [name+"_sim", "-R"], name + "-run.log")
            # XSim returns success even after $fatal, so require an explicit
            # success marker and reject assertion failures in the run log.
            if "PASS " not in output or re.search(r"\b(?:Fatal|FATAL|ERROR|Error|FAIL)\b", output):
                print(output)
                raise RuntimeError(f"XPM simulation did not pass: {name}")
            for line in output.splitlines():
                if line.startswith("PASS "):
                    print(f"XPM shared-reset={shared} " + line, flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
