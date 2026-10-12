#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check standby RGB in physical DE coordinates with the production effects path."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--renderer", type=Path, help="Explicit archived renderer for a baseline regression")
    parser.add_argument("--output", type=Path, default=ROOT / "build/physical-splash-tests")
    parser.add_argument("--expected-compact-offset", type=int, choices=(0, 1), default=0)
    parser.add_argument("--expect-failure", action="store_true", help="Require legacy 1080p to pass and compact physical pixels to fail")
    args = parser.parse_args()
    build = args.output.resolve()
    if not build.is_relative_to(ROOT / "build") or build == ROOT / "build":
        parser.error("--output must stay under this checkout's build directory")
    build.mkdir(parents=True, exist_ok=True)
    renderer = (args.renderer or ROOT / "hdl/board/multitini_video_test.sv").resolve()
    names = ["hdl/common/multitini_display_pkg.sv", "hdl/sim/xilinx_primitives.sv",
             "hdl/common/multitini_async_mailbox.sv", "hdl/common/multitini_video_effects.sv",
             "hdl/board/multitini_video_test.sv", "hdl/sim/tb_physical_splash.sv"]
    selected = {p: renderer if p == "hdl/board/multitini_video_test.sv" else ROOT / p for p in names}
    selected.update({p: ROOT / p for p in ("scripts/test_physical_splash.py", "scripts/run_native_tool.py")})
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    hashes = lambda: {name: sha(path) for name, path in selected.items()}
    before = hashes()
    def posix(value):
        value = str(value).replace("\\", "/")
        return "/" + value[0].lower() + value[2:] if len(value) > 2 and value[1] == ":" else value
    command = ["verilator", "--binary", "--timing", "--top-module", "tb_physical_splash", "-Wno-fatal",
               "--Mdir", str(build / "obj"), "--build-jobs", "4",
               f"-GCOMPACT_OFFSET={args.expected_compact_offset}"] + [str(selected[p]) for p in names]
    if os.name == "nt":
        shell = "cd " + shlex.quote(posix(build)) + " && export PATH=/ucrt64/bin:/usr/bin:$PATH; exec "
        command = ["C:/msys64/usr/bin/bash.exe", "-lc", shell + shlex.join([posix(p) for p in command])]
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    executable = build / "obj" / ("Vtb_physical_splash.exe" if os.name == "nt" else "Vtb_physical_splash")
    logs = []
    for name, arguments in (("compile", command), ("run", [str(executable)])):
        result = subprocess.run(guard + arguments, cwd=build, capture_output=True, text=True, timeout=180)
        output = result.stdout + result.stderr
        (build / (name + ".log")).write_text(output, encoding="utf-8")
        logs.append({"name": name, "command": arguments, "returncode": result.returncode})
        if name == "compile":
            if result.returncode:
                print(output)
                raise RuntimeError("Physical splash compile failed")
        elif args.expect_failure:
            if result.returncode == 0 or "physical raster oracle mode=1" not in output or "PHYSICAL mode=0" not in output:
                print(output)
                raise RuntimeError("Expected compact physical failure/1080p preservation not reproduced")
            print("PASS archived physical regression: legacy 1080p matches; compact DE-derived pixel oracle rejects the one-column shift")
        else:
            if result.returncode or output.count("PHYSICAL mode=") != 6 or "PASS physical splash:" not in output:
                print(output)
                raise RuntimeError("Physical splash raster coverage failed")
            print("\n".join(line for line in output.splitlines() if line.startswith(("PHYSICAL ", "PASS "))))
    after = hashes()
    if before != after:
        raise RuntimeError("Selected physical splash inputs changed during validation")
    summaries = [line for line in output.splitlines() if line.startswith("PHYSICAL ")]
    rows = [line for line in output.splitlines() if line.startswith("INKROW ")]
    (build / "validation.json").write_text(json.dumps({
        "passed": True, "board_tested": False, "enable_effects": True,
        "expected_failure": args.expect_failure, "expected_compact_physical_offset": args.expected_compact_offset,
        "renderer_override": args.renderer is not None, "renderer_path": str(renderer),
        "source_sha256": before, "source_sha256_after": after, "logs": logs,
        "physical_summary": summaries, "physical_ink_rows": rows,
        "limits": "Simulation of actual ENABLE_EFFECTS=1 output. Physical coordinates derive solely from DE pulse counts; no RTL scan_x or standby_x_q coordinate is used. No board proof.",
    }, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
