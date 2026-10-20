#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture and exercise the production IIgs fast-floppy path with private ROMs.

The source runner and testbench are captured verbatim alongside the production
inputs. Apple ROMs and boot media stay under the ignored build directory.
"""
from pathlib import Path
import argparse
import hashlib
import json
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--rom-version", type=int, choices=[1, 3], default=3)
    parser.add_argument("--baseline606", action="store_true")
    args, remaining = parser.parse_known_args()
    sys.argv = [sys.argv[0], *remaining]
    source = ROOT / "scripts/test_apple_iigs_boot.py"
    text = source.read_text()

    def once(old, new):
        nonlocal text
        if text.count(old) != 1:
            raise ValueError("Canonical bootstrap runner changed: " + old[:100])
        text = text.replace(old, new, 1)

    once('ROOT/"hdl/sim/tb_apple_iigs_boot.sv"',
         'ROOT/"hdl/sim/tb_apple_iigs_fast_floppy_boot.sv"')
    once('Path(__file__).resolve()]',
         'Path(__file__).resolve(), ROOT/"scripts/test_apple_iigs_fast_floppy_boot.py"]')
    once('if not 0<=args.compatibility_flags<=0x1f:',
         'if not 0<=args.compatibility_flags<=0x9f or args.compatibility_flags&0x60:')
    once('five supported runtime bits', 'supported runtime bits (including fast floppy bit 7)')
    once('"+TRACE_END="+str(args.trace_end)],"simulate")',
         '"+TRACE_END="+str(args.trace_end),"+ROM_VERSION=' + str(args.rom_version) + '"],"simulate")')
    once('"configuration":{"rom":3,', '"configuration":{"rom":' + str(args.rom_version) + ',')
    once('text=run([executable,',
         'record["compiled_executable_sha256"]=sha(executable.read_bytes())\n        text=run([executable,')
    once('if hashes()!=before:\n            raise ValueError("current sources changed during actual-ROM test; captured result is not a current-source qualification")\n        record["source_hashes_unchanged"]=True',
         'record["current_sources_unchanged"]=hashes()==before\n        if any(sha((snapshot/name).read_bytes())!=expected for name,expected in before.items()):\n            raise ValueError("Captured bootstrap input changed")\n        record["captured_sources_unchanged"]=True')
    once('record["milestones"]=[line for line in text.splitlines() if line.startswith(("BOOT_MILESTONE","BOOT_RESULT"))]',
         'record["milestones"]=[line for line in text.splitlines() if line.startswith(("BOOT_MILESTONE","BOOT_RESULT","FAST_FLOPPY_RESULT"))]')
    if args.rom_version == 1:
        once('(("rom3",262144),("char",4096),("adb3",4096),("hdd",2304))',
             '(("rom3",131072),("char",4096),("adb3",4096),("hdd",2304))')
        once('path=args.rom_source/(name+".rom")',
             'path=args.rom_source/(("rom1" if name=="rom3" else "adb1" if name=="adb3" else name)+".rom")')
    if args.baseline606:
        prior = ROOT / "build/iigs-floppy-policy/rom3-final606-180m"
        baseline = prior / "captured"
        # These are the exact source captures retained with the606 package.
        # A baseline run refuses any changed input, even if its HDL still builds.
        receipt = json.loads((prior / "validation.json").read_text())
        if receipt["status"] != "PASS":
            raise ValueError("Prior606 source capture did not pass")
        for name, expected in receipt["files"].items():
            if hashlib.sha256((baseline / name).read_bytes()).hexdigest() != expected:
                raise ValueError("Prior606 source capture changed: " + name)
        pinned = baseline / "hdl/core/apple_iigs/ports/iigs.sv"
        if hashlib.sha256(pinned.read_bytes()).hexdigest() != "70f35366a566add5d27a1740c2074e1c8634714f1d183705f51392e19b9acb52":
            raise ValueError("Delivered606 policy source hash changed")
        base_root = baseline
        once('captured=sources+[',
             'sources=[q for q in sources if q.name not in ("smartport_dev.v","smartport_dev.sv")]\n    sources += [ROOT/"' + base_root.relative_to(ROOT).as_posix() + '/third_party/apple-iigs/upstream/rtl/smartport_dev.v"]\n    sources=[(ROOT/"' + base_root.relative_to(ROOT).as_posix() + '"/q.relative_to(ROOT)) if (ROOT/"' + base_root.relative_to(ROOT).as_posix() + '"/q.relative_to(ROOT)).exists() else q for q in sources]\n    captured=sources+[')
    scope = {"__file__": str(source), "__name__": "fast_floppy_boot_runner"}
    exec(compile(text, str(source), "exec"), scope)
    return scope["main"]()


if __name__ == "__main__":
    raise SystemExit(main())
