#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check bounded Amiga base/overscan/SuperHiRes DDR capture and reader profiles offline."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
COMMON = ["third_party/appletini/hdl/cdc_bit_sync.sv",
          "hdl/common/multitini_async_mailbox.sv", "hdl/sim/amiga_cdc_model.sv"]
CAPTURE = ["hdl/common/multitini_video_capture.sv", "hdl/sim/video_fifo_model.sv",
           "hdl/sim/tb_amiga_capture_profiles.sv"]
READER = ["hdl/common/multitini_video_frame_reader.sv", "hdl/sim/tb_video_frame_reader.sv"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/amiga-capture-tests")
    parser.add_argument("--case", action="append", choices=["capture", "reader-base", "reader-alt", "reader-wide", "reader-wide-native"])
    args = parser.parse_args()
    output = args.build_dir.resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("Evidence must remain under this project's build directory")
    output.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        return str(runtime / (name + ".exe")) if os.name == "nt" else name
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    cases = args.case or ["capture", "reader-base", "reader-alt", "reader-wide", "reader-wide-native"]
    selected = (CAPTURE if "capture" in cases else []) + (READER if any(c.startswith("reader-") for c in cases) else [])
    tracked = list(dict.fromkeys(COMMON + selected + ["scripts/test_amiga_capture.py", "scripts/run_native_tool.py"]))
    hashes = lambda: {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in tracked}
    before = hashes()
    results = []
    passed = False
    def run(name, command):
        p = subprocess.run(guard + command, cwd=ROOT, capture_output=True, text=True, timeout=240)
        text = p.stdout + p.stderr
        (output / (name + ".log")).write_text(text, encoding="utf-8")
        results.append({"name": name, "command": command, "exit_code": p.returncode,
                        "log": name + ".log", "sha256": hashlib.sha256(text.encode()).hexdigest()})
        if p.returncode or re.search(r"\b(?:FATAL|ERROR):", text):
            print(text[-10000:])
            raise RuntimeError(name + " failed")
        if name.endswith("simulate"):
            print(text.strip())
        return text
    try:
        for case in cases:
            capture = case == "capture"
            top = "tb_amiga_capture_profiles" if capture else "tb_video_frame_reader"
            executable = output / (case + ".vvp")
            command = [tool("iverilog"), "-g2012", "-Wall", "-Wimplicit", "-s", top]
            if not capture:
                params = {"WIDTH": 640, "HEIGHT": 512, "ALT_WIDTH": 724, "ALT_HEIGHT": 566,
                          "SOURCE_ALT": int(case == "reader-alt"), "SOURCE_PROFILE": 2 if case.startswith("reader-wide") else int(case=="reader-alt"),
                          "WIDE_WIDTH":1280,"WIDE_HEIGHT":512,"WIDE_BASE":0x23800000,"WIDE_SLOT_SHIFT":21, "DYNAMIC_DISPLAY": 1,
                          "SHARED_PIXEL_RESET": 1, "SCALE_NUM": 1, "SCALE_DEN": 1,
                          "VIEW_X": -7 if case=="reader-wide" else 0, "VIEW_Y": -3 if case=="reader-wide" else 0, "WATCHDOG_NS": 100000000}
                for key, value in params.items():
                    command += ["-P", f"{top}.{key}={value}"]
            run(case + "-compile", command + ["-o", str(executable)] +
                [str(ROOT / name) for name in COMMON + (CAPTURE if capture else READER)])
            text = run(case + "-simulate", [tool("vvp"), str(executable)])
            required = 6 if capture else 1
            if text.count("PASS ") != required:
                raise RuntimeError(case + " did not reach every completion marker")
        passed = True
    finally:
        after = hashes()
        report = {"passed": passed and before == after, "board_evidence": False,
                  "base": {"width": 640, "height": 512, "stride_pixels": 640},
                  "alternate": {"width": 724, "height": 566, "stride_pixels": 768},
                  "slot_bytes": 1048576, "wide_slot_bytes": 2097152,
                  "wide": {"width": 1280, "height": 512, "stride_pixels": 1280}, "cases": cases, "source_sha256": before,
                  "source_sha256_after": after, "results": results}
        (output / "validation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if before != after:
        raise RuntimeError("Captured test sources changed during verification")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
