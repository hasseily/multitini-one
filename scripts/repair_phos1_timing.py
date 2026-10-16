#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Repair an explicit PHO1 routed image, then rerun every normal export gate."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

import package_cpu1_video_test as package

ROOT = package.ROOT


def dvi_replay_candidate_text(source):
    """Reserialize only the original DVI pair, preserving its exact scope."""
    flattened = source.replace("\\\n", " ")
    ports = "[get_ports {dvi_red[*] dvi_grn[*] dvi_blu[*] dvi_de dvi_hsync dvi_vsync}]"
    declarations = [" ".join(line.split()) for line in flattened.splitlines()
                    if line.startswith("set dvi_data_ports ")]
    package.require(declarations == ["set dvi_data_ports " + ports],
                    "Unexpected canonical DVI output-port scope")
    original = [line.strip() for line in source.splitlines()
                if line.startswith("set_output_delay -clock dvi_clk_out ")]
    expected = ["set_output_delay -clock dvi_clk_out -max 1.2 $dvi_data_ports",
                "set_output_delay -clock dvi_clk_out -min -1.3 $dvi_data_ports"]
    package.require(original == expected, "Unexpected canonical DVI output-delay pair")
    candidate = [line.replace("-clock dvi_clk_out",
                            "-clock [get_clocks -quiet -filter {NAME == dvi_clk_out}]")
                 .replace("$dvi_data_ports", ports) for line in original]
    package.require([line.replace("[get_clocks -quiet -filter {NAME == dvi_clk_out}]",
                                  "dvi_clk_out").replace(ports, "$dvi_data_ports")
                     for line in candidate] == original,
                    "DVI replay changed the canonical timing policy")
    return "\n".join(candidate) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--directive", default="AggressiveExplore",
                        choices=("AggressiveExplore", "AlternateReplication", "Explore"))
    parser.add_argument("--vivado", default="C:/Xilinx/2025.2/Vivado/bin/vivado.bat")
    parser.add_argument("--signoff-only", action="store_true")
    parser.add_argument("--iigs-constraint-replay", action="store_true",
                        help="Reserialize the four existing IIgs MCP pairs without changing their timing policy")
    parser.add_argument("--dvi-constraint-replay", action="store_true",
                        help="Reserialize the existing IIgs DVI output-delay pair during signoff only")
    parser.add_argument("--replicate-cell", action="append", default=[])
    parser.add_argument("--replicate-net", action="append", default=[],
                        help="Exact combinational or registered net name to replicate before routing")
    args = parser.parse_args()
    replicate = bool(args.replicate_cell or args.replicate_net)
    package.require(not (args.signoff_only and replicate), "Signoff-only cannot replicate cells or nets")
    package.require(not args.iigs_constraint_replay or args.signoff_only,
                    "IIgs constraint replay requires --signoff-only")
    package.require(not args.dvi_constraint_replay or args.signoff_only,
                    "DVI constraint replay requires --signoff-only")
    package.require(not (args.dvi_constraint_replay and args.iigs_constraint_replay),
                    "DVI and IIgs MCP constraint replays cannot be combined")
    package.require(all(re.fullmatch(r"[A-Za-z0-9_./\[\]]+", name) for name in args.replicate_cell),
                    "Invalid cell name")
    package.require(all(re.fullmatch(r"[A-Za-z0-9_./\[\]]+", name) for name in args.replicate_net),
                    "Invalid net name")
    directory = args.directory.resolve()
    package.require(directory.is_relative_to(ROOT / "build"), "Build must stay inside the checkout")
    record = json.loads((directory / "source-inputs.json").read_text())
    package.require(not args.iigs_constraint_replay or record["machine"] == "APPLE_IIGS",
                    "Constraint replay applies only to the IIgs image")
    package.require(not args.dvi_constraint_replay or record["machine"] == "APPLE_IIGS",
                    "DVI constraint replay applies only to the IIgs image")
    package.verify_sources(record["files"])
    package.verify_hardware_source_archive(directory, record["files"])
    run = directory / "multitini_one.runs/impl_1"
    package.require((run / "multitini_one_top_routed.dcp").is_file(), "Routed checkpoint is absent")
    original = (ROOT / "scripts/build_and_export_xsa.tcl").read_text()
    first = "set script_dir [file dirname [file normalize [info script]]]"
    original = original.replace(first, f"set script_dir {{{(ROOT / 'scripts').as_posix()}}}", 1)
    start = original.index("reset_run synth_1\n")
    end = original.index("open_run impl_1\n", start) + len("open_run impl_1\n")
    attempts = directory / "reports/repair"
    attempts.mkdir(parents=True, exist_ok=True)
    numbers = [int(match.group(1)) for path in attempts.glob("pass-*.tcl")
               if (match := re.fullmatch(r"pass-([0-9]+)\.tcl", path.name))]
    number = max(numbers, default=0) + 1
    replay = ""
    replay_record = None
    dvi_replay_record = None
    if args.iigs_constraint_replay:
        source = ROOT / "hdl/board/multitini_apple_iigs_timing.xdc"
        body = source.read_text().split("# Software eject is sampled", 1)
        package.require(len(body) == 2, "Missing canonical IIgs MCP section")
        body = "# Software eject is sampled" + body[1]
        package.require(body.count("[get_clocks apple_iigs_clk14]") == 8 and
                        len(re.findall(r"^set_multicycle_path", body, re.M)) == 8 and
                        body.count("-setup") == body.count("-hold") == 4,
                        "Unexpected canonical IIgs MCP inventory")
        # The filter is an exact-name query. The only other change resets each
        # old pair before recreating the identical setup and companion hold.
        candidate_text = body.replace("[get_clocks apple_iigs_clk14]",
                                     "[get_clocks -quiet -filter {NAME == apple_iigs_clk14}]")
        candidate_text = candidate_text.replace("-setup", "-setup -reset_path")
        package.require(candidate_text.replace("-setup -reset_path", "-setup").replace(
                        "[get_clocks -quiet -filter {NAME == apple_iigs_clk14}]",
                        "[get_clocks apple_iigs_clk14]") == body,
                        "IIgs replay changed canonical numeric timing policy")
        candidate = attempts / f"pass-{number}-iigs-mcp.xdc"
        candidate.write_text(candidate_text, encoding="utf-8")
        helper = attempts / f"pass-{number}-replay-source.tcl"
        shutil.copyfile(ROOT / "scripts/repair_apple_iigs_constraint_replay.tcl", helper)
        tool_source = attempts / f"pass-{number}-tool-source.py"
        shutil.copyfile(Path(__file__), tool_source)
        equivalence = attempts / f"pass-{number}-replay-equivalence.txt"
        replay = (f"source {{{helper.as_posix()}}}\n"
                  f"::iigs_replay::begin {{{candidate.as_posix()}}} {{{equivalence.as_posix()}}}\n")
        gate = "\nforeach delay_type {max min} {\n"
        package.require(original.count(gate) == 1, "Cannot identify final timing gate")
        original = original.replace(gate,
            "\n::iigs_replay::check after_all_reports\n"
            "source [file join $script_dir check_apple_iigs_phase_constraints.tcl]\n"
            "report_timing_summary -delay_type min_max -max_paths 100 -check_timing_verbose "
            "-report_unconstrained -file [file join $report_dir timing_summary.rpt]\n"
            "::iigs_replay::check after_final_timing_summary\n" + gate, 1)
        original += "\n::iigs_replay::finish\n"
        replay_record = {"selector_reserialization": True, "numeric_timing_policy_changed": False,
                         "original_mcp_section_sha256": hashlib.sha256(body.encode()).hexdigest(),
                         "candidate": {"path": str(candidate), "sha256": package.sha(candidate)},
                         "helper": {"path": str(helper), "sha256": package.sha(helper)},
                         "tool_source": {"path": str(tool_source), "sha256": package.sha(tool_source)},
                         "equivalence_report": str(equivalence)}
    if args.dvi_constraint_replay:
        source = ROOT / "hdl/board/multitini_timing.xdc"
        candidate = attempts / f"pass-{number}-dvi-output-delay.xdc"
        candidate.write_text(dvi_replay_candidate_text(source.read_text()), encoding="utf-8")
        helper = attempts / f"pass-{number}-dvi-replay-source.tcl"
        shutil.copyfile(ROOT / "scripts/repair_dvi_constraint_replay.tcl", helper)
        tool_source = attempts / f"pass-{number}-tool-source.py"
        shutil.copyfile(Path(__file__), tool_source)
        equivalence = attempts / f"pass-{number}-dvi-replay-equivalence.txt"
        replay = (f"source {{{helper.as_posix()}}}\n"
                  f"::dvi_replay::begin {{{candidate.as_posix()}}} {{{equivalence.as_posix()}}}\n")
        gate = "\nforeach delay_type {max min} {\n"
        package.require(original.count(gate) == 1, "Cannot identify final timing gate")
        original = original.replace(gate,
            "\n::dvi_replay::check after_all_reports\n"
            "source [file join $script_dir check_apple_iigs_phase_constraints.tcl]\n"
            "report_timing_summary -delay_type min_max -max_paths 100 -check_timing_verbose "
            "-report_unconstrained -file [file join $report_dir timing_summary.rpt]\n"
            "::dvi_replay::check after_final_timing_summary\n" + gate, 1)
        original += "\n::dvi_replay::finish\n"
        dvi_replay_record = {"selector_reserialization": True,
                            "numeric_timing_policy_changed": False,
                            "scope": "19 DVI output ports, max 1.2 ns and min -1.3 ns, original generated clock",
                            "source": {"path": str(source), "sha256": package.sha(source)},
                            "candidate": {"path": str(candidate), "sha256": package.sha(candidate)},
                            "helper": {"path": str(helper), "sha256": package.sha(helper)},
                            "tool_source": {"path": str(tool_source), "sha256": package.sha(tool_source)},
                            "equivalence_report": str(equivalence)}
    replication = ""
    if replicate:
        replication = ('set repair_clock [get_clocks -quiet -filter {NAME =~ *clk_out1*zynq_ps_bd_clk_wiz_c64*}]\n'
            'require_one_object "repair machine clock" $repair_clock\n'
            'set repair_path [get_timing_paths -quiet -delay_type max -from $repair_clock -to $repair_clock -max_paths 1]\n'
            'require_one_object "repair machine path" $repair_path\n'
            'set repair_uncertainty [get_property USER_UNCERTAINTY $repair_path]\n'
            'if {$repair_uncertainty ne "" && abs(double($repair_uncertainty)) > 0.0005} {error "Expected nominal machine uncertainty"}\n'
            'set_clock_uncertainty -setup 0.100 $repair_clock\n'
            'route_design -unroute\n')
    for name in args.replicate_cell:
        replication += (f'set repair_cell [get_cells -hierarchical -quiet -filter {{NAME == "{name}"}}]\n'
            'require_one_object "replication source cell" $repair_cell\n'
            'set repair_pin [get_pins -quiet -of_objects $repair_cell -filter {DIRECTION == OUT && REF_PIN_NAME == Q}]\n'
            'require_one_object "replication source Q pin" $repair_pin\n'
            'set repair_net [get_nets -quiet -of_objects $repair_pin]\n'
            'require_one_object "replication source net" $repair_net\n'
            'phys_opt_design -force_replication_on_nets $repair_net\n')
    for name in args.replicate_net:
        replication += (f'set repair_net [get_nets -hierarchical -quiet -filter {{NAME == "{name}"}}]\n'
            'require_one_object "replication source net" $repair_net\n'
            'phys_opt_design -force_replication_on_nets $repair_net\n')
    optimization = "" if args.signoff_only else f"phys_opt_design -directive {args.directive}\n"
    if replicate:
        optimization = ('phys_opt_design -directive Explore\n'
            'route_design -directive Explore\n' + optimization +
            'set_clock_uncertainty -setup 0.000 $repair_clock\n')
    repair = ("open_run impl_1\n" + replication + optimization + replay +
              f"write_checkpoint -force {{{(run / 'multitini_one_top_routed.dcp').as_posix()}}}\n"
              f"write_bitstream -force {{{(run / 'multitini_one_top.bit').as_posix()}}}\n")
    script = original[:start] + repair + original[end:]
    # Keep the original generation/build wrappers and their pre-build archive immutable.
    prefix = (directory / "generate.tcl").read_text().split("source {", 1)[0]
    tcl = attempts / f"pass-{number}.tcl"
    tcl.write_text(prefix + script, encoding="utf-8")
    failure = attempts / f"before-{number}"
    failure.mkdir()
    checkpoints = directory / "repair-checkpoints"
    checkpoints.mkdir(exist_ok=True)
    checkpoint = checkpoints / f"before-{number}.dcp"
    shutil.copyfile(run / "multitini_one_top_routed.dcp", checkpoint)
    for name in ("build.log", "build-console.log"):
        shutil.copyfile(directory / name, failure / name)
    for name in ("timing_summary.rpt", "route_status.rpt", "cdc.rpt"):
        path = directory / "reports" / name
        if path.is_file(): shutil.copyfile(path, failure / name)
    log = attempts / f"pass-{number}.log"
    command = [sys.executable, ROOT / "scripts/run_native_tool.py", "--", args.vivado,
               "-mode", "batch", "-source", tcl, "-log", log,
               "-journal", attempts / f"pass-{number}.jou"]
    console = attempts / f"pass-{number}-console.log"
    with console.open("w", encoding="utf-8") as stream:
        result = subprocess.run(list(map(str, command)), cwd=directory,
                                stdout=stream, stderr=subprocess.STDOUT)
    if dvi_replay_record is not None:
        dvi_replay_record["equivalence"] = (
            {"path": str(equivalence), "sha256": package.sha(equivalence)}
            if equivalence.is_file() else None)
    package.write_json(attempts / f"pass-{number}.json", {
        "directive": None if args.signoff_only else args.directive,
        "replicated_cells": args.replicate_cell,
        "replicated_nets": args.replicate_net,
        "full_reroute": replicate,
        "temporary_machine_setup_margin_ns": 0.100 if replicate else 0.000,
        "constraints_changed": bool(replay_record or dvi_replay_record), "rtl_changed": False,
        "iigs_constraint_replay": replay_record,
        "dvi_constraint_replay": dvi_replay_record,
        "normal_signoff_script_sha256": hashlib.sha256((ROOT / "scripts/build_and_export_xsa.tcl").read_bytes()).hexdigest(),
        "generated_script_sha256": package.sha(tcl), "command": list(map(str, command)),
        "routed_checkpoint_before_sha256": package.sha(checkpoint),
        "routed_checkpoint_after_sha256": package.sha(run / "multitini_one_top_routed.dcp"),
        "returncode": result.returncode})
    if result.returncode:
        raise RuntimeError("Timing repair/export failed; see " + str(console))
    package.verify_sources(record["files"])
    # The auditor requires the log of the selected, timing-gated export.
    shutil.copyfile(log, directory / "build.log")
    shutil.copyfile(console, directory / "build-console.log")
    print("PASS repaired and timing-gated: " + str(directory))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
