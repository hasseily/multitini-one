#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise live base/ALT/WIDE transitions with physical DDR pool aliases."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from test_amiga_frame_profiles import BUFFER

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/amiga-profile-transition")
    parser.add_argument("--mutation-no-hold", action="store_true", help="Require failure when the pixel snapshot hold is removed")
    args = parser.parse_args()
    output = args.build_dir.resolve()
    if not output.is_relative_to(ROOT / "build"):
        parser.error("Use this project's build directory")
    output.mkdir(parents=True, exist_ok=True)
    sources = BUFFER[:-1] + ["hdl/sim/tb_amiga_profile_transition.sv"]
    tracked = sources + ["scripts/test_amiga_profile_transition.py", "scripts/test_amiga_frame_profiles.py", "scripts/run_native_tool.py"]
    hashes = lambda: {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in tracked}
    before = hashes()
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    tool = lambda name: str(runtime / (name + ".exe")) if os.name == "nt" else name
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    commands = ["compile", [tool("iverilog"), "-g2012", "-Wall", "-Wimplicit", "-s", "tb_amiga_profile_transition",
                "-P", "tb_amiga_profile_transition.OMIT_PIXEL_HOLD=" + str(int(args.mutation_no_hold)),
                "-o", str(output / "test.vvp")] + [str(ROOT / name) for name in sources]]
    results = []
    passed = False
    try:
        for name, command in [(commands[0], commands[1]), ("simulate", [tool("vvp"), str(output / "test.vvp")])]:
            p = subprocess.run(guard + command, cwd=ROOT, capture_output=True, text=True, timeout=180)
            text = p.stdout + p.stderr
            (output / (name + ".log")).write_text(text, encoding="utf-8")
            results.append({"name": name, "command": command, "exit_code": p.returncode,
                            "sha256": hashlib.sha256(text.encode()).hexdigest()})
            if name == "simulate" and args.mutation_no_hold:
                if not p.returncode or "Live pixel hold did not snapshot changed geometry" not in text:
                    raise RuntimeError("Missing expected no-pixel-hold mutation failure: " + text)
                print("PASS mutation rejected missing live pixel geometry hold", flush=True)
            elif p.returncode or re.search(r"\b(?:FATAL|ERROR):", text):
                print(text[-10000:], flush=True)
                raise RuntimeError(name + " failed")
            elif name == "simulate":
                print(text, flush=True)
                if text.count("PASS ") != 5:
                    raise RuntimeError("Missing live transition completion")
        passed = True
    finally:
        after = hashes()
        (output / "validation.json").write_text(json.dumps({"passed": passed and before == after,
            "board_evidence": False, "expected_no_hold_failure": args.mutation_no_hold,
            "source_sha256": before, "source_sha256_after": after, "results": results}, indent=2) + "\n", encoding="utf-8")
    if before != after:
        raise RuntimeError("Live transition verification sources changed during the run")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
