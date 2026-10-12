#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check signed clipped origins, effect phase, borders, UI and DVI alignment."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    build = ROOT / "build/signed-video-geometry-tests"
    build.mkdir(parents=True, exist_ok=True)
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    compiler = str(runtime / "iverilog.exe") if os.name == "nt" else "iverilog"
    simulator = str(runtime / "vvp.exe") if os.name == "nt" else "vvp"
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    effects = ["hdl/sim/xilinx_primitives.sv", "hdl/common/multitini_async_mailbox.sv",
               "hdl/common/multitini_video_effects.sv", "hdl/board/multitini_video_test.sv",
               "hdl/sim/tb_video_signed_geometry.sv"]
    borders = ["hdl/board/multitini_border_overlay.sv", "hdl/sim/tb_border_arithmetic.sv",
               "hdl/sim/tb_border_overlay.sv"]
    tracked = effects + borders + ["scripts/test_signed_video_geometry.py"]
    before = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in tracked}
    logs = []

    def run(name, top, sources, parameters):
        image = build / (name + ".vvp")
        command = guard + [compiler, "-g2012", "-Wall", "-Wimplicit", "-s", top]
        for key, value in parameters.items():
            command += ["-P", f"{top}.{key}={value}"]
        command += ["-o", str(image)] + [str(ROOT / source) for source in sources]
        compiled = subprocess.run(command, cwd=build, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, timeout=120)
        (build / (name + "-compile.log")).write_text(compiled.stdout)
        if compiled.returncode:
            print(compiled.stdout)
            raise RuntimeError(name + " compile failed")
        result = subprocess.run(guard + [simulator, str(image)], cwd=build,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
        (build / (name + ".log")).write_text(result.stdout)
        if result.returncode or "PASS" not in result.stdout:
            print(result.stdout)
            raise RuntimeError(name + " failed")
        print(result.stdout.strip(), flush=True)
        logs.append({"name": name, "returncode": result.returncode})

    for signed in (0, 1):
        run(f"border-arithmetic-signed{signed}", "tb_border_arithmetic", borders[:2],
            {"SIGNED_VIEWPORT": signed})
    run("border-clipping", "tb_border_overlay", [borders[0], borders[2]],
        {"DYNAMIC_DISPLAY": 1, "SIGNED_VIEWPORT": 1})
    geometries = {"auto": (3, 1, 58, 9), "both": (-5, -3, 74, 18),
                  "horizontal": (-5, 2, 74, 8), "vertical": (4, -3, 56, 18)}
    for scale in (1, 2, 3, 4):
        for label, (x, y, width, height) in geometries.items():
            run(f"effects-scale{scale}-{label}", "tb_video_signed_geometry", effects,
                {"SCALE_NUM": scale, "ORIGIN_X": x, "ORIGIN_Y": y,
                 "VIEW_WIDTH": width, "VIEW_HEIGHT": height})
    after = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in tracked}
    if before != after:
        raise RuntimeError("Signed geometry inputs changed during validation")
    (build / "validation.json").write_text(json.dumps({"passed": True, "logs": logs,
        "source_sha256": before, "source_sha256_after": after}, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
