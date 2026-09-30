#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare the native FPGA spatial engine against the portable C frame kernel."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--small-only", action="store_true")
    parser.add_argument("--dynamic-display", action="store_true")
    args = parser.parse_args()
    build = ROOT / "build/video-frame-filter-tests"
    if args.dynamic_display:
        build /= "dynamic-display"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    tool = lambda name: str(runtime / (name + ".exe")) if os.name == "nt" else name
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    sources = ["hdl/common/multitini_video_frame_filter.sv", "hdl/sim/tb_video_frame_filter.sv",
               "tests/host/video_frame_filter_vectors.c", "firmware/common/video_frame.c",
               "firmware/include/multitini_video_frame.h", "scripts/test_video_frame_filter.py"]
    hashes = lambda: {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sources}
    before = hashes()
    logs = []

    def run(arguments, expected_failure=False):
        result = subprocess.run(guard + arguments, cwd=ROOT, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace")
        logs.append({"command": arguments, "returncode": result.returncode,
                     "expected_failure": expected_failure, "output": result.stdout})
        print(result.stdout, end="", flush=True)
        if (result.returncode != 0) != expected_failure:
            raise RuntimeError("Native frame filter check failed")
        return result.stdout

    oracle = build / ("vectors.exe" if os.name == "nt" else "vectors")
    run([tool("gcc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Ifirmware/include",
         "tests/host/video_frame_filter_vectors.c", "firmware/common/video_frame.c", "-o", str(oracle)])
    for num, den in ((0, 1), (1, 0), (-1, 2), (2, -1), (65, 1), (1, 65)):
        simulation = build / "invalid-scale.vvp"
        run([tool("iverilog"), "-g2012", "-s", "multitini_video_frame_filter",
             "-P", f"multitini_video_frame_filter.SCALE_NUM={num}",
             "-P", f"multitini_video_frame_filter.SCALE_DEN={den}",
             "-o", str(simulation), str(ROOT / sources[0])])
        output = run([tool("vvp"), str(simulation)], expected_failure=True)
        if "Blur scale parameters must be in the range 1..64" not in output:
            raise RuntimeError("Invalid scale did not fail through the parameter guard")
    ratios = ((1, 1), (2, 1), (3, 1), (4, 1), (5, 1), (6, 1), (3, 2)) if args.dynamic_display else ((3, 2), (4, 1), (6, 1))
    cases = [(width, height, 80, 1, num, den)
             for num, den in ratios
             for width, height in ((13, 9), (1, 1), (2, 3))]
    if not args.dynamic_display:
        cases += [(13, 9, 80, 1, 1, 1), (13, 9, 80, 1, 5, 2)]
    if not args.small_only:
        cases += [(160, 144, 1, 0, 6, 1), (382, 270, 1, 0, 4, 1),
                  (404, 250, 1, 0, 4, 1), (540, 720, 1, 0, 3, 2)]
    for width, height, jobs, stalls, num, den in cases:
        directory = build / f"{width}x{height}-s{num}-{den}"
        directory.mkdir(exist_ok=True)
        run([str(oracle), str(width), str(height), str(directory), str(int(jobs > 1)), str(num), str(den)])
        simulation = directory / "filter.vvp"
        run([tool("iverilog"), "-g2012", "-Wall", "-Wimplicit", "-s", "tb_video_frame_filter",
             "-P", f"tb_video_frame_filter.WIDTH={width}", "-P", f"tb_video_frame_filter.HEIGHT={height}",
             "-P", f"tb_video_frame_filter.JOBS={jobs}", "-P", f"tb_video_frame_filter.STALL={stalls}",
             "-P", f"tb_video_frame_filter.SCALE_NUM={num}", "-P", f"tb_video_frame_filter.SCALE_DEN={den}",
             "-P", f"tb_video_frame_filter.DYNAMIC_DISPLAY={int(args.dynamic_display)}",
             "-o", str(simulation), str(ROOT / sources[0]), str(ROOT / sources[1])])
        output = run([tool("vvp"), str(simulation), "+vectors=" + directory.as_posix()])
        if "PASS native frame filter:" not in output:
            raise RuntimeError("Missing filter success marker")
    after = hashes()
    changed = [path for path in sources if before[path] != after[path]]
    (build / "validation.json").write_text(json.dumps({"passed": not changed, "source_sha256": before,
        "source_sha256_after": after, "changed_sources": changed, "dynamic_display": args.dynamic_display,
        "cases": cases, "logs": logs},
        indent=2) + "\n", encoding="utf-8")
    if before != after:
        raise RuntimeError("Filter validation inputs changed during the run: " + ", ".join(changed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
