#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Capture/replay complete-frame VIC and SID native goldens for fast-port work.

Goldens are exact pre-turbo production RTL, not a VICE/audio correctness oracle.
Copies and traces live only in a fresh build/ directory. Observation additions
to the VIC are mechanically reversible; no executable statement is replaced.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BASELINE = "a0bf372718bba99a49e2a7fcf8199b9c2267b5b9"
VIC = "hdl/core/c64/ports/video_vicII_656x.vhd"
SID = (
    "third_party/c64/upstream/rtl/sid/sid_envelope.sv",
    "third_party/c64/upstream/rtl/sid/sid_voice.sv",
    "hdl/core/c64/ports/sid_dac.sv",
    "hdl/core/c64/ports/sid_filter.sv",
    "hdl/core/c64/ports/sid_tables.sv",
    "hdl/core/c64/ports/sid_top.sv",
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def observed_vic(original: str) -> str:
    ports = "\t\tobserve_state : out std_logic_vector(511 downto 0);\n"
    declarations = """
    function observe_bit(b : boolean) return std_logic is
    begin
        if b then return '1'; else return '0'; end if;
    end function;
"""
    body = """
    -- Observation only: no signal below is read by the original VIC.
    process(all)
        variable s : std_logic_vector(511 downto 0);
    begin
        s := (others => '0');
        s(3 downto 0) := std_logic_vector(to_unsigned(vicCycles'pos(vicCycle),4));
        s(12 downto 4) := std_logic_vector(rasterY);
        s(22 downto 13) := std_logic_vector(rasterX);
        s(32 downto 23) := std_logic_vector(colCounter);
        s(42 downto 33) := std_logic_vector(colRestart);
        s(45 downto 43) := std_logic_vector(rowCounter);
        s(46) := idle;
        s(47) := rasterEnable;
        for i in 0 to 7 loop
            s(48+i) := observe_bit(MDMA(i));
            s(56+i) := observe_bit(MActive(i));
            s(64+6*i+5 downto 64+6*i) := std_logic_vector(MCBase(i));
            s(112+6*i+5 downto 112+6*i) := std_logic_vector(MCnt(i));
        end loop;
        s(167 downto 160) := std_logic_vector(MYE_ff);
        s(175 downto 168) := std_logic_vector(MPtr);
        s(176) := fetchBMM; s(177) := fetchECM; s(178) := baLoc;
        s(181 downto 179) := std_logic_vector(baCnt);
        s(182) := IRST; s(183) := IMMC; s(184) := IMBC; s(185) := ILP;
        s(193 downto 186) := std_logic_vector(M2M);
        s(201 downto 194) := std_logic_vector(M2D);
        s(209 downto 202) := std_logic_vector(M2DDelay);
        s(210) := rasterIrqDone; s(211) := M2Mhit; s(212) := M2Dhit;
        s(228 downto 213) := std_logic_vector(MPixelStore);
        s(231 downto 229) := std_logic_vector(sprite);
        s(232) := DEN; s(233) := BMM; s(234) := ECM;
        s(242 downto 235) := std_logic_vector(ME);
        s(250 downto 243) := std_logic_vector(MYE);
        s(258 downto 251) := std_logic_vector(MCDelay);
        s(267 downto 259) := std_logic_vector(rasterCmp);
        s(279 downto 268) := std_logic_vector(nextChar);
        s(287 downto 280) := std_logic_vector(waitingPixels);
        s(295 downto 288) := std_logic_vector(shiftingPixels);
        observe_state <= s;
    end process;
"""
    anchor = "architecture rtl of video_vicii_656x is\n"
    if any(original.count(marker) != 1 for marker in ("\tport (\n", anchor)) or "\nbegin\n" not in original:
        raise ValueError("VIC observation anchor changed")
    observed = original.replace("\tport (\n", "\tport (\n" + ports, 1)
    observed = observed.replace(anchor, anchor + declarations, 1)
    observed = observed.replace("\nbegin\n", "\nbegin\n" + body, 1)
    if observed.replace(ports, "", 1).replace(declarations, "", 1).replace(body, "", 1) != original:
        raise ValueError("VIC observation changed original behavior")
    return observed


def rows(text: str) -> list[dict[str, str]]:
    result = list(csv.DictReader(io.StringIO(text)))
    if not result or any(None in row or None in row.values() for row in result):
        raise ValueError("Empty or malformed native trace")
    if [row["sample"] for row in result] != [str(i) for i in range(len(result))]:
        raise ValueError("Missing/duplicated native samples")
    return result


def compare(reference: str, actual: str) -> list[dict]:
    expected, observed = rows(reference), rows(actual)
    if len(expected) != len(observed) or tuple(expected[0]) != tuple(observed[0]):
        raise ValueError("Native trace lengths/schema differ")
    return [{"row": i, "expected": a, "actual": b}
            for i, (a, b) in enumerate(zip(expected, observed)) if a != b]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--components", default="vic,sid")
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--record-native", action="store_true")
    mode.add_argument("--golden", type=Path)
    args = parser.parse_args()
    components = args.components.split(",")
    if not components or len(set(components)) != len(components) or set(components) - {"vic", "sid"}:
        parser.error("Use unique nonempty components from vic,sid")
    build = (ROOT / args.build_dir).resolve()
    if not build.is_relative_to(ROOT / "build") or build == ROOT / "build" or build.exists():
        parser.error("Use a fresh dedicated directory under build/")
    golden = None
    if args.golden:
        golden_path = (ROOT / args.golden).resolve()
        golden = json.loads(golden_path.read_text())
        if golden.get("status") != "PASS" or golden.get("baseline_commit") != BASELINE or not golden.get("record_native"):
            raise ValueError("Golden is not the pinned native capture")
        for item in golden["cases"]:
            if digest(ROOT / item["trace"]) != item["sha256"]:
                raise ValueError("Golden trace changed")
    source_names = ["scripts/test_c64_turbo_native_ports.py", "scripts/run_native_tool.py"]
    production = ([VIC] if "vic" in components else []) + (list(SID) if "sid" in components else [])
    source_names += production + [f"hdl/sim/tb_c64_turbo_{kind}_native.sv" for kind in components]
    before = {name: digest(ROOT / name) for name in source_names}
    build.mkdir(parents=True)
    captured = build / "inputs"
    captured.mkdir()
    for name in source_names:
        destination = captured / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, destination)
    receipt = {"status": "RUNNING", "baseline_commit": BASELINE, "record_native": args.record_native,
               "source_sha256": before, "cases": [], "commands": [],
               "scope": ["Two complete native frame durations per standard/model; VIC settles two frames, SID one.",
                         "Exact production RTL golden, not a separate emulator correctness oracle.",
                         "Fast-port-idle equivalence when ports exist; no active fast-I/O claim.",
                         "Synthetic memory/register stimuli only; no ROMs or private programs.",
                         "Logical native phase timing; no routed timing, physical audio or board acceptance."],
               "vic_observation": "Pre-edge e15 state and rolling digest of every pixel edge; observation only."}
    env = {key: value for key, value in os.environ.items() if key.upper() != "PATH"}
    env["PATH"] = "C:/msys64/ucrt64/bin;" + os.environ.get("PATH", os.environ.get("Path", ""))
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    vendor = Path("C:/Xilinx/2025.2/Vivado/bin")

    def run(folder: Path, label: str, command: list, binary=False):
        argv = guard + list(map(str, command))
        output = subprocess.run(argv, cwd=folder, env=env, capture_output=True, timeout=600)
        text = (output.stdout + output.stderr).decode("utf-8", "replace")
        log = folder / (label + ".log")
        log.write_text(text)
        receipt["commands"].append({"argv": argv, "cwd": str(folder.relative_to(ROOT)),
                                    "returncode": output.returncode, "log": str(log.relative_to(ROOT)), "sha256": digest(log)})
        if output.returncode or (not binary and re.search(r"\b(?:ERROR|FATAL)\s*:|\bFatal:", text, re.I)):
            raise RuntimeError(label + " failed\n" + text[-4000:])
        return output.stdout if binary else text

    try:
        if args.record_native:
            for number, name in enumerate(production):
                exact = run(build, "pin-" + str(number), ["git", "show", BASELINE + ":" + name], binary=True)
                # Git stores LF while a Windows checkout can use CRLF.
                if exact.replace(b"\r\n", b"\n") != (ROOT / name).read_bytes().replace(b"\r\n", b"\n"):
                    raise ValueError("Native source differs from pinned baseline: " + name)
        for kind in components:
            folder = build / kind
            folder.mkdir()
            top_source = (captured / (VIC if kind == "vic" else SID[-1])).read_text()
            ports_available = bool(re.search(r"\bFAST_PORT\s*(?::|=)", top_source))
            if args.record_native and ports_available:
                raise ValueError("Cannot record fast-port implementation as native golden")
            define = ["-d", "TURBO_PORTS"] if ports_available else []
            bench = captured / f"hdl/sim/tb_c64_turbo_{kind}_native.sv"
            if kind == "vic":
                observed = folder / "video_vicii_observed.vhd"
                observed.write_text(observed_vic(top_source))
                receipt["vic_observed_sha256"] = digest(observed)
                run(folder, "xvhdl", [vendor / "xvhdl.bat", "--nolog", "--2008", observed])
            else:
                run(folder, "xvlog-leaves", [vendor / "xvlog.bat", "--nolog", "--sv", "--relax", *[captured / p for p in SID]])
            run(folder, "xvlog-bench", [vendor / "xvlog.bat", "--nolog", "--sv", *define, bench])
            run(folder, "xelab", [vendor / "xelab.bat", "--nolog", "--relax", "--timescale", "1ns/1ps",
                                  "--snapshot", "native_sim", "tb_c64_turbo_" + kind + "_native"])
            for ntsc in (0, 1):
                for model in ((0, 1) if kind == "sid" else (0,)):
                    identity = f"{kind}-{'ntsc' if ntsc else 'pal'}-{model}"
                    (folder / "case.hex").write_text(f"{ntsc:02x}\n{model:02x}\n")
                    temporary = folder / "native.csv"
                    temporary.unlink(missing_ok=True)
                    output = run(folder, identity, [vendor / "xsim.bat", "--nolog", "native_sim", "--runall"])
                    count = 2 * (65 * 263 if ntsc else 63 * 312)
                    if output.count(f"PASS {kind.upper()} native frames=2 samples={count}") != 1:
                        raise ValueError("Missing complete-frame pass marker")
                    trace = folder / (identity + ".csv")
                    temporary.replace(trace)
                    actual = trace.read_text()
                    if len(rows(actual)) != count:
                        raise ValueError("Incomplete native frame capture")
                    differences = []
                    if golden:
                        candidates = [item for item in golden["cases"] if item["id"] == identity]
                        if len(candidates) != 1:
                            raise ValueError("Missing or duplicate golden case")
                        differences = compare((ROOT / candidates[0]["trace"]).read_text(), actual)
                    # Independent trace corruption must be detected, even in
                    # capture mode. A malformed/empty trace is rejected above.
                    lines = actual.splitlines()
                    fields = lines[1].split(",")
                    fields[1] = "0" if fields[1] != "0" else "1"
                    lines[1] = ",".join(fields)
                    if len(compare(actual, "\n".join(lines))) != 1:
                        raise ValueError("Trace corruption was not rejected")
                    receipt["cases"].append({"id": identity, "samples": count, "frames": 2,
                        "fast_ports_present": ports_available, "trace": str(trace.relative_to(ROOT)),
                        "sha256": digest(trace), "mismatch_count": len(differences), "mismatches": differences[:20],
                        "negative_control": "PASS"})
                    print(identity, "PASS" if not differences else "FAIL", count, "native samples", flush=True)
        receipt["sources_unchanged"] = before == {name: digest(ROOT / name) for name in source_names}
        if not receipt["sources_unchanged"]:
            raise ValueError("Inputs changed during capture/replay")
        receipt["case_count"] = len(receipt["cases"])
        receipt["status"] = "FAIL" if any(item["mismatch_count"] for item in receipt["cases"]) else "PASS"
    except Exception as exc:
        receipt["status"] = "ERROR"
        receipt["error"] = str(exc)
        raise
    finally:
        (build / "validation.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return 0 if receipt["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
