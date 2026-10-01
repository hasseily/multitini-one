#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check stable upstream transitions and the explicit VDP self-clear policy."""
import argparse
from collections import deque
from itertools import product
from pathlib import Path
import os
import sys
import shutil
import subprocess
import json

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build/genesis-vdp-pair"


def reference(state, signals):
    """Solve the original three simultaneous outputs, independently of RTL."""
    a1, a2, b1, b2, c1, c2 = state
    reset, d21, cpu_as, w2, d14, data = signals
    solutions = []
    for a, b, c in product((0, 1), repeat=3):
        clock_a = w2 and cpu_as and (not b or d21)
        expected_a = not (reset or c) and (a1 if clock_a else a2)
        expected_b = not (reset or d21 or a) and (b1 if d14 else b2)
        expected_c = a and (c2 if cpu_as else c1)
        if (a, b, c) == (expected_a, expected_b, expected_c):
            solutions.append((a, b, c))
    # The sampled second latch retains the preceding output. In a bistable
    # feedback state, continuity selects that output rather than a new edge.
    if solutions:
        output = next((value for value in solutions if value[0] == a2), solutions[0])
    else:
        # No simultaneous assignment can satisfy this upstream pulse race.
        # The port explicitly samples the self-clear pulse (c=1), clears
        # dff13 (a=0), and evaluates the unaffected dff15 equation at a=0.
        output = (0, int(not (reset or d21) and (b1 if d14 else b2)), 1)
    a, b, c = output
    clock_a = w2 and cpu_as and (not b or d21)
    next_state = (0 if reset or c else a1 if clock_a else data, a,
                  0 if reset or d21 or a else b1 if d14 else data, b,
                  0 if not a else c1 if not cpu_as else 1, c)
    return output, next_state, not bool(solutions)


def main():
    global BUILD
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=BUILD)
    BUILD = parser.parse_args().build_dir.resolve()
    all_inputs = list(product((0, 1), repeat=6))
    paths = {(0, 0, 0, 0, 0, 0): []}
    queue = deque(paths)
    while queue:
        state = queue.popleft()
        for signals in all_inputs:
            _, successor, _ = reference(state, signals)
            if successor not in paths:
                paths[successor] = paths[state] + [signals]
                queue.append(successor)
    if len(paths) != 11:
        raise AssertionError(f"Unexpected reachable-state census: {len(paths)}")
    vectors = []
    pulse_cases = []
    for state, prefix in paths.items():
        for target in all_inputs:
            if reference(state, target)[2]:
                pulse_cases.append({"state": state, "inputs": target})
            current = (0, 0, 0, 0, 0, 0)
            for signals in [(1, 0, 0, 0, 0, 0)] + prefix + [target]:
                output, current, _ = reference(current, signals)
                packed_inputs = sum(bit << (5-index) for index, bit in enumerate(signals))
                packed_state = sum(bit << (5-index) for index, bit in enumerate(current))
                vectors.append(packed_inputs | output[0] << 6 | output[1] << 7 | output[2] << 8 | packed_state << 9)
    if len(pulse_cases) != 32:
        raise AssertionError(f"Unexpected pulse-race census: {len(pulse_cases)}")
    BUILD.mkdir(parents=True, exist_ok=True)
    (BUILD / "vdp-pair-vectors.txt").write_text("".join(f"{value:04x}\n" for value in vectors))
    (BUILD / "pulse-cases.json").write_text(json.dumps({
        "state_order": ["dff13_l1", "dff13_l2", "dff15_l1", "dff15_l2", "dff12_l1", "dff12_l2"],
        "input_order": ["reset_comb", "dff21", "cpu_as", "w2", "dff14", "w44"],
        "policy": "No fixed point: sample dff12 clear pulse; clear dff13; evaluate dff15 at dff13=0.",
        "cases": pulse_cases}, indent=2) + "\n")
    native_runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    def tool(name):
        return str(native_runtime / f"{name}.exe") if os.name == "nt" else shutil.which(name)
    compiler, runtime = tool("iverilog"), tool("vvp")
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    if not compiler or not runtime:
        raise RuntimeError("The VDP pair proof requires Icarus Verilog")
    executable = BUILD / "pair.vvp"
    subprocess.run(guard + [compiler, "-g2012", "-s", "tb_genesis_vdp_pair", "-o", executable,
                    ROOT / "hdl/core/genesis/ports/vdp_bus_pair.sv",
                    ROOT / "hdl/sim/tb_genesis_vdp_pair.sv"], cwd=BUILD, check=True)
    subprocess.run(guard + [runtime, executable], cwd=BUILD, check=True)
    count = len(paths)*len(all_inputs)
    print(f"PASS VDP cluster closure: {len(paths)} states, {count-len(pulse_cases)} stable transitions, "
          f"{len(pulse_cases)} explicitly resolved upstream pulse races")


if __name__ == "__main__":
    main()
