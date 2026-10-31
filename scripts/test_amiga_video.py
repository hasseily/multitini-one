#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the ECS active canvas with independently fixed PAL timing/geometry."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["hdl/board/multitini_amiga_video_normalizer.sv",
           "hdl/sim/tb_amiga_video_normalizer.sv"]


def main():
    output = ROOT / "build/amiga-video-tests"
    output.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        return str(runtime / (name + ".exe")) if os.name == "nt" else name
    tracked = SOURCES + ["scripts/test_amiga_video.py"]
    def hashes():
        return {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in tracked}
    before = hashes()
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    executable = output / "video.vvp"
    commands = [("compile", [tool("iverilog"), "-g2012", "-s", "tb_amiga_video_normalizer",
                              "-o", str(executable), *[str(ROOT / name) for name in SOURCES]]),
                ("simulate", [tool("vvp"), str(executable)])]
    for stage, command in commands:
        result = subprocess.run(guard + command, cwd=ROOT, capture_output=True, text=True, timeout=180)
        log = result.stdout + result.stderr
        (output / (stage + ".log")).write_text(log, encoding="utf-8")
        print(log[-12000:])
        if result.returncode:
            return result.returncode
    if log.count("PASS AMIGA") != 6:
        raise RuntimeError("Video simulation did not reach all test completions")
    if before != hashes():
        raise RuntimeError("Amiga video sources changed during verification")
    (output / "validation.json").write_text(json.dumps({"passed": True,
        "source_sha256": before, "board_evidence": False,
        "canvas": {"width": 640, "height": 512, "stride_pixels": 640},
        "native_ticks_per_pal_line": 1816,
        "nonlace_pal_first_line": 41,
        "short_interlace_pal_first_line": 42,
        "ntsc_profile_enabled": False,
        "overrun_fixture": "explicit occupied-bank fault injection"}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
