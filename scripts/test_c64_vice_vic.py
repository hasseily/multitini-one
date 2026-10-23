#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bounded VICE 3.10/production-VIC IRQ, graphics-address and REU-BA comparisons.

No ROMs or private programs. VICE functions/tables are extracted unchanged;
the complete emulator, pixels and actual machine IRQ sampling are outside scope.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

from c64_vice_oracle import COMMIT, REFERENCE, verify_reference
from port_c64_vic import port, validate_reference

ROOT = Path(__file__).resolve().parents[1]
REF = REFERENCE / "upstream/vice/src/viciisc"
VIC = ROOT / "hdl/core/c64/ports/video_vicII_656x.vhd"
UPSTREAM = ROOT / "third_party/c64/upstream/rtl/video_vicII_656x.vhd"
FETCH_CASES = tuple((*case, 52, 0) for case in (
    (1, 0x3b, 0x14, 0xa5), (0, 0x3b, 0x14, 0xa5),
    (1, 0x1b, 0x14, 0xa5), (0, 0x1b, 0x14, 0xa5),
    (1, 0x7b, 0x14, 0xa5), (1, 0x5b, 0x14, 0xa5),
    (1, 0x3b, 0x1c, 0x5a), (1, 0x1b, 0x1c, 0x5a),
)) + ((1, 0x3b, 0x14, 0xa5, 156, 520), (0, 0x3b, 0x14, 0xa5, 156, 520))
BA_CASES = ((0, 80), (1, 80), (2, 80), (255, 80), (1, 78))
FIELDS = {"irq": ("stage", "line", "irq", "irst"),
          "fetch": ("cycle", "address", "vc", "rc", "code"),
          "ba": ("line", "cycle", "phase", "ba", "ba_reu")}


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def block(text: str, marker: str) -> str:
    if text.count(marker) != 1:
        raise ValueError("Ambiguous VICE extraction: " + marker)
    start = text.index(marker)
    cursor = text.index("{", start) + 1
    depth = 1
    while depth:
        if text[cursor] == "{": depth += 1
        if text[cursor] == "}": depth -= 1
        cursor += 1
    return text[start:cursor]


def extract(kind: str) -> str:
    mem = (REF / "vicii-mem.c").read_text()
    irq = (REF / "vicii-irq.c").read_text()
    cycle = (REF / "vicii-cycle.c").read_text()
    body = []
    if kind == "irq":
        for source, markers in (
            (irq, ("void vicii_irq_set_line(void)", "static inline void vicii_irq_set_line_clk(CLOCK mclk)",
                   "void vicii_irq_raster_set(CLOCK mclk)", "void vicii_irq_raster_trigger(void)")),
            (mem, ("inline static void update_raster_line(void)", "inline static void d011_store(uint8_t value)",
                   "inline static void d012_store(uint8_t value)", "inline static void d019_store(const uint8_t value)",
                   "inline static void d01a_store(const uint8_t value)")),
        ):
            body.extend(block(source, marker) for marker in markers)
        start = cycle.index("    if (vicii.raster_line == vicii.raster_irq_line) {")
        end = cycle.index("\n\n    /* Check vertical border flag */", start)
        body.append("void reference_tick(void) {\n" + cycle[start:end] + "\n}")
    elif kind == "fetch":
        source = (REF / "vicii-fetch.c").read_text()
        body.extend(block(source, marker) for marker in (
            "inline static uint16_t g_fetch_addr(uint8_t mode)",
            "inline static int is_char_rom(uint16_t addr)", "uint8_t vicii_fetch_graphics(void)"))
    else:
        model = (REF / "vicii-chip-model.c").read_text()
        body.append(model[model.index("struct ViciiCycle {"):model.index("struct ViciiChipModel {")])
        body.extend(block(model, "static const struct ViciiCycle cycle_tab_" + mode + "[] =") + ";"
                    for mode in ("pal", "ntsc"))
        header = (REF / "viciitypes.h").read_text()
        macro = [line for line in header.splitlines() if line.startswith(("#define VICII_PAL_CYCLE(", "#define VICII_NUM_SPRITES "))]
        if len(macro) != 2: raise ValueError("Missing VICE cycle/sprite constants")
        body.extend(macro)
        body.extend(block(cycle, marker) for marker in (
            "static inline void turn_sprite_dma_on(unsigned int i, int y_exp)",
            "static inline void check_sprite_dma(void)", "int vicii_cycle_reu(void)"))
    # Preserve all notices in the input files, not just an abbreviated SPDX tag.
    notices = []
    for name in ("vicii-mem.c", "vicii-irq.c", "vicii-cycle.c", "vicii-fetch.c", "vicii-chip-model.c", "viciitypes.h"):
        text = (REF / name).read_text()
        notices.append(text[:text.index("*/") + 2])
    return "\n".join(notices) + "\n// Unchanged extracts from VICE " + COMMIT + "\n" + "\n\n".join(body) + "\n"


def observed_source() -> str:
    original = VIC.read_text()
    ports = """
 observe_state: out unsigned(3 downto 0);
 observe_raster: out unsigned(8 downto 0);
 observe_irst: out std_logic;
 observe_vc: out unsigned(9 downto 0);
 observe_rc: out unsigned(2 downto 0);
 observe_code: out unsigned(7 downto 0);
"""
    assignments = """
 observe_state <= to_unsigned(vicCycles'pos(vicCycle),4);
 observe_raster <= rasterY;
 observe_irst <= IRST;
 observe_vc <= colCounter;
 observe_rc <= rowCounter;
 observe_code <= nextChar(7 downto 0);
"""
    if original.count("\tport (\n") != 1 or original.count("\nbegin\n") != 1:
        raise ValueError("VIC observation anchors changed")
    result = original.replace("\tport (\n", "\tport (\n" + ports, 1).replace("\nbegin\n", "\nbegin\n" + assignments, 1)
    if result.replace(ports, "", 1).replace(assignments, "", 1) != original:
        raise ValueError("Observation unexpectedly changed production behavior")
    return result


def compare(expected: list[dict], actual: list[dict], fields: tuple[str, ...]) -> list[dict]:
    if not expected or len(expected) != len(actual):
        raise ValueError(f"Invalid trace lengths: {len(expected)} vs {len(actual)}")
    if any(tuple(row) != fields for row in expected + actual):
        raise ValueError("Trace fields changed")
    return [{"row": n, "vice": e, "rtl": a} for n, (e, a) in enumerate(zip(expected, actual)) if e != a]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir", type=Path, default=Path("build/c64-vice-vic"))
    ap.add_argument("--suites", default="irq,fetch,ba")
    ap.add_argument("--modes", default="0,1")
    args = ap.parse_args()
    suites = args.suites.split(",")
    modes = [int(mode) for mode in args.modes.split(",")]
    if not suites or len(set(suites)) != len(suites) or set(suites) - set(FIELDS): raise ValueError("Invalid suites")
    if not modes or len(set(modes)) != len(modes) or set(modes) - {0, 1}: raise ValueError("Invalid modes")
    build = (ROOT / args.build_dir).resolve()
    if build == ROOT / "build" or not build.is_relative_to(ROOT / "build"):
        raise ValueError("Use a dedicated directory under build/")
    if build.exists(): raise ValueError("Preserve prior evidence; choose a new build directory")
    verify_reference()
    validate_reference()
    if VIC.read_text() != port(UPSTREAM.read_text()): raise ValueError("VIC deterministic port differs")
    selection = (ROOT / "hdl/hdl_sources.txt").read_text()
    if "core/c64/ports/video_vicII_656x.vhd" not in selection or "upstream/rtl/video_vicII_656x.vhd" in selection:
        raise ValueError("Production source selection differs")
    inputs = [VIC, UPSTREAM, Path(__file__), ROOT / "scripts/port_c64_vic.py", ROOT / "scripts/c64_vice_oracle.py",
              ROOT / "scripts/run_native_tool.py", ROOT / "hdl/hdl_sources.txt", REFERENCE / "import_manifest.json"]
    inputs += [REF / name for name in ("vicii-mem.c", "vicii-irq.c", "vicii-cycle.c", "vicii-fetch.c", "vicii-chip-model.c", "viciitypes.h")]
    inputs += [ROOT / f"tests/c64_vice/vic_{kind}_reference.cpp" for kind in suites]
    inputs += [ROOT / f"hdl/sim/tb_c64_vice_vic_{kind}.sv" for kind in suites]
    before = {p.relative_to(ROOT).as_posix(): sha(p) for p in inputs}
    build.mkdir(parents=True)
    receipt = {"status": "RUNNING", "reference_commit": COMMIT, "source_sha256": before,
               "cases": [], "commands": [], "compared_fields": FIELDS,
               "limits": ["Extracted VICE functions and tables with bounded scaffolds; not the complete emulator.",
                          "IRQ checks settled register state, not exact CPU sampling or pixel edge timing.",
                          "Fetch tests independently prescribe VC/RC/matrix bytes; no pixel output equivalence.",
                          "BA fixtures disable DEN and end before sprite termination; CPU BA and separate REU BA checked at phases15/28.",
                          "NMOS PAL/NTSC only; no private ROMs, programs, demo or board acceptance."]}
    env = {k: v for k, v in os.environ.items() if k.upper() != "PATH"}
    env["PATH"] = "C:/msys64/ucrt64/bin;" + os.environ.get("PATH", os.environ.get("Path", ""))
    vb = Path("C:/Xilinx/2025.2/Vivado/bin")
    def run(folder: Path, name: str, args: list) -> str:
        command = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--", *map(str, args)]
        result = subprocess.run(command, cwd=folder, env=env, capture_output=True, text=True, errors="replace", timeout=240)
        output = result.stdout + result.stderr
        log = folder / (name + ".log")
        log.write_text(output)
        receipt["commands"].append({"name": name, "cwd": str(folder), "argv": command,
                                    "returncode": result.returncode, "log": str(log.relative_to(ROOT)), "sha256": sha(log)})
        if result.returncode: raise RuntimeError(name + " failed: " + output[-6000:])
        return output
    try:
        for kind in suites:
            folder = build / kind
            folder.mkdir()
            (folder / f"vice_{kind}_verbatim.inc").write_text(extract(kind))
            (folder / "video_vicii_observed.vhd").write_text(observed_source())
            scaffold = ROOT / f"tests/c64_vice/vic_{kind}_reference.cpp"
            shutil.copy2(scaffold, folder / scaffold.name)
            run(folder, "compile-reference", ["C:/msys64/ucrt64/bin/g++.exe", "-std=c++17", "-O2", scaffold.name, "-o", "reference.exe"])
            run(folder, "xvhdl", [vb / "xvhdl.bat", "--nolog", "--2008", folder / "video_vicii_observed.vhd"])
            run(folder, "xvlog", [vb / "xvlog.bat", "--nolog", "--sv", ROOT / f"hdl/sim/tb_c64_vice_vic_{kind}.sv"])
            run(folder, "xelab", [vb / "xelab.bat", "--nolog", "--relax", "--timescale", "1ns/1ps", "--snapshot", "vic_sim", "tb_" + kind])
            for mode in modes:
                configs = [()] if kind == "irq" else FETCH_CASES if kind == "fetch" else BA_CASES
                for case, config in enumerate(configs):
                    identity = f"{kind}-{'ntsc' if mode else 'pal'}-{case}"
                    (folder / ("mode.hex" if kind == "irq" else "case.hex")).write_text("".join(f"{value:02x}\n" for value in (mode, *config)))
                    output = run(folder, identity + "-reference", [folder / "reference.exe"])
                    reference = folder / (identity + "-vice.csv")
                    reference.write_text(output)
                    expected = list(csv.DictReader(output.splitlines()))
                    required = 12 if kind == "irq" else 15 if kind == "fetch" else 3 * (65 if mode else 63) * 2
                    if len(expected) != required: raise ValueError("Incomplete reference trace")
                    temporary = folder / f"rtl_{kind}.csv"
                    temporary.unlink(missing_ok=True)
                    simulation = run(folder, identity + "-simulation", [vb / "xsim.bat", "--nolog", "vic_sim", "--runall"])
                    if "PASS directed " not in simulation or "Fatal:" in simulation:
                        raise ValueError("Simulation did not complete its assertions")
                    actual_path = folder / (identity + "-rtl.csv")
                    shutil.copy2(temporary, actual_path)
                    with actual_path.open() as stream: actual = list(csv.DictReader(stream))
                    differences = compare(expected, actual, FIELDS[kind])
                    mutation = [dict(row) for row in expected]
                    field = "irq" if kind == "irq" else "address" if kind == "fetch" else "ba_reu"
                    mutation[3][field] = str(int(mutation[3][field]) ^ 1)
                    if len(compare(expected, mutation, FIELDS[kind])) != 1: raise ValueError("Corruption control did not reject")
                    item = {"id": identity, "mode": "NTSC" if mode else "PAL", "config": config,
                            "samples": required, "mismatch_count": len(differences), "mismatches": differences[:100],
                            "reference_trace": str(reference.relative_to(ROOT)), "reference_sha256": sha(reference),
                            "rtl_trace": str(actual_path.relative_to(ROOT)), "rtl_sha256": sha(actual_path),
                            "negative_control": "PASS"}
                    receipt["cases"].append(item)
                    print(identity, "PASS" if not differences else "FAIL", "samples", required, "mismatches", len(differences), flush=True)
        receipt["sources_unchanged"] = before == {p.relative_to(ROOT).as_posix(): sha(p) for p in inputs}
        if not receipt["sources_unchanged"]: raise ValueError("Sources changed during test")
        receipt["status"] = "FAIL" if any(c["mismatch_count"] for c in receipt["cases"]) else "PASS"
        receipt["negative_control"] = "PASS"
        receipt["case_count"] = len(receipt["cases"])
        receipt["generated_sha256"] = {str(p.relative_to(build)): sha(p) for p in build.rglob("*")
                                        if p.is_file() and p.suffix in (".cpp", ".vhd", ".inc")}
    except Exception as exc:
        receipt["status"] = "ERROR"
        receipt["error"] = str(exc)
        raise
    finally:
        (build / "validation.json").write_text(json.dumps(receipt, indent=2) + "\n")
    return 0 if receipt["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
