#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Qualify an unchanged routed NES image after the reviewed clock-check correction.

The original synthesis archives remain immutable. This tool permits only the
exact original clock-check block to become the reviewed structural check, then
replays the normal export gates without changing the checkpoint or bitstream.
"""
from pathlib import Path
import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import zipfile

import capture_vectrex_build_inputs as capture
import package_cpu1_video_test as package

ROOT = Path(__file__).resolve().parents[1]
CHECKER = "scripts/build_and_export_xsa.tcl"
CDC_CHECKER = "scripts/check_video_bus_constraints.tcl"
START = "    set nes_period [get_property PERIOD $nes_clock]\n"
END = "    set nes_register_pins "
OLD_GUARD = '''    set nes_period [get_property PERIOD $nes_clock]
    if {abs($nes_period - 1000.0 / 42.954544) > 0.001} {
        error "NES service clock differs from the reviewed 42.954544 MHz root: $nes_period ns"
    }
'''
NEW_GUARD_SHA256 = "d1b785b458c64a7d01db9418b9d9cc38c6b3805e486bca8b221d2268967247bc"


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def describe(path):
    data = Path(path).read_bytes()
    return {"bytes": len(data), "sha256": digest(data)}


def checker_delta(original, current):
    old = original.decode("utf-8").replace("\r\n", "\n")
    new = current.decode("utf-8").replace("\r\n", "\n")
    require(old.count(START) == new.count(START) == 1, "Ambiguous NES clock guard")
    a, b = old.index(START), new.index(START)
    x, y = old.index(END, a), new.index(END, b)
    require(old[a:x] == OLD_GUARD, "Original NES guard is not the known failed check")
    require(digest(new[b:y].encode()) == NEW_GUARD_SHA256,
            "Current NES clock guard is not the reviewed structural check")
    require(old[:a] == new[:b] and old[x:] == new[y:],
            "NES export source changed outside the reviewed clock guard")
    return {"original_checker_sha256": digest(original), "current_checker_sha256": digest(current),
            "original_guard_sha256": digest(old[a:x].encode()),
            "corrected_guard_sha256": NEW_GUARD_SHA256}


def cdc_checker_delta(original, current):
    old = original.decode("utf-8").replace("\r\n", "\n")
    new = current.decode("utf-8").replace("\r\n", "\n")
    replacements = (("([0-5])", "([0-6])"),
                    ("Genesis and IIgs capture", "Genesis, IIgs and NES capture"),
                    ("$active_machine in {4 5}", "$active_machine in {4 5 6}"))
    expected = old
    for before, after in replacements:
        require(expected.count(before) == 1, "Unknown original CDC census")
        expected = expected.replace(before, after, 1)
    require(new == expected, "CDC checker changed beyond the reviewed NES enum/FIFO census")
    return {"original_sha256": digest(original), "current_sha256": digest(current),
            "changes": "Accept machine6 and require its four eight-bit FIFO pointer groups; timing bounds unchanged"}


def source_delta(directory):
    record = json.loads((directory / "source-inputs.json").read_text())
    require(record["machine"] == "NES", "Clock revalidation applies only to NES")
    package.verify_hardware_source_archive(directory, record["files"])
    current = {p.relative_to(ROOT).as_posix() for p in capture.inputs("NES")}
    require(current == set(record["files"]), "NES FPGA input closure changed")
    changed = [name for name, expected in record["files"].items()
               if describe(ROOT / name)["sha256"] != expected]
    require(changed == [CHECKER, CDC_CHECKER], "Only the two reviewed NES checkers may differ: " + str(changed))
    with zipfile.ZipFile(directory / "source-inputs.zip") as archive:
        original = archive.read(CHECKER)
        cdc_original = archive.read(CDC_CHECKER)
    delta = checker_delta(original, (ROOT / CHECKER).read_bytes())
    delta["cdc_census"] = cdc_checker_delta(cdc_original, (ROOT / CDC_CHECKER).read_bytes())
    return record, delta


def pinned_files(directory, records):
    for name, expected in records.items():
        path = (directory / name).resolve()
        require(path.is_relative_to(directory.resolve()), "Unsafe signoff receipt path")
        require(describe(path) == expected, "NES signoff receipt file changed: " + name)


def validate_receipt(directory):
    directory = Path(directory).resolve()
    _, delta = source_delta(directory)
    receipt = json.loads((directory / "reports/nes-export-signoff/receipt.json").read_text())
    require(receipt.get("schema") == 1 and receipt.get("passed") is True and
            receipt.get("machine") == "NES" and receipt.get("checker_delta") == delta,
            "NES export receipt does not match the reviewed source delta")
    require(receipt["tool_source_sha256"] == describe(Path(__file__))["sha256"],
            "NES export verifier source changed")
    require(receipt.get("rtl_changed") is False and receipt.get("constraints_changed") is False and
            receipt.get("routed_design_changed") is False, "NES export receipt permits a design change")
    for name, pair in receipt["unchanged_artifacts"].items():
        require(pair["before"] == pair["after"] == describe(directory / name),
                "NES routed artifact differs from its export receipt: " + name)
    pinned_files(directory, receipt["files"])
    bit = directory / "multitini_one.runs/impl_1/multitini_one_top.bit"
    with zipfile.ZipFile(directory / "multitini_one.xsa") as archive:
        bits = [name for name in archive.namelist() if name.endswith(".bit")]
        require(len(bits) == 1 and digest(archive.read(bits[0])) == describe(bit)["sha256"],
                "NES XSA differs from the original routed bitstream")
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--vivado", default="C:/Xilinx/2025.2/Vivado/bin/vivado.bat")
    args = parser.parse_args()
    directory = args.directory.resolve()
    require(directory.is_relative_to(ROOT / "build"), "Build must remain in the checkout")
    if args.verify:
        validate_receipt(directory)
        print("PASS NES checker-only export receipt and immutable routed artifacts")
        return 0
    record, delta = source_delta(directory)
    parent = directory / "reports/nes-export-signoff"
    parent.mkdir(parents=True, exist_ok=True)
    attempt = parent / ("pass-" + str(len(list(parent.glob("pass-*"))) + 1))
    attempt.mkdir()
    original_console = (directory / "build-console.log").read_text(errors="replace")
    require("synth_1 finished" in original_console and "impl_1 finished" in original_console and
            "NES_SAVE_RAM_PASS stage=synthesis" in original_console,
            "Original synthesis/implementation completion evidence is absent")
    match = re.search(r"Captured 130 NES build inputs in ([^\r\n]+)", original_console)
    require(match is not None, "Original Vivado capture path is absent")
    consumed = Path(match[1].strip())
    consumed_record = json.loads(consumed.read_text())
    require(all(consumed_record[k] == record[k] for k in ("machine", "configuration", "files")),
            "Original Vivado capture differs from the original build snapshot")
    for source in (consumed, consumed.parent / "build_inputs.sources.tar.gz"):
        shutil.copy2(source, directory / "reports" / source.name)
    for name in ("build.log", "build-console.log"):
        shutil.copy2(directory / name, attempt / ("original-" + name))
    shutil.copy2(ROOT / CHECKER, attempt / "corrected-checker.tcl")
    shutil.copy2(ROOT / CDC_CHECKER, attempt / "corrected-cdc-checker.tcl")
    shutil.copy2(Path(__file__), attempt / "export-tool.py")
    unchanged = {name: {"before": describe(directory / name)} for name in (
        "multitini_one.runs/impl_1/multitini_one_top.bit",
        "multitini_one.runs/impl_1/multitini_one_top_routed.dcp")}
    text = (ROOT / CHECKER).read_text()
    text = text.replace("set script_dir [file dirname [file normalize [info script]]]",
                        "set script_dir {" + (ROOT / "scripts").as_posix() + "}", 1)
    # Existing completion is proved by the original log and immutable artifacts.
    # Reapplying run metadata would merely mark those completed runs out-of-date.
    text = text.replace("source [file join $script_dir configure_vivado_run_profile.tcl]",
                        "source [file join $script_dir vivado_run_threads.tcl]", 1)
    a = text.index("# Capture dirty source bytes")
    b = text.index("open_run impl_1\n", a) + len("open_run impl_1\n")
    text = text[:a] + "open_run impl_1\n" + text[b:]
    a = text.index("puts [exec {*}$python [file join $script_dir capture_vectrex_build_inputs.py] --verify")
    b = text.index("write_hw_platform", a)
    text = text[:a] + "# Original synthesis-input records are preserved by the export driver.\n" + text[b:]
    require(not re.search(r"^\s*(?:reset_run|launch_runs|phys_opt_design|route_design|write_bitstream|write_checkpoint|read_xdc|set_clock_uncertainty|set_false_path|set_multicycle_path|create_clock)\b", text, re.M),
            "Export-only script contains a design-changing command")
    prefix = (directory / "generate.tcl").read_text().split("source {", 1)[0]
    script = attempt / "export-only.tcl"
    script.write_text(prefix + text)
    log, console = attempt / "export.log", attempt / "export-console.log"
    command = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--", args.vivado,
               "-mode", "batch", "-source", str(script), "-log", str(log),
               "-journal", str(attempt / "export.jou")]
    with console.open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, cwd=directory, stdout=stream, stderr=subprocess.STDOUT)
    for name, pair in unchanged.items():
        pair["after"] = describe(directory / name)
        require(pair["before"] == pair["after"], "Export changed the routed artifact: " + name)
    require(result.returncode == 0, "NES export-only verification failed: " + str(console))
    source_delta(directory)
    output = log.read_text(errors="replace")
    for marker in ("NES_CLOCK_RATIO_PASS", "NES_CLOCK_PASS", "NES_SAVE_RAM_PASS stage=implementation",
                   "Timing gates passed and XSA exported:"):
        require(marker in output, "NES export log lacks " + marker)
    clock_match = re.search(r"^NES_CLOCK_RATIO_PASS parent=(\S+) parent_period=([0-9.]+) M=31.5 D=5 O=22 derived_period=([0-9.]+) actual_period=([0-9.]+)$", output, re.M)
    require(clock_match is not None, "NES live clock receipt is absent")
    parent_period, derived_period, actual_period = map(float, clock_match.groups()[1:])
    clock = {"parent": clock_match[1], "parent_period_ns": parent_period, "M": 31.5, "D": 5, "O": 22,
             "expression": "parent_period_ns * 5 * 22 / 31.5", "derived_period_ns": derived_period,
             "actual_period_ns": actual_period, "tolerance_ns": 0.001}
    require(abs(parent_period * 5 * 22 / 31.5 - derived_period) < 1e-9 and
            abs(actual_period - derived_period) <= 0.001, "NES live MMCM ratio is inconsistent")
    for name in ("build.log", "build-console.log"):
        shutil.copy2(log if name == "build.log" else console, directory / name)
    files = {p.relative_to(directory).as_posix(): describe(p) for p in attempt.iterdir() if p.is_file()}
    for name in ("source-inputs.json", "source-inputs.zip", "source-inputs.sources.tar.gz",
                 "reports/build_inputs.json", "reports/build_inputs.sources.tar.gz", "multitini_one.xsa"):
        files[name] = describe(directory / name)
    receipt = {"schema": 1, "passed": True, "machine": "NES", "checker_delta": delta,
        "rtl_changed": False, "constraints_changed": False, "routed_design_changed": False,
        "tool_source_sha256": describe(Path(__file__))["sha256"], "command": command,
        "unchanged_artifacts": unchanged, "files": files, "clock": clock}
    (parent / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    validate_receipt(directory)
    print("PASS NES export-only verification; original source archives, bitstream and routed checkpoint preserved")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
