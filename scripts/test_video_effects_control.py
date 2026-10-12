#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check firmware table writes through control registers to visible DVI effects."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
EMITTER = r'''#include "multitini_video_effects.h"
#include <stdio.h>
int main(void) {
    FILE *words=fopen("effect-control-tables.mem","w");
    FILE *pixels=fopen("effect-control-pixels.mem","w");
    if(!words || !pixels)return 1;
    for(unsigned scene=0;scene<4;++scene) {
        mt_video_effects_settings_t settings;
        mt_video_effects_tables_t tables;
        mt_video_effects_defaults(&settings);
        settings.enabled=scene!=0;
        settings.scanlines=scene ? scene:3;
        if(!mt_video_effects_build(&settings,&tables))return 2;
        for(unsigned i=0;i<MT_VIDEO_EFFECTS_WRITE_COUNT;++i) {
            uint16_t address;uint32_t data;
            if(!mt_video_effects_write(&tables,i,&address,&data))return 3;
            fprintf(words,"%03x%08x\n",(unsigned)address,(unsigned)data);
        }
        for(unsigned y=0;y<9;++y) {
            uint16_t result=mt_video_effects_reference_spatial_rgb565(
                &tables,0xffff,0xffff,0xffff,0,0,y%4>=2,0,y);
            fprintf(pixels,"%04x\n",(unsigned)result);
        }
    }
    return fclose(words) || fclose(pixels);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xpm", action="store_true", help="Use Vivado's real XPM models and device primitives")
    args = parser.parse_args()
    build = ROOT / "build/video-effects-control-tests"
    if args.xpm:
        build /= "xpm"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        return str(runtime / (name + ".exe")) if os.name == "nt" else name
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    sources = ["hdl/generated/multitini_regs_pkg.sv", "hdl/sim/xilinx_primitives.sv",
               "hdl/sim/video_fifo_model.sv", "third_party/appletini/hdl/cdc_bit_sync.sv",
               "hdl/common/multitini_async_mailbox.sv", "hdl/common/multitini_display_reset.sv",
               "hdl/common/multitini_control_regs.sv", "hdl/common/multitini_video_capture.sv",
               "hdl/common/multitini_video_effects.sv", "hdl/board/multitini_video_test.sv",
               "hdl/sim/tb_video_effects_control.sv"]
    if args.xpm:
        sources = [p for p in sources if p not in ("hdl/sim/xilinx_primitives.sv", "hdl/sim/video_fifo_model.sv")]
    tracked = sources + ["firmware/common/video_effects.c", "firmware/include/multitini_video_effects.h",
                         "scripts/test_video_effects_control.py", "scripts/run_native_tool.py",
                         "hdl/board/multitini_one_top.sv"]
    before = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in tracked}
    emitter_source = build / "emit.c"
    emitter_source.write_text(EMITTER)
    emitter = build / ("emit.exe" if os.name == "nt" else "emit")
    image = build / "effects-control.vvp"
    commands = [
        ("emitter-compile", [os.environ.get("CC", tool("gcc") if os.name == "nt" else "cc"),
            "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "firmware/include"),
            str(ROOT / "firmware/common/video_effects.c"), str(emitter_source), "-lm", "-o", str(emitter)]),
        ("emitter-run", [str(emitter)]),
    ]
    vendor_hashes = {}
    if args.xpm:
        vivado = Path(os.environ.get("XILINX_VIVADO", "C:/Xilinx/2025.2/Vivado"))
        vendor_tool = lambda name: str(vivado / "bin" / (name + (".bat" if os.name == "nt" else "")))
        vendor = [vivado / "data/ip/xpm" / p for p in (
            "xpm_cdc/hdl/xpm_cdc.sv", "xpm_memory/hdl/xpm_memory.sv", "xpm_fifo/hdl/xpm_fifo.sv")]
        vendor_hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in vendor}
        commands += [
            ("vendor-compile", [vendor_tool("xvlog"), "--sv", "--work", "multitini_xpm"] + list(map(str, vendor))),
            ("hdl-compile", [vendor_tool("xvlog"), "--sv", "-d", "SYNTHESIS"] +
                [str(ROOT / p) for p in sources] + [str(vivado / "data/verilog/src/glbl.v")]),
            ("hdl-elaborate", [vendor_tool("xelab"), "--timescale", "1ns/1ps", "-L", "multitini_xpm",
                "-L", "unisims_ver", "-s", "effects_control_sim", "work.tb_video_effects_control", "work.glbl"]),
            ("hdl-run", [vendor_tool("xsim"), "effects_control_sim", "-R"]),
        ]
    else:
        commands += [
            ("hdl-compile", [tool("iverilog"), "-g2012", "-s", "tb_video_effects_control", "-o", str(image)] +
                [str(ROOT / p) for p in sources]),
            ("hdl-run", [tool("vvp"), str(image)]),
        ]
    logs = []
    for name, command in commands:
        result = subprocess.run(guard + command, cwd=build, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, timeout=180)
        (build / (name + ".log")).write_text(result.stdout)
        logs.append({"name": name, "returncode": result.returncode})
        if result.returncode or re.search(r"\b(?:Fatal|FATAL|ERROR|Error):", result.stdout):
            print(result.stdout)
            raise RuntimeError(name + " failed")
        if name == "hdl-run":
            marker = "PASS control-register video effects: 7 firmware tables, 1883 staged data writes, 6 strict sequence acknowledgements,"
            if marker not in result.stdout:
                raise RuntimeError("Control-register effects coverage marker missing")
            print(result.stdout.strip())
    after = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in tracked}
    if before != after:
        raise RuntimeError("Effects integration inputs changed during the run")
    if any(hashlib.sha256(Path(p).read_bytes()).hexdigest() != digest for p, digest in vendor_hashes.items()):
        raise RuntimeError("Vendor simulation inputs changed during the run")
    artifacts = [emitter_source, build / "effect-control-tables.mem", build / "effect-control-pixels.mem"]
    (build / "validation.json").write_text(json.dumps({
        "passed": True, "board_tested": False, "logs": logs,
        "source_sha256": before, "source_sha256_after": after,
        "xpm": args.xpm, "vendor_sha256": vendor_hashes,
        "generated_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in artifacts},
    }, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
