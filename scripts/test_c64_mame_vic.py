#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Differential VIC BA/AEC frame traces using pinned MAME execution methods.

Static registers settle for a complete warm-up frame. No drawing, register
edge effects, collisions, lightpen or complete-machine conformance is claimed.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from port_c64_vic import port as generate_vic_port, validate_reference as validate_vic_reference

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / "third_party/mame_reference"
COMMIT = "122719e8d13e2fdb7dd33886455484207de59fc8"
VIC = ROOT / "hdl/core/c64/ports/video_vicII_656x.vhd"
VIC_UPSTREAM = ROOT / "third_party/c64/upstream/rtl/video_vicII_656x.vhd"
H = ROOT / "tests/c64_mame/vic_bus_harness.cpp"
BENCH = ROOT / "hdl/sim/tb_c64_vic_bus.sv"
SCENARIOS = (
    "blank", "display-scroll0", "display-scroll3", "display-scroll7",
    "sprite0", "sprite7", "all-sprites", "staggered-sprites",
    "expanded-sprites", "mixed-expansion", "sprites-y255", "sprites-y0",
)
COMPARED_FIELDS = ("line", "cycle", "raster", "ba", "aec")


def sha(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def block(source: str, marker: str) -> str:
    start = source.index(marker)
    cursor = source.index("{", start) + 1
    depth = 1
    while depth:
        if source[cursor] == "{": depth += 1
        if source[cursor] == "}": depth -= 1
        cursor += 1
    return source[start:cursor]


def selections(value: str, supported: range, name: str) -> list[int]:
    try:
        items = [int(item) for item in value.split(",")]
    except ValueError as exc:
        raise ValueError(name + " must be a nonempty comma-separated integer list") from exc
    if len(set(items)) != len(items) or any(item not in supported for item in items):
        raise ValueError(name + " must contain unique supported values: " + ",".join(map(str, supported)))
    return items


def ownership_writers(source: str, extracted: list[str]) -> dict:
    """Audit every direct write/bulk initialization of bus-ownership state.

    The reference commit and whole-file digest are also checked. The omitted
    register-write entry point has known side effects, deliberately outside
    these static-register fixtures; rendering methods have no such writers.
    """
    methods = {}
    for name in [*extracted, "write"]:
        match = re.search(r"^[^\n]*\bmos6566_device::" + name + r"\([^\n]*\)(?: const)?\n\{", source, re.M)
        if not match:
            raise ValueError("Ownership audit missing method " + name)
        body = block(source, match.group())
        methods[name] = (match.start(), match.start() + len(body))
    members = r"m_(?:ba|aec|aec_delay|badline|badlines_enabled|spr_dma|spr_mcbase|spr_mc|spr_yff)\b"
    assignment = rf"\b{members}\s*(?:\[[^\]\n]+\])?\s*(?:=(?!=)|[|&^+*/%~-]=|<<=|>>=|\+\+|--)"
    increment = rf"(?:\+\+|--)\s*\b{members}"
    bulk = rf"(?:std::(?:fill|copy)|memset|memcpy)[^\n]*\b{members}"
    writers = []
    for match in re.finditer(rf"(?:{assignment})|(?:{increment})|(?:{bulk})", source):
        owner = next((name for name, (start, end) in methods.items() if start <= match.start() < end), None)
        if owner is None:
            raise ValueError("Unaccounted ownership writer at line " + str(source.count("\n", 0, match.start()) + 1))
        writers.append({"line": source.count("\n", 0, match.start()) + 1,
                        "method": owner, "extracted": owner in extracted,
                        "text": source[match.start():source.find("\n", match.start())].strip()})
    if not writers or not any(item["method"] == "execute_run" for item in writers):
        raise ValueError("Ownership writer audit found no scheduler writes")
    # Constructor initializers of BA/AEC are overwritten by actual reset;
    # reject any unreviewed initialization of the remaining tracked state.
    initializers = []
    for match in re.finditer(rf"^\s*,\s*({members})\(([^\n]*)\)", source, re.M):
        if match.group(1) not in ("m_ba", "m_aec") or match.group(2) != "1":
            raise ValueError("Unreviewed ownership constructor initialization")
        initializers.append({"member": match.group(1), "value": match.group(2)})
    return {"writers": writers, "constructor_initializers_reset_overwrites": initializers,
            "excluded_write_entry_point": "Static registers settle before recording; write() DEN changes, sprite crunch/Y-expansion writes and VIC-IIe fast-mode BA effects are not exercised.",
            "rendering_ownership_writers": 0}


def generate_reference(build: Path, source: str, header: str) -> list[str]:
    constants = source[source.index("constexpr int FIRST_BAD_LINE"):source.index("} // anonymous namespace")]
    struct_names = ["raster_timing", "process_traits"]
    structs = [block(source, "struct mos6566_device::" + n) + ";" for n in struct_names]
    method_names = ["raster_timing_for", "process_traits_for", "sprite_cycle", "build_decode",
                    "sprite_slot_dot", "phase_x", "device_reset", "set_ba", "line_start",
                    "update_badline", "sprite_dma_check", "sprite_phase1", "sprite_phase2",
                    "fetch", "fetch_phi2", "read_color", "fetch_mode", "graphics_fetch",
                    "matrix_fetch", "execute_run"]
    methods = []
    declarations = []
    for name in method_names:
        match = re.search(r"^[^\n]*\bmos6566_device::" + name + r"\([^\n]*\)(?: const)?\n\{", source, re.M)
        if not match: raise ValueError("Missing pinned method " + name)
        body = block(source, match.group())
        methods.append(body)
        declaration = body.split("\n", 1)[0].replace("mos6566_device::", "")
        if name in ("raster_timing_for", "process_traits_for"): declaration = "static " + declaration
        declarations.append(declaration + ";")
    states = header[header.index("\tint32_t m_phi0;"):header.index("\tbool m_vert_ff;") + len("\tbool m_vert_ff;")]
    enum = block(header, "\tenum\n\t{") + ";"
    local_structs = [block(header, "\tstruct " + name) + ";" for name in ("reg_write", "cycle_decode")]
    inline = []
    for name in ("is_viciie", "fast_mode"):
        matches = [line.strip() for line in header.splitlines() if re.search(r"\b" + name + r"\(\) const \{", line)]
        if len(matches) != 1: raise ValueError("Missing pinned inline " + name)
        inline += matches
    cls = """
class mos6566_device {
public:
    static constexpr int MAX_CYCLES_PER_LINE = 65;
    static constexpr int VIC6567_FIRST_DISP_LINE = 0x1c;
    struct raster_timing;
    struct process_traits;
    int m_icount = 0;
    const int m_variant;
    const raster_timing &m_timing;
    const process_traits &m_process;
    timer_double timer;
    timer_double *m_fast_timer = &timer;
    memory_double m_videoram, m_colorram;
    // These doubles cannot change any BA/AEC/badline/sprite-DMA state.
    void draw_until(uint64_t dot) { m_dot = dot; }
    void raise_irq(uint8_t) {}
    void check_raster_irq() {}
    void trigger_lightpen(int) {}
    void m_write_irq(int) {}
    void m_write_ba(int) {}
    void m_write_aec(int) {}
    void m_write_phi2(int) {}
    void m_write_k(int, int) {}
    bool m_read_charrom(offs_t) { return false; }
    mos6566_device(bool ntsc): m_variant(ntsc ? TYPE_6567 : TYPE_6569),
        m_timing(raster_timing_for(m_variant)), m_process(process_traits_for(m_variant)) { build_decode(); }
""" + enum + "\n" + "\n".join(local_structs + inline + declarations) + "\n" + states + "\n};\n"
    (build / "vic_generated.inc").write_text(
        "// Extracted verbatim pinned MAME methods: license:BSD-3-Clause\n// copyright-holders:Curt Coder\n"
        + constants + cls + "\n\n".join(structs + methods) + "\n", encoding="utf-8")
    return method_names


def instrument(source: str) -> str:
    ports = """
        observe_state: out unsigned(3 downto 0);
        observe_sprite: out unsigned(2 downto 0);
        observe_raster: out unsigned(8 downto 0);
        observe_x: out unsigned(9 downto 0);
        observe_dma: out unsigned(7 downto 0);
        observe_badline: out std_logic;
"""
    assigns = """
    observe_state <= to_unsigned(vicCycles'pos(vicCycle), 4);
    observe_sprite <= sprite;
    observe_raster <= rasterY;
    observe_x <= rasterX;
    observe_badline <= '1' when badLine else '0';
    observe_flags: for i in 0 to 7 generate
        observe_dma(i) <= '1' when MDMA(i) else '0';
    end generate;
"""
    out = source.replace("\tport (\n", "\tport (\n" + ports, 1)
    out = out.replace("\nbegin\n", "\nbegin\n" + assigns, 1)
    if out.replace(ports, "", 1).replace(assigns, "", 1) != source:
        raise ValueError("Observation instrumentation changed production body")
    return out


def read_trace(path: Path) -> list[dict[str, int]]:
    with path.open() as stream:
        return [{k: int(v) for k, v in row.items()} for row in csv.DictReader(stream)]


def differences(expected: list[dict], actual: list[dict]) -> list[dict]:
    if len(expected) != len(actual):
        return [{"length_expected": len(expected), "length_actual": len(actual)}]
    return [{"index": i, "reference": e, "rtl": a}
            for i, (e, a) in enumerate(zip(expected, actual))
            if any(e[k] != a[k] for k in COMPARED_FIELDS)]


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--build-dir", type=Path, default=ROOT / "build/c64-mame-expanded-vic")
    p.add_argument("--cases", default="0,1,2,3,4,5,6,7,8,9,10,11")
    p.add_argument("--modes", default="0,1")
    p.add_argument("--vivado-bin", type=Path, default=Path("C:/Xilinx/2025.2/Vivado/bin"))
    p.add_argument("--compiler", type=Path, default=Path("C:/msys64/ucrt64/bin/g++.exe"))
    args = p.parse_args()
    scenarios = selections(args.cases, range(len(SCENARIOS)), "--cases")
    modes = selections(args.modes, range(2), "--modes")
    build = args.build_dir.resolve()
    build_root = (ROOT / "build").resolve()
    if build == build_root or not build.is_relative_to(build_root):
        p.error("--build-dir must be a dedicated subdirectory inside this checkout's ignored build/")
    build.mkdir(parents=True, exist_ok=True)
    receipt = build / "validation.json"; receipt.unlink(missing_ok=True)
    manifest = json.loads((REF / "import_manifest.json").read_text())
    if manifest["commit"] != COMMIT: raise ValueError("Unexpected reference pin")
    cpp = REF / "upstream/src/devices/video/mos6566.cpp"
    hdr = REF / "upstream/src/devices/video/mos6566.h"
    for path in (cpp, hdr):
        item = next(x for x in manifest["files"] if x["path"] == path.relative_to(REF / "upstream").as_posix())
        if sha(path) != item["sha256"]: raise ValueError("Pinned VIC source changed")
    sources = ROOT / "hdl/hdl_sources.txt"
    selected = [line.strip() for line in sources.read_text().splitlines()
                if line.strip().endswith("/video_vicII_656x.vhd")]
    if selected != ["core/c64/ports/video_vicII_656x.vhd"]:
        raise ValueError("Production manifest does not select only the local VIC port")
    validate_vic_reference()
    if VIC.read_text() != generate_vic_port(VIC_UPSTREAM.read_text()):
        raise ValueError("Production VIC port diverges from deterministic generation")
    tracked = [Path(__file__).resolve(), H, BENCH, VIC, VIC_UPSTREAM, cpp, hdr,
               REF / "import_manifest.json", ROOT / "third_party/c64/import_manifest.json",
               ROOT / "provenance/imports.lock.json",
               ROOT / "scripts/port_c64_vic.py", ROOT / "scripts/run_native_tool.py", sources,
               ROOT / "hdl/core/c64/ports/fpga64_sid_iec.vhd"]
    before = {x.relative_to(ROOT).as_posix(): sha(x) for x in tracked}
    methods = generate_reference(build, cpp.read_text(), hdr.read_text())
    writer_audit = ownership_writers(cpp.read_text(), methods)
    observed = build / "video_vicii_observed.vhd"
    observed.write_text(instrument(VIC.read_text()), encoding="utf-8")
    environment = {k:v for k,v in os.environ.items() if k.upper() != "PATH"}
    environment["PATH"] = "C:/msys64/ucrt64/bin" + os.pathsep + os.environ.get("PATH", os.environ.get("Path", ""))
    commands = []
    guard = [sys.executable, (ROOT / "scripts/run_native_tool.py").as_posix(), "--"] if os.name == "nt" else []
    def run(name, command):
        argv = guard + [x.as_posix() if isinstance(x, Path) else str(x) for x in command]
        result = subprocess.run(argv, cwd=build, env=environment, capture_output=True,
            text=True, errors="replace", timeout=600,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        output = result.stdout + result.stderr
        (build / (name + ".log")).write_text(output, encoding="utf-8")
        commands.append({"name":name,"argv":argv,"returncode":result.returncode})
        if result.returncode or re.search(r"\b(?:ERROR|FAIL|FATAL)\s*:|\bFatal\b", output, re.I):
            raise RuntimeError(name + " failed:\n" + output[-10000:])
        return output
    exe = build / "vic_bus_oracle.exe"
    run("compile_reference", [args.compiler,"-std=c++20","-O2","-I",build,H,"-o",exe])
    vb=args.vivado_bin
    run("xvhdl",[vb/"xvhdl.bat","--nolog","--2008",observed])
    run("xvlog",[vb/"xvlog.bat","--nolog","--sv",BENCH])
    run("xelab",[vb/"xelab.bat","--nolog","--relax","--timescale","1ns/1ps","--snapshot","vic_bus_sim","tb_c64_vic_bus"])
    cases=[]; failure=False
    for mode in modes:
        for scenario in scenarios:
            name=f"{'ntsc' if mode else 'pal'}-{scenario}"
            expected_path=build/(name+"-mame.csv"); actual_path=build/(name+"-rtl.csv")
            expected_path.unlink(missing_ok=True)
            actual_path.unlink(missing_ok=True)
            (build/"rtl_trace.csv").unlink(missing_ok=True)
            output=run(name+"-reference",[exe,mode,scenario,expected_path])
            if "PASS reference VIC ownership" not in output: raise RuntimeError("Missing reference pass marker")
            (build/"case.hex").write_text(f"{mode:02x}\n{scenario:02x}\n")
            output=run(name+"-simulate",[vb/"xsim.bat","--nolog","vic_bus_sim","--runall"])
            if "PASS production VIC ownership" not in output: raise RuntimeError("Missing RTL pass marker")
            shutil.copyfile(build/"rtl_trace.csv",actual_path)
            expected=read_trace(expected_path); actual=read_trace(actual_path)
            if len(expected) != (17095 if mode else 19656):
                raise RuntimeError("Reference did not produce exactly one native frame")
            mismatches=differences(expected,actual)
            # Reject deliberate BA corruption and an AEC phase slip even if a
            # real DUT mismatch is present. Never derive reference from RTL.
            mutated=[dict(row) for row in expected]; mutated[100]["ba"]^=1
            check_ba=bool(differences(expected,mutated))
            mutated=[dict(row) for row in expected]
            for i in range(1,len(mutated)): mutated[i]["aec"]=expected[i-1]["aec"]
            check_aec=bool(differences(expected,mutated)) if any(r["aec"]==0 for r in expected) else None
            if not check_ba or check_aec is False: raise RuntimeError("Mutation negative control failed")
            (build/(name+"-mismatch.json")).write_text(json.dumps(mismatches[:128],indent=2)+"\n")
            case={"mode":name.split('-')[0],"scenario":scenario,"name":SCENARIOS[scenario],"cycles":len(expected),"mismatches":len(mismatches),
                "ba_low":sum(r['ba']==0 for r in expected),"aec_low":sum(r['aec']==0 for r in expected),
                "negative_control_ba":check_ba,"negative_control_aec_phase":check_aec,
                "reference_sha256":sha(expected_path),"rtl_sha256":sha(actual_path)}
            cases.append(case); failure |= bool(mismatches)
            print(json.dumps(case),flush=True)
    after={x.relative_to(ROOT).as_posix():sha(x) for x in tracked}
    if before!=after: raise RuntimeError("Source changed during VIC differential")
    result={"schema":1,"status":"FAIL" if failure else "PASS","reference_commit":COMMIT,"source_sha256":before,
        "methods":methods,"ownership_writer_audit":writer_audit,
        "generated_sha256":sha(build/"vic_generated.inc"),"observation_copy_sha256":sha(observed),
        "cases":cases,"counts":{"cases":len(cases),"cycles":sum(c['cycles'] for c in cases),"mismatches":sum(c['mismatches'] for c in cases)},
        "compared_fields":COMPARED_FIELDS,"diagnostic_fields":["dma","badline"],
        "phase_normalization":"Native cycles 1..63 PAL/1..65 NTSC; RTL refresh1 at phase0 anchors cycle11. Sample RTL at CPU request phase28. MAME trace is post-execute_run phi2. RTL addrValid is inverted to physical AEC. Compare a full frame after warm-up starting line0/cycle11; preserve last-line raster-register delay.",
        "commands":commands,"limits":["Static registers after a warm-up frame only; no mid-cycle register-write timing proof.","MAME drawing/IRQ callbacks and memory data use explicit doubles. BA/AEC/badline/sprite DMA execution bodies are verbatim pinned source.","Standalone production VIC ownership only; not complete VIC rendering, machine, raster effect, or hardware conformance."]}
    receipt.write_text(json.dumps(result,indent=2)+"\n")
    return 1 if failure else 0


if __name__=="__main__":
    try: raise SystemExit(main())
    except (OSError,ValueError,RuntimeError,subprocess.SubprocessError) as exc:
        print("ERROR: "+str(exc),file=sys.stderr); raise SystemExit(1)
