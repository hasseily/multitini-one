#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Record controlled phosphor resource comparisons and unchanged simulated deadlines."""
import argparse
import hashlib
import json
from pathlib import Path
import re

import package_cpu1_video_test as package
from build_phos1 import BUILD_ID, MATRIX

ROOT = package.ROOT


def read_log(path):
    data = path.read_bytes()
    return data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8")


def utilization(path):
    text = path.read_text()
    hierarchy = re.search(r"^\| multitini_one_top\s*\|\s*\(top\)\s*\|.*$", text, re.MULTILINE)
    if hierarchy:
        row = [column.strip() for column in hierarchy[0].strip("|").split("|")]
        package.require(len(row) == 10, "Unexpected hierarchy utilization columns")
        return {"LUT": int(row[2]), "FF": int(row[6]),
                "BRAM": int(row[7]) + int(row[8]) / 2, "DSP": int(row[9])}
    labels = {"LUT": "Slice LUTs*", "FF": "Slice Registers", "BRAM": "Block RAM Tile", "DSP": "DSPs"}
    result = {}
    for name, label in labels.items():
        match = re.search(r"^\| " + re.escape(label) + r"\s*\|\s*([\d.]+)\s*\|", text, re.MULTILINE)
        package.require(match, "Missing utilization metric: " + label)
        result[name] = float(match[1]) if "." in match[1] else int(match[1])
    return result


def engine_cases(path):
    pattern = r"PASS frame engine oracle/deadline: (\d+x\d+) jobs=(\d+) pixels=(\d+) cycles=(\d+) max_stall=(\d+) bound=(\d+)"
    rows = re.findall(pattern, read_log(path))
    package.require(len(rows) == 16, "Incomplete frame-engine run")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--hardware", type=Path, required=True)
    parser.add_argument("--supervisor-dir", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "build/phos1/performance")
    parser.add_argument("--build-id", default=BUILD_ID, help="Eight hexadecimal digits identifying every image")
    parser.add_argument("--baseline-engine-log", type=Path, help="Baseline frame-engine log; default <output>/baseline-engine.log")
    parser.add_argument("--engine-log", type=Path, default=ROOT / "build/phos1/engine.log")
    parser.add_argument("--baseline-label", default="GL01 working-tree source captured before the PHO1 port")
    parser.add_argument("--manual-clipping", action="store_true", help="Include the PHO3 manual scanout contract and require all reader probes")
    parser.add_argument("--baseline-validation", type=Path,
                        help="Optional baseline validation JSON; default <output>/baseline-validation.json if present")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9A-Fa-f]{8}", args.build_id):
        parser.error("--build-id must contain exactly eight hexadecimal digits")
    args.build_id = args.build_id.upper()
    out = args.output.resolve()
    package.require(out.is_relative_to(ROOT / "build"), "Output must stay under build/")
    baseline_log = (args.baseline_engine_log or out / "baseline-engine.log").resolve()
    engine_log = args.engine_log.resolve()
    baseline_validation = (args.baseline_validation or out / "baseline-validation.json").resolve()
    if args.baseline_validation:
        package.require(baseline_validation.is_file(), "Missing baseline validation: " + str(baseline_validation))
    identity = bytes.fromhex(args.build_id).decode("ascii", errors="replace")
    if not re.fullmatch(r"[A-Z0-9]+", identity):
        identity = "0x" + args.build_id
    before = engine_cases(baseline_log)
    after = engine_cases(engine_log)
    package.require(before == after, "Frame-engine cycle or stall behavior changed")
    probes = {}
    for block in ("c64_engine", "gbc_engine", "vectrex_engine", "pixel_effects",
                  "c64_reader", "gbc_reader", "vectrex_reader"):
        reports = {version: out / "blocks" / version / block / "utilization.rpt"
                   for version in ("before", "after")}
        if block not in ("c64_engine", "gbc_engine", "vectrex_engine") and not any(path.exists() for path in reports.values()):
            package.require(not (args.manual_clipping and block.endswith("_reader")), "Missing manual reader probe: " + block)
            continue
        package.require(all(path.is_file() for path in reports.values()), "Incomplete resource comparison: " + block)
        values = {version: utilization(path) for version, path in reports.items()}
        values["delta"] = {key: values["after"][key] - values["before"][key] for key in values["after"]}
        probes[block] = values
    matrix = {}
    for name, (machine, _, pal) in MATRIX.items():
        directory = args.supervisor_dir if name == "supervisor" and args.supervisor_dir else args.hardware / name
        image = package.verify_hardware(directory.resolve(), machine, build_id=args.build_id, genesis_pal=pal)
        matrix[name] = {"timing": image["timing"], "utilization": utilization(directory / "reports/utilization.rpt")}
    frames = [{"geometry": current[0], "cycles_before": int(previous[3]), "cycles_after": int(current[3]),
               "delta": 0, "milliseconds_at_133MHz": round(int(current[3]) / 133000, 4),
               "bounded_stall_clocks": int(current[4]), "frame_budget_clocks": int(current[5])}
              for previous, current in zip(before[-5:], after[-5:])]
    evidence = [out / "probe.tcl", out / "probe.log", baseline_log, engine_log]
    if baseline_validation.is_file():
        evidence.append(baseline_validation)
    evidence += sorted((out / "blocks").rglob("utilization.rpt"))
    evidence += sorted(p for p in (out.parent / "baseline").rglob("*") if p.is_file())
    evidence += [ROOT / "hdl/common" / name for name in
                 ("multitini_video_frame_filter.sv", "multitini_video_effects.sv",
                  "multitini_video_frame_engine.sv", "multitini_video_frame_reader.sv",
                  "multitini_async_mailbox.sv")]
    record = {"status": "PASS", "build_id": "0x" + args.build_id, "reference": args.baseline_label,
              "method": "Vivado 2025.2 out-of-context synthesis, same tops/generics/part, two threads",
              "limits": "Block resource deltas exclude whole-core placement; reader probes are reported when supplied. Full-image counts are reported separately. Simulated bounded DDR stalls are not measured board throughput. CPU reference-filter work, including additional history reads, is not benchmarked; production CPU1 prepares color tables while the FPGA applies spatial effects.",
              "clock_delta": 0, "added_pipeline_stages": 0, "added_frame_transfers": 0,
              "added_scanout_ddr_transactions_auto": 0, "engine_cases": len(after),
              "blocks": probes, "frames": frames, "matrix": matrix,
              "evidence_sha256": {(p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else p.as_posix()):
                                  hashlib.sha256(p.read_bytes()).hexdigest() for p in evidence}}
    if args.manual_clipping:
        record["manual_scanout"] = "Clipping starts reads at the first visible source word minus one neighbor word; native frame capture/filtering remains complete. Manual geometry changes which source rows/words are read. No enlarged output framebuffer is added."
    package.write_json(out / "performance.json", record)
    lines = [f"# {identity} performance results", "", "No additional native frame-engine clocks, filter pipeline stages or frame transfers. Auto retains its scanout transaction pattern.", "",
             f"Controlled Vivado 2025.2 synthesis against {args.baseline_label}:", "",
             "| Block | LUT before/after | FF before/after | BRAM before/after | DSP before/after |", "|---|---:|---:|---:|---:|"]
    for name, row in probes.items():
        lines.append("| " + name + " | " + " | ".join(f"{row['before'][key]}/{row['after'][key]}" for key in ("LUT", "FF", "BRAM", "DSP")) + " |")
    lines += ["", "These are block comparisons; they exclude whole-core placement. Optional reader probes cover scanout logic.", "",
              "| Source frame | Before/after clocks | Time at 133 MHz |", "|---|---:|---:|"]
    if args.manual_clipping:
        table_index = lines.index("| Source frame | Before/after clocks | Time at 133 MHz |")
        lines[table_index:table_index] = [record["manual_scanout"], ""]
    for row in frames:
        lines.append(f"| {row['geometry']} | {row['cycles_before']}/{row['cycles_after']} | {row['milliseconds_at_133MHz']} ms |")
    stall_bound = max(int(row[4]) for row in after)
    budgets = sorted({int(row[5]) for row in after})
    budget_text = ", ".join(f"{budget:,} clocks ({budget / 133000:.2f} ms)" for budget in budgets)
    lines += ["", f"The same simulation bounded stalls at {stall_bound} clocks; the frame budget is {budget_text}.", "",
              "| Full image | WNS | WHS | WPWS | LUT | FF | BRAM | DSP |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for name, row in matrix.items():
        slack = row["timing"]["timing_ns"]
        lines.append("| " + name + " | " + " | ".join(f"+{slack[k]:.3f} ns" for k in ("WNS", "WHS", "WPWS")) + " | " +
                     " | ".join(str(row["utilization"][k]) for k in ("LUT", "FF", "BRAM", "DSP")) + " |")
    lines += ["", "Every image passed the normal setup, hold, pulse-width, routing, CDC, bus-skew, and timing-coverage gates.", "",
              "CPU reference-filter work, including additional history reads, is not benchmarked here.", "",
              "Board throughput, live HDMI behavior, and gameplay remain untested. CPU1 prepares color tables; the FPGA applies the spatial effects.", ""]
    (out / "PERFORMANCE.md").write_text("\n".join(lines), encoding="utf-8")
    print(json.dumps({"status": "PASS", "frame_cycle_delta": 0, "blocks": probes}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
