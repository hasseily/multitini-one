#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise exact top profile CDC and reader snapshots through the real host."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
TOP = "hdl/board/multitini_one_top.sv"
READER = "hdl/common/multitini_video_frame_reader.sv"
HOST = "hdl/core/amiga/multitini_amiga_host.sv"
SOURCES = ["hdl/sim/amiga_cdc_model.sv", "third_party/appletini/hdl/cdc_bit_sync.sv",
           "hdl/common/multitini_async_mailbox.sv", "hdl/common/multitini_display_pkg.sv",
           "hdl/common/multitini_display_geometry.sv", "hdl/common/multitini_display_geometry_pair.sv",
           "hdl/core/amiga/multitini_amiga_ide_block.sv", "hdl/core/amiga/multitini_amiga_diagnostics.sv", HOST, "hdl/sim/tb_amiga_profile_cdc_settle.sv"]
TRACKED = list(dict.fromkeys([TOP, READER] + SOURCES + ["scripts/test_amiga_profile_cdc_settle.py", "scripts/run_native_tool.py"]))


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def excerpt(source, start, stop):
    a = source.index(start)
    b = source.index(stop, a)
    return source[a:b]


def wrapper(top, reader):
    pieces = {
        "profile_declarations": excerpt(top, "    reg [1:0] amiga_profile_source=0", "    wire effective_display_pause"),
        "profile_cdc": excerpt(top, "    always @(posedge c64_clk)\n        if(!c64_aresetn)amiga_profile_source", "    generate if(ACTIVE_MACHINE!=7)"),
        "geometry_declarations": excerpt(top, "    reg [11:0] display_h_active,", "    reg [3:0] display_pixel_mode;"),
        "geometry_tables_and_registers": excerpt(top, "    // Filter coefficients stay in the control domain.", "    multitini_axi3_regs axi_regs"),
        "reader_geometry_declarations": excerpt(reader, "    // Dynamic origins are signed full-image positions.", "    localparam bit HAS_ALT"),
        "reader_pixel_reset": excerpt(reader, "    always @(posedge pixel_clk)\n        if (!mailbox_pixel_run", "    wire control_run"),
        "reader_snapshot": excerpt(reader, "    generate if(DYNAMIC_DISPLAY)begin : g_geometry_snapshot", "    end else begin : g_constant_geometry"),
    }
    bridge = top[top.index("            multitini_amiga_video_bridge video_bridge ("):]
    active_width = re.search(r"\.display_h_active\((.*?)\),\.display_v_active", bridge, re.S)
    if active_width is None:
        raise RuntimeError("Production bridge active-width input not found")
    pieces["bridge_active_width_expression"] = active_width[1]
    if pieces["profile_cdc"].count("#(.WIDTH(2),.DRAIN_ON_RESET(1'b1))") != 2:
        raise RuntimeError("Production profile mailbox contract changed")
    if pieces["geometry_tables_and_registers"].count("multitini_display_geometry_pair #") != 3:
        raise RuntimeError("Expected all three production geometry instances")
    if "seeds_4<=seeds_3" not in pieces["reader_snapshot"]:
        raise RuntimeError("Reader four-stage snapshot contract changed")
    header = """`timescale 1ns/1ps
module amiga_top_profile_excerpt(
 input wire c64_clk,control_clk,video_clk,c64_aresetn,control_aresetn,video_control_resetn,
 input wire [1:0] amiga_capture_mode,input wire amiga_switch_source,
 input wire genesis_source_quiescent,framebuffer_display_quiescent,
 input wire [3:0] display_active_mode,display_pixel_mode,input wire [2:0] display_pixel_multiplier);
 localparam integer ACTIVE_MACHINE=7,C64_NTSC_MODE=0;
 localparam bit DYNAMIC_DISPLAY=1;
 localparam integer WIDTH=640,HEIGHT=512,VIEW_X=0,VIEW_Y=0,SCALE_NUM=1,SCALE_DEN=1;
 localparam integer H_ACTIVE=1920,V_ACTIVE=1080,H_TOTAL=2200,V_TOTAL=1125;
 wire amiga_switch_control,amiga_switch_pixel,amiga_quiet_source;
"""
    glue = """    wire pixel_clk=video_clk;
    wire pixel_resetn=video_control_resetn && !amiga_switch_pixel;
    wire mailbox_pixel_run=video_control_resetn;
    reg [2:0] reset_pixel;
    wire pixel_run=reset_pixel[2];
"""
    text = header + pieces["profile_declarations"] + pieces["profile_cdc"]
    text += pieces["geometry_declarations"] + pieces["geometry_tables_and_registers"]
    text += "    wire [11:0] bridge_h_active=" + pieces["bridge_active_width_expression"] + ";\n"
    text += pieces["reader_geometry_declarations"] + glue + pieces["reader_pixel_reset"]
    text += pieces["reader_snapshot"] + "    end endgenerate\nendmodule\n"
    return text, pieces


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build/amiga-profile-cdc-settle")
    args = parser.parse_args()
    out = args.build_dir.resolve()
    if not out.is_relative_to(ROOT / "build"):
        parser.error("Use this repository's build directory")
    out.mkdir(parents=True, exist_ok=True)
    before = {p: digest(ROOT / p) for p in TRACKED}
    generated, pieces = wrapper((ROOT / TOP).read_text(), (ROOT / READER).read_text())
    fixture = out / "production-top-excerpt.sv"
    fixture.write_text(generated)
    host = (ROOT / HOST).read_text()
    if host.count("if(display_timeout==63)") != 1:
        raise RuntimeError("Production post-COMMIT hold is no longer64sourcecycles")
    runtime = Path(os.environ.get("MULTITINI_UCRT_BIN", "C:/msys64/ucrt64/bin"))
    tool = lambda name: str(runtime / (name + ".exe")) if os.name == "nt" else name
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"] if os.name == "nt" else []
    results = []
    queued_both = 0
    synthetic = [fixture]
    for name in ("production", "short-hold-mutation", "physical-width-mutation"):
        mutation = name != "production"
        used_host = ROOT / HOST
        used_fixture = fixture
        if name == "short-hold-mutation":
            used_host = out / "short-hold-host.sv"
            used_host.write_text(host.replace("if(display_timeout==63)", "if(display_timeout==7)"))
            synthetic.append(used_host)
        if name == "physical-width-mutation":
            used_fixture = out / "physical-width-top.sv"
            old = "    wire [11:0] bridge_h_active=" + pieces["bridge_active_width_expression"] + ";"
            used_fixture.write_text(generated.replace(old, "    wire [11:0] bridge_h_active=display_h_active;"))
            synthetic.append(used_fixture)
        executable = out / (name + ".vvp")
        source_args = [str(used_host) if p == HOST else str(ROOT / p) for p in SOURCES]
        commands = [("compile", [tool("iverilog"), "-g2012", "-Wall", "-s", "tb_amiga_profile_cdc_settle", "-o", str(executable), str(used_fixture)] + source_args),
                    ("simulate", [tool("vvp"), str(executable)])]
        for stage, command in commands:
            run = subprocess.run(guard + command, cwd=ROOT, capture_output=True, text=True, timeout=60)
            text = run.stdout + run.stderr
            log = out / (name + "-" + stage + ".log")
            log.write_text(text)
            results.append({"case": name, "stage": stage, "returncode": run.returncode,
                            "command": command, "log": log.name, "sha256": digest(log)})
            if stage == "compile" and run.returncode:
                raise RuntimeError(text)
            if stage == "simulate":
                if mutation:
                    expected = "Profile hold released before virtual bridge width" if name == "physical-width-mutation" else "Profile hold released before coherent geometry"
                    if run.returncode == 0 or expected not in text:
                        raise RuntimeError("Mutation escaped or failed for an unrelated reason: " + text)
                else:
                    if run.returncode or "PASS production top profile CDC/settle:16phasepairs,80commits" not in text:
                        raise RuntimeError(text)
                    queued_both = sum(map(int, re.findall(r"oldboth(\d+)", text)))
                    if queued_both == 0:
                        raise RuntimeError("No commit exercised two queued old profile snapshots")
            print(name, stage, "expected result", flush=True)
    after = {p: digest(ROOT / p) for p in TRACKED}
    if before != after:
        raise RuntimeError("Production input changed during test; rerun with stable sources")
    report = {"passed": True, "board_evidence": False, "source_sha256": before,
              "source_sha256_after": after, "top_excerpt_sha256": {n: hashlib.sha256(v.encode()).hexdigest() for n, v in pieces.items()},
              "synthetic_sha256": {p.relative_to(ROOT).as_posix(): digest(p) for p in synthetic},
              "clock_mhz": {"source": 28.375, "control": 133, "pixel": 25},
              "phase_pairs": 16, "commits": 80, "commit_source_edges": 64, "queued_old_both": queued_both,
              "coverage": ["exact production two2bit drained mailboxes/decode/profile registers", "all3geometry tables/topregisters", "exact four-stage reader geometry snapshot", "exact bridge virtual-active-width expression", "base/ALT/WIDE roundtrips", "old snapshots queued in both domains", "PS reset drains stale profiles; pixel hold leaves mailboxes alive", "short-hold mutation rejected by actual profile/geometry mismatch", "physical-active-width mutation rejected by clipped virtual recapture"],
              "limits": "RTL latency/source equivalence only; physical timing/metastability and complete DDR pipeline are separate gates.",
              "results": results}
    (out / "validation.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS production top profile CDC/settle and short-hold mutation")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
