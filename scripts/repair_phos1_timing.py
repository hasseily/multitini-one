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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--directive", default="AggressiveExplore",
                        choices=("AggressiveExplore", "AlternateReplication", "Explore"))
    parser.add_argument("--vivado", default="C:/Xilinx/2025.2/Vivado/bin/vivado.bat")
    parser.add_argument("--signoff-only", action="store_true")
    parser.add_argument("--replicate-cell", action="append", default=[])
    parser.add_argument("--replicate-net", action="append", default=[],
                        help="Exact combinational or registered net name to replicate before routing")
    args = parser.parse_args()
    replicate = bool(args.replicate_cell or args.replicate_net)
    package.require(not (args.signoff_only and replicate), "Signoff-only cannot replicate cells or nets")
    package.require(all(re.fullmatch(r"[A-Za-z0-9_./\[\]]+", name) for name in args.replicate_cell),
                    "Invalid cell name")
    package.require(all(re.fullmatch(r"[A-Za-z0-9_./\[\]]+", name) for name in args.replicate_net),
                    "Invalid net name")
    directory = args.directory.resolve()
    package.require(directory.is_relative_to(ROOT / "build"), "Build must stay inside the checkout")
    record = json.loads((directory / "source-inputs.json").read_text())
    package.verify_sources(record["files"])
    package.verify_hardware_source_archive(directory, record["files"])
    run = directory / "multitini_one.runs/impl_1"
    package.require((run / "multitini_one_top_routed.dcp").is_file(), "Routed checkpoint is absent")
    original = (ROOT / "scripts/build_and_export_xsa.tcl").read_text()
    first = "set script_dir [file dirname [file normalize [info script]]]"
    original = original.replace(first, f"set script_dir {{{(ROOT / 'scripts').as_posix()}}}", 1)
    start = original.index("reset_run synth_1\n")
    end = original.index("open_run impl_1\n", start) + len("open_run impl_1\n")
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
    repair = ("open_run impl_1\n" + replication + optimization +
              f"write_checkpoint -force {{{(run / 'multitini_one_top_routed.dcp').as_posix()}}}\n"
              f"write_bitstream -force {{{(run / 'multitini_one_top.bit').as_posix()}}}\n")
    script = original[:start] + repair + original[end:]
    # Keep the original generation/build wrappers and their pre-build archive immutable.
    prefix = (directory / "generate.tcl").read_text().split("source {", 1)[0]
    attempts = directory / "reports/repair"
    attempts.mkdir(parents=True, exist_ok=True)
    number = len(list(attempts.glob("pass-*.tcl"))) + 1
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
    package.write_json(attempts / f"pass-{number}.json", {
        "directive": None if args.signoff_only else args.directive,
        "replicated_cells": args.replicate_cell,
        "replicated_nets": args.replicate_net,
        "full_reroute": replicate,
        "temporary_machine_setup_margin_ns": 0.100 if replicate else 0.000,
        "constraints_changed": False, "rtl_changed": False,
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
