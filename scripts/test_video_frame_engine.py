#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compare full FPGA DMA/persistence/spatial jobs to C, including AXI faults."""
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
    args = parser.parse_args()
    build = ROOT / "build/video-frame-engine-tests"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    tool = lambda name: str(runtime / (name + ".exe")) if os.name == "nt" else name
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    sources = ["hdl/common/multitini_video_frame_engine.sv", "hdl/common/multitini_video_frame_filter.sv",
               "hdl/sim/tb_video_frame_engine.sv", "tests/host/video_frame_engine_vectors.c",
               "firmware/common/video_frame.c", "firmware/include/multitini_video_frame.h",
               "scripts/test_video_frame_engine.py"]
    hashes = lambda: {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest() for path in sources}
    before, logs = hashes(), []

    def run(arguments):
        result = subprocess.run(guard + arguments, cwd=ROOT, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors="replace")
        logs.append({"command": arguments, "returncode": result.returncode, "output": result.stdout})
        print(result.stdout, end="", flush=True)
        if result.returncode:
            raise RuntimeError("Native frame engine check failed")
        return result.stdout

    oracle = build / ("vectors.exe" if os.name == "nt" else "vectors")
    run([tool("gcc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-Ifirmware/include",
         sources[3], sources[4], "-o", str(oracle)])
    cases = [(width, height, 260, int(width == 13), num, den)
             for num, den in ((3, 2), (4, 1), (6, 1))
             for width, height in ((13, 9), (1, 1), (2, 3))]
    cases += [(13, 9, 260, 0, 1, 1), (13, 9, 260, 0, 5, 2)]
    if not args.small_only:
        cases += [(160, 144, 1, 0, 6, 1), (382, 270, 1, 0, 4, 1), (404, 250, 1, 0, 4, 1),
                  (540, 720, 1, 0, 3, 2), (640, 720, 1, 0, 3, 2)]
    for width, height, jobs, faults, num, den in cases:
        directory = build / f"{width}x{height}-s{num}-{den}"
        directory.mkdir(exist_ok=True)
        run([str(oracle), str(width), str(height), str(directory), str(int(jobs > 1)), str(num), str(den)])
        simulation = directory / "engine.vvp"
        run([tool("iverilog"), "-g2012", "-Wall", "-Wimplicit", "-s", "tb_video_frame_engine",
             "-P", f"tb_video_frame_engine.WIDTH={width}", "-P", f"tb_video_frame_engine.HEIGHT={height}",
             "-P", f"tb_video_frame_engine.JOBS={jobs}", "-P", f"tb_video_frame_engine.FAULT_TESTS={faults}",
             "-P", f"tb_video_frame_engine.SCALE_NUM={num}", "-P", f"tb_video_frame_engine.SCALE_DEN={den}",
             "-o", str(simulation)] + [str(ROOT / path) for path in sources[:3]])
        output = run([tool("vvp"), str(simulation), "+vectors=" + directory.as_posix()])
        if "PASS frame engine oracle/deadline:" not in output:
            raise RuntimeError("Missing engine success marker")
    after = hashes()
    if before != after:
        raise RuntimeError("Engine validation inputs changed during the run")
    (build / "validation.json").write_text(json.dumps({"passed": True, "source_sha256": before,
        "source_sha256_after": after, "cases": cases, "axi_schedule":
        "AR/AW blocked up to 8 clocks; R <= one beat per 2 clocks plus periodic stalls; W 3/4 clocks; B delay 10 clocks",
        "frame_cycle_bound_at_133MHz": 2216666, "logs": logs}, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
