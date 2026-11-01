#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare both Amiga frame profiles against the independent C video oracle."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
ENGINE = ["hdl/common/multitini_video_frame_engine.sv", "hdl/common/multitini_video_frame_filter.sv",
          "hdl/sim/tb_video_frame_engine.sv"]
FILTER = ["hdl/common/multitini_video_frame_filter.sv", "hdl/sim/tb_video_frame_filter.sv"]
BUFFER = ["third_party/appletini/hdl/cdc_bit_sync.sv", "hdl/common/multitini_async_mailbox.sv",
          "hdl/common/multitini_cdc_pulse_toggle.sv", "hdl/sim/amiga_cdc_model.sv",
          "hdl/sim/video_fifo_model.sv", "hdl/common/multitini_video_capture.sv",
          "hdl/common/multitini_video_frame_reader.sv", "hdl/common/multitini_video_frame_filter.sv",
          "hdl/common/multitini_video_frame_engine.sv", "hdl/common/multitini_video_frame_scheduler.sv",
          "hdl/common/multitini_video_framebuffer.sv", "hdl/sim/tb_video_framebuffer.sv"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/amiga-frame-profile-tests")
    parser.add_argument("--case", action="append", choices=["engine-base", "engine-alt", "engine-wide", "filter-base", "filter-alt", "filter-wide", "framebuffer"])
    parser.add_argument("--small", action="store_true", help="Use small bounded geometry for fast transition diagnosis")
    args = parser.parse_args()
    output = args.build_dir.resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("Evidence must remain under the project's build directory")
    output.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    tool = lambda name: str(runtime / (name + ".exe")) if os.name == "nt" else name
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    cases = args.case or ["engine-base", "engine-alt", "engine-wide", "filter-base", "filter-alt", "filter-wide", "framebuffer"]
    selected = (ENGINE if any(c.startswith("engine") for c in cases) else []) + (FILTER if any(c.startswith("filter") for c in cases) else []) + (BUFFER if "framebuffer" in cases else [])
    tracked = list(dict.fromkeys(selected + ["scripts/test_amiga_frame_profiles.py",
        "scripts/run_native_tool.py", "tests/host/video_frame_engine_vectors.c",
        "tests/host/video_frame_filter_vectors.c", "firmware/common/video_frame.c", "firmware/include/multitini_video_frame.h"]))
    hashes = lambda: {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in tracked}
    before = hashes()
    results = []
    passed = False
    def run(name, command):
        p = subprocess.run(guard + command, cwd=ROOT, capture_output=True, text=True, timeout=360)
        text = p.stdout + p.stderr
        (output / (name + ".log")).write_text(text, encoding="utf-8")
        results.append({"name": name, "command": command, "exit_code": p.returncode,
                        "log": name + ".log", "sha256": hashlib.sha256(text.encode()).hexdigest()})
        if p.returncode or re.search(r"\b(?:FATAL|ERROR):", text):
            print(text[-10000:], flush=True)
            raise RuntimeError(name + " failed")
        if name.endswith("simulate"):
            print(text.strip(), flush=True)
        return text
    try:
        for kind in ("engine", "filter"):
            if any(case.startswith(kind) for case in cases):
                run(kind + "-oracle-compile", [tool("gcc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                    "-Ifirmware/include", f"tests/host/video_frame_{kind}_vectors.c", "firmware/common/video_frame.c",
                    "-o", str(output / (kind + "-oracle.exe"))])
        for case in cases:
            directory = output / case
            directory.mkdir(exist_ok=True)
            params = {}
            if case == "framebuffer":
                top = "tb_video_framebuffer"
                sources = BUFFER
                params = {"PROFILE_TEST": 1, "SHARED_PIXEL_RESET": 1}
            else:
                kind = case.split("-")[0]
                profile = 2 if case.endswith("wide") else 1 if case.endswith("alt") else 0
                base_width, base_height, alt_width, alt_height = (13, 9, 19, 11) if args.small else (640, 512, 724, 566)
                wide_width,wide_height = (65,12) if args.small else (1280,512)
                width, height = (wide_width,wide_height) if profile==2 else (alt_width,alt_height) if profile==1 else (base_width,base_height)
                top = "tb_video_frame_" + kind
                sources = ENGINE if kind == "engine" else FILTER
                oracle = [str(output / (kind + "-oracle.exe")), str(width), str(height), str(directory), "0", "2", "1"]
                if kind == "engine":
                    oracle += [str(int(profile!=0))]  # Geometry transitions invalidate old persistence history.
                run(case + "-oracle", oracle)
                params = {"WIDTH": base_width, "HEIGHT": base_height, "ALT_WIDTH": alt_width, "ALT_HEIGHT": alt_height,
                          "WIDE_WIDTH":wide_width,"WIDE_HEIGHT":wide_height,"SOURCE_PROFILE":profile,
                          "SOURCE_ALT":int(profile==1),"JOBS": 1, "SCALE_NUM": 2, "SCALE_DEN": 1,
                          **({"FAULT_TESTS": 0,"WIDE_SLOT_SHIFT":21, "AUTO_PROFILE_RESET":int(profile!=0), "WATCHDOG_NS": 1000000 if args.small else 100000000} if kind == "engine" else {"STALL": 0})}
            command = [tool("iverilog"), "-g2012", "-Wall", "-Wimplicit", "-s", top]
            for key, value in params.items():
                command += ["-P", f"{top}.{key}={value}"]
            executable = directory / "test.vvp"
            run(case + "-compile", command + ["-o", str(executable)] + [str(ROOT / name) for name in sources])
            text = run(case + "-simulate", [tool("vvp"), str(executable), "+vectors=" + directory.as_posix()])
            if "PASS " not in text:
                raise RuntimeError(case + " did not reach completion")
        passed = True
    finally:
        after = hashes()
        (output / "validation.json").write_text(json.dumps({"passed": passed and before == after,
            "board_evidence": False, "small_geometry": args.small, "cases": cases, "source_sha256": before,
            "source_sha256_after": after, "results": results}, indent=2) + "\n", encoding="utf-8")
    if before != after:
        raise RuntimeError("Frame-profile verification inputs changed during verification")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
