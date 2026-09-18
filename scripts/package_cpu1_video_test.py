#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package explicit VID4/ABI 0.15 builds after source and timing signoff.

No discovery or fallback selects an FPGA image, FSBL, platform, or application.
Use --check-only to audit an already completed matrix without packaging it.
The output is a local test bundle; this script never flashes hardware.
"""
from __future__ import annotations

import argparse
import binascii
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

import audit_vectrex_package as audit
import capture_vectrex_build_inputs as hardware_inputs

ROOT = Path(__file__).resolve().parents[1]
BUILD_ID = "56494434"
ABI = "0.15"
GUARD = [sys.executable, "-u", str(ROOT / "scripts/run_native_tool.py"), "--"]
REQUIRED_REPORTS = {
    "timing_summary.rpt", "bus_skew.rpt", "cdc.rpt", "route_status.rpt",
    "timing_coverage_gate.rpt", "drc.rpt", "utilization.rpt", "build_manifest.txt",
    "clocks.rpt", "exceptions.rpt", "check_timing.rpt", "methodology.rpt",
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def sha(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def describe(path):
    return {"bytes": path.stat().st_size, "sha256": sha(path)}


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def copy(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)


def run(command, *, vitis=None):
    environment = dict(os.environ)
    if vitis:
        environment["XILINX_VITIS"] = str(vitis)
    subprocess.run(GUARD + list(map(str, command)), cwd=ROOT,
                   env=environment, check=True)


def git(*arguments):
    return subprocess.check_output(GUARD + ["git", *arguments], cwd=ROOT, text=True)


def source_path(name):
    clean = PurePosixPath(name)
    require(not clean.is_absolute() and ".." not in clean.parts and "\\" not in name,
            "Unsafe source path: " + name)
    path = (ROOT / name).resolve()
    require(path.is_relative_to(ROOT) and path.is_file(), "Missing source: " + name)
    require(clean.parts[0] not in {"build", ".git", ".codex", ".agents"} and
            not clean.parts[0].startswith("build-"),
            "Generated/private path in source inventory: " + name)
    return path


def verify_sources(files):
    require(files, "Empty source inventory")
    for name, expected in files.items():
        require(sha(source_path(name)) == expected, "Build source changed: " + name)


def timing(directory, machine, psram):
    reports = directory / "reports"
    require(REQUIRED_REPORTS <= {p.name for p in reports.iterdir()}, "Missing timing signoff reports")
    lines = (reports / "timing_summary.rpt").read_text().splitlines()
    header = next((i for i, line in enumerate(lines) if line.strip().startswith("WNS(ns)")), None)
    require(header is not None and header + 2 < len(lines), "No Vivado timing summary")
    values = lines[header + 2].split()
    require(len(values) == 12, "Unexpected timing-summary columns")
    slacks = {name: float(values[index]) for name, index in (("WNS", 0), ("WHS", 4), ("WPWS", 8))}
    require(all(math.isfinite(value) and value >= 0 for value in slacks.values()) and
            slacks["WNS"] > 0 and
            all(int(values[index]) == 0 for index in (2, 6, 10)) and
            all(float(values[index]) == 0 for index in (1, 5, 9)) and
            all(int(values[index]) > 0 for index in (3, 7, 11)),
            "Setup, hold, pulse-width, or endpoint gate failed")
    skew = (reports / "bus_skew.rpt").read_text()
    require("VIOLATED" not in skew, "Bus-skew violation")
    sections = re.split(r"^Id:\s*\d+\s*$", skew, flags=re.MULTILINE)[1:]
    require(sections, "No bus-skew constraints reported")
    skew_slacks = []
    for section in sections:
        matches = re.findall(r"^Slack \(MET\)\s*:\s*([-+\d.]+)ns", section, re.MULTILINE)
        if not matches:
            require(re.search(r"^Endpoints:\s*1\s*$", section, re.MULTILINE) and
                    "No bus skew paths found." in section, "Unreported bus-skew result")
        else:
            require(len(matches) == 1, "Ambiguous bus-skew result")
            skew_slacks.append(float(matches[0]))
    require(skew_slacks and all(math.isfinite(s) and s >= 0 for s in skew_slacks), "Bad bus-skew slack")
    coverage = (reports / "timing_coverage_gate.rpt").read_text()
    categories = ("no_clock", "unconstrained_internal_endpoints", "no_input_delay",
                  "no_output_delay", "partial_input_delay", "partial_output_delay")
    require(all("checking " + item in coverage for item in categories), "Incomplete timing coverage report")
    failures = (
        r"There (?:are|is) [1-9]\d* register/latch pins with no clock",
        r"There (?:are|is) [1-9]\d* pins that are not constrained for maximum delay",
        r"There (?:are|is) [1-9]\d* input ports with no input delay specified",
        r"There (?:are|is) [1-9]\d* ports with no output delay specified\. \(HIGH\)",
        r"There (?:are|is) [1-9]\d* input ports with partial input delay specified",
        r"There (?:are|is) [1-9]\d* ports with partial output delay specified")
    require(not any(re.search(p, coverage) for p in failures), "Unconstrained timing objects")
    route = (reports / "route_status.rpt").read_text()
    count = lambda label: re.search(re.escape(label) + r"\.+\s*:\s*(\d+)\s*:", route)
    logical, full, errors = (count(label) for label in
        ("# of routable nets", "# of fully routed nets", "# of nets with routing errors"))
    require(logical and full and errors and int(logical[1]) == int(full[1]) > 0 and int(errors[1]) == 0,
            "Incomplete or erroneous routing")
    cdc = (reports / "cdc.rpt").read_text()
    require("CDC Report" in cdc and re.search(r"^CDC-\d+\s+(?:Info|Warning|Critical)\s+\d+", cdc, re.MULTILINE),
            "Incomplete CDC report")
    summary = sum(map(int, re.findall(r"^CDC-\d+\s+Critical\s+(\d+)", cdc, re.MULTILINE)))
    rows = re.findall(r"^\s*\d+\s+CDC-\d+\s+Critical\s+.*$", cdc, re.MULTILINE)
    waivers = [row for row in rows if re.search(
        r"CDC-13\s+Critical.*False Path\s+psram_[ab]_io\[[0-3]\]\s+.*u_[ab]_iddr/D\s*$", row)]
    expected = 8 if psram and machine != "VECTREX" else 0
    require(summary == len(rows) == len(waivers) == expected, "Unexpected critical CDC crossing")
    if expected:
        pins = {re.search(r"psram_[ab]_io\[[0-3]\]", row)[0] for row in waivers}
        require(pins == {f"psram_{bank}_io[{bit}]" for bank in "ab" for bit in range(4)},
                "CDC waivers do not cover exactly the eight external PSRAM pins")
    require(not re.search(r"^\S+#\d+\s+(?:Error|Critical Warning)\s*$",
                          (reports / "drc.rpt").read_text(), re.MULTILINE), "DRC error")
    if machine == "VECTREX":
        pins = {f"psram_{bank}_io[{bit}]" for bank in "ab" for bit in range(4)}
        for kind in ("setup", "hold"):
            report = (reports / f"psram_input_{kind}.rpt").read_text()
            require("VIOLATED" not in report, "Vectrex PSRAM input timing failed")
            found = set(re.findall(r"^\s*Source:\s*(psram_[ab]_io\[[0-3]\])\s*$", report, re.MULTILINE))
            require(found == pins, "Missing Vectrex PSRAM input timing paths")
    return {"timing_ns": slacks, "minimum_bus_skew_ns": min(skew_slacks),
            "critical_cdc_rows": summary, "external_psram_waivers": expected,
            "routed_nets": int(full[1])}


def video_xsa(path):
    with zipfile.ZipFile(path) as archive:
        hwh = [name for name in archive.namelist() if name.endswith(".hwh")]
        require(len(hwh) == 1, "Ambiguous XSA hardware description")
        text = archive.read(hwh[0]).decode("utf-8")
        for port in ("HP0", "HP1"):
            require(re.search(r'NAME="PCW_USE_S_AXI_' + port + r'"\s+VALUE="1"', text) and
                    re.search(r'NAME="C_S_AXI_' + port + r'_DATA_WIDTH"\s+VALUE="64"', text),
                    "Selected platform lacks the 64-bit video " + port + " port")
        return digest(archive.read("ps7_init.c"))


def verify_hardware_source_archive(directory, files):
    """Retain and audit the source capture made before Vivado consumed it."""
    with zipfile.ZipFile(directory / "source-inputs.zip") as archive:
        names = archive.namelist()
        require(len(names) == len(set(names)) and set(files) <= set(names),
                "Incomplete or duplicate FPGA source archive")
        extras = set(names) - set(files)
        allowed = {"build-config/" + name for name in ("source-inputs.json", "generate.tcl", "build.tcl")}
        require(extras <= allowed, "Unexpected FPGA source archive entries")
        for name, expected in files.items():
            require(digest(archive.read(name)) == expected, "FPGA source archive mismatch: " + name)
        for name in extras:
            require(archive.read(name) == (directory / PurePosixPath(name).name).read_bytes(),
                    "FPGA build configuration changed after source capture: " + name)


def verify_hardware(directory, machine, *, ntsc=False):
    record = json.loads((directory / "source-inputs.json").read_text())
    require(record["machine"] == machine, "Wrong source-capture machine")
    configuration = record["configuration"]
    if machine == "C64":
        require(configuration["c64_video_standard"] == ("NTSC" if ntsc else "PAL"), "Wrong C64 video standard")
    if machine == "GBC":
        require(configuration["gbc_psram"] is True, "GBC package requires PSRAM=1")
    verify_sources(record["files"])
    verify_hardware_source_archive(directory, record["files"])
    require(set(record["files"]) == {p.relative_to(ROOT).as_posix() for p in hardware_inputs.inputs(machine)},
            "Incomplete FPGA source capture")
    bit = directory / "multitini_one.runs/impl_1/multitini_one_top.bit"
    marker = Path(str(bit) + ".kind")
    require(marker.read_text().strip() == machine, "Wrong FPGA image kind")
    manifest = dict(line.split("=", 1) for line in (directory / "reports/build_manifest.txt").read_text().splitlines() if "=" in line)
    require(manifest.get("image_kind") == machine and manifest.get("part") == "xc7z020clg484-2" and
            manifest.get("top") == "multitini_one_top", "Wrong FPGA build manifest")
    generics = manifest["generic"]
    require(re.search(r"SHELL_BUILD_ID=32'h" + BUILD_ID + r"\b", generics, re.I), "FPGA build is not VID4")
    active = {"C64": 0, "GBC": 1, "SUPERVISOR": 2, "VECTREX": 3}[machine]
    require(re.search(rf"\bACTIVE_MACHINE={active}\b", generics), "Wrong FPGA active machine")
    require(f"C64_NTSC_MODE=1'b{int(ntsc)}" in generics, "Wrong FPGA PAL/NTSC generic")
    psram = machine in {"C64", "VECTREX"} or configuration["gbc_psram"]
    require(f"ENABLE_GBC_PSRAM=1'b{int(configuration['gbc_psram'])}" in generics, "Wrong GBC PSRAM generic")
    xsa = directory / "multitini_one.xsa"
    log = (directory / "build.log").read_text(errors="replace").replace("\\", "/")
    require("Timing gates passed and XSA exported: " + xsa.as_posix() in log,
            "Build did not reach timing-gated XSA export")
    artifacts = [bit, marker, xsa, directory / "build.log", directory / "build.tcl"]
    artifacts += sorted(p for p in (directory / "reports").rglob("*") if p.is_file())
    captured = datetime.fromisoformat(record["captured_utc"]).timestamp()
    require(all(p.stat().st_mtime >= captured for p in artifacts if p.name != "build.tcl"),
            "FPGA artifact predates source capture")
    artifacts += [directory / "source-inputs.zip", directory / "generate.tcl"]
    artifacts += [p for p in (directory / "generate.log", directory / "generate.jou", directory / "build.jou") if p.is_file()]
    return {"bit": bit, "xsa": xsa, "record": record, "ps7_init_sha256": video_xsa(xsa),
            "timing": timing(directory, machine, psram), "artifacts": artifacts}


def elf_section(path, wanted):
    data, _, _ = audit.elf_segments(path)
    table = struct.unpack_from("<I", data, 32)[0]
    entry_bytes, count, names_index = struct.unpack_from("<HHH", data, 46)
    require(entry_bytes >= 40 and names_index < count and table + entry_bytes * count <= len(data),
            "Invalid ELF section headers")
    names_header = table + names_index * entry_bytes
    offset, length = struct.unpack_from("<II", data, names_header + 16)
    names = data[offset:offset + length]
    for index in range(count):
        entry = table + index * entry_bytes
        name = struct.unpack_from("<I", data, entry)[0]
        if names[name:].split(b"\0", 1)[0].decode("ascii") == wanted:
            offset, length = struct.unpack_from("<II", data, entry + 16)
            require(offset + length <= len(data), "ELF section outside file")
            return data[offset:offset + length]
    raise ValueError("Missing ELF section: " + wanted)


def verify_cpu1_control_program(files, commands):
    require(set(files) == {"firmware/cpu1/main.c", "firmware/common/video_effects.c"},
            "CPU1 production source list must contain only the control-table worker")
    for command in commands:
        text = command.get("command", " ".join(command.get("arguments", [])))
        require(not any(macro in text for macro in (
                    "MT_VIDEO_WORKER_FRAME_REFERENCE", "MT_VIDEO_WORKER_HOST_TEST",
                    "MT_VIDEO_WORKER_NO_DISPATCH")),
                "CPU1 production build enables a reference/test dispatcher")


def verify_cpu_images(workspace):
    manifest = json.loads((workspace / "cpu1_manifest.json").read_text())
    paths = {"cpu0_elf": workspace / "diagnostic/build/diagnostic.elf",
             "cpu1_elf": workspace / "video_worker/build/video_worker.elf",
             "cpu1_binary": workspace / "diagnostic/src/video_core1_blob.bin"}
    for name, path in paths.items():
        require(path.resolve() == Path(manifest[name]["path"]).resolve() and
                describe(path) == {key: manifest[name][key] for key in ("bytes", "sha256")},
                "CPU image differs from verified build manifest: " + name)
    require(elf_section(paths["cpu0_elf"], ".mt_video_core1") == paths["cpu1_binary"].read_bytes(),
            "CPU0 embedded worker differs from CPU1 binary")
    for name, low, high in (("cpu0_elf", 0x100000, 0x20000000), ("cpu1_elf", 0x20000000, 0x21000000)):
        _, entry, segments = audit.elf_segments(paths[name])
        require(low <= entry < high and (name != "cpu1_elf" or entry == low), "Incorrect CPU ELF entry")
        require(all(low <= s["load_address"] and s["load_address"] + s["memory_bytes"] <= high
                    for s in segments), "CPU ELF overlaps reserved DDR")
    record = manifest["build_inputs"]
    verify_sources(record["files"])
    require(re.search(r"MT_FIRMWARE_BUILD_ID\s+UINT32_C\(0x" + BUILD_ID + r"\)",
                      (ROOT / "firmware/platform/xilinx/main.c").read_text(), re.I), "Firmware source is not VID4")
    require("#define MT_ABI_MAJOR 0U" in (ROOT / "firmware/include/multitini_regs.h").read_text() and
            "#define MT_ABI_MINOR 15U" in (ROOT / "firmware/include/multitini_regs.h").read_text() and
            "MT_ABI_MINOR = 16'd15" in (ROOT / "hdl/generated/multitini_regs_pkg.sv").read_text(),
            "Firmware/HDL ABI is not 0.15")
    # A fresh application workspace may explicitly reuse the audited HP0+HP1 AMP
    # export; the build fingerprint records that exact platform directory.
    platform = Path(record["platform_path"]).resolve()
    require(platform.is_relative_to(ROOT / "build"), "Platform export is outside this checkout's build directory")
    platform_files = {p.relative_to(platform).as_posix(): sha(p) for p in platform.rglob("*") if p.is_file()}
    require(platform_files == record["platform_files"], "Exported platform changed after application build")
    require((platform / ".multitini-cpu1-ready").read_text() ==
            "video-worker-abi=1 USE_AMP=1 entry=0x20000000 bytes=0x01000000\n", "CPU1 AMP platform not ready")
    for app, compilation in record["compiled"].items():
        require(app in {"diagnostic", "video_worker"}, "Unknown application in build audit")
        commands_path = workspace / app / "build/compile_commands.json"
        require(sha(commands_path) == compilation["compile_commands_sha256"], "Compiler command list changed")
        commands = json.loads(commands_path.read_text())
        actual = {}
        for command in commands:
            path = Path(command["file"]).resolve()
            name = "generated/video_core1_blob.S" if path == (workspace / "diagnostic/src/video_core1_blob.S").resolve() else path.relative_to(ROOT).as_posix()
            actual[name] = sha(path)
            if not name.startswith("generated/"):
                require(record["files"].get(name) == actual[name], "Compiled source not in build snapshot")
                flags = command["command"].replace("\\", "/")
                require(platform.as_posix() in flags and "-O0" not in flags and
                        ("-O3" if app == "video_worker" else "-O2") in flags and "-mfpu=neon" in flags,
                        "Compiler platform or optimization mismatch")
        require(actual and actual == compilation["files"], "Compiler source checksum mismatch")
        if app == "video_worker":
            verify_cpu1_control_program(actual, commands)
    require(set(record["compiled"]) == {"diagnostic", "video_worker"}, "Missing CPU compiler audit")
    xsa = Path(record["xsa"]["path"])
    require(sha(xsa) == record["xsa"]["sha256"], "Selected application XSA changed")
    exported = list((platform / "hw").glob("*.xsa"))
    require(len(exported) == 1 and sha(exported[0]) == sha(xsa), "Exported platform XSA differs from selected input")
    fsbl = platform / "sw/boot/fsbl.elf"
    require(sha(fsbl) == sha(platform.parents[1] / "zynq_fsbl/build/fsbl.elf"), "FSBL export differs from fresh platform build")
    return {"manifest": manifest, "record": record, "paths": paths, "platform": platform,
            "fsbl": fsbl, "xsa": xsa, "ps7_init_sha256": video_xsa(xsa)}


def source_inventory(required):
    names = {name for name in git("ls-files", "--cached", "--others", "--exclude-standard", "-z").split("\0")
             if name and (ROOT / name).is_file()}
    # Do not include commercial ROMs or user media in a source-delivery bundle.
    excluded = {".bin", ".rom", ".d64", ".d81", ".g64", ".crt", ".prg", ".tap", ".t64", ".gb", ".gbc", ".vec", ".ovr"}
    names = {name for name in names if Path(name).suffix.lower() not in excluded}
    usb = "third_party/CherryUSB/"
    suffixes = {".c", ".h", ".cpp", ".hpp", ".S", ".s", ".cmake", ".ld", ".py", ".sh", ".md", ".txt", ".inc", ".i"}
    names |= {usb + name for name in git("-C", usb, "ls-files", "-z").split("\0")
              if name and (Path(name).suffix in suffixes or Path(name).name in {"LICENSE", "NOTICE", "Kconfig"})
              and (ROOT / usb / name).is_file()}
    require(all(Path(name).suffix.lower() not in excluded for name in required), "Build depends on an excluded ROM/media input")
    names.update(required)
    inventory = {name: sha(source_path(name)) for name in sorted(names)}
    require(all(inventory[name] == expected for name, expected in required.items()), "Snapshot differs from compiled source")
    require({"licenses/files.csv", "provenance/sources.lock.json", "MULTITINI_ONE_PLAN.md"} <= names,
            "Source/license provenance missing")
    return inventory


def archive_files(destination, files):
    with zipfile.ZipFile(destination, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, path in sorted(files.items()):
            archive.write(path, name)
    with zipfile.ZipFile(destination) as archive:
        require(set(archive.namelist()) == set(files) and archive.testzip() is None, "ZIP membership/integrity failure")
        require(all(digest(archive.read(name)) == sha(path) for name, path in files.items()), "ZIP content hash mismatch")


def verify_core(directory, variants):
    text = (directory / "core.cfg").read_text()
    require("abi=" + ABI + "\n" in text, "Core package ABI mismatch")
    rows = [line.split("=", 1)[1].split("|") for line in text.splitlines() if line.startswith("variant=")]
    require(len(rows) == len(variants) and {row[0] for row in rows} == set(variants), "Core variants mismatch")
    result = {}
    for ident, _, filename, size, crc in rows:
        require(Path(filename).name == filename, "Unsafe core binary name")
        payload = (directory / filename).read_bytes()
        require(len(payload) == int(size) and binascii.crc32(payload) & 0xffffffff == int(crc, 0), "Core size/CRC mismatch")
        require(f"# {ident}.sha256={digest(payload)}" in text, "Core SHA-256 mismatch")
        result[ident] = {"filename": filename, "bytes": len(payload), "sha256": digest(payload)}
    return result


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--workspace", required=True, type=Path)
    for name in ("supervisor", "c64-pal", "gbc", "vectrex"):
        result.add_argument("--" + name + "-dir", required=True, type=Path)
    result.add_argument("--vitis", type=Path, default=Path(os.environ.get("XILINX_VITIS", "C:/Xilinx/2025.2/Vitis")))
    result.add_argument("--output", type=Path, default=ROOT / "build/firmware-cpu1-video")
    result.add_argument("--evidence", action="append", type=Path, default=[], help="Explicit additional completed validation log; repeatable")
    result.add_argument("--check-only", action="store_true")
    return result


def main():
    args = parser().parse_args()
    workspace, out, vitis = args.workspace.resolve(), args.output.resolve(), args.vitis.resolve()
    require(out.is_relative_to(ROOT / "build") and out != ROOT / "build", "Output must be a fresh child of build/")
    cpu = verify_cpu_images(workspace)
    builds = {}
    for name, machine, ntsc in (("supervisor", "SUPERVISOR", False), ("c64-pal", "C64", False),
                              ("gbc", "GBC", False), ("vectrex", "VECTREX", False)):
        directory = getattr(args, name.replace("-", "_") + "_dir").resolve()
        builds[name] = verify_hardware(directory, machine, ntsc=ntsc)
        require(builds[name]["ps7_init_sha256"] == cpu["ps7_init_sha256"], "FPGA PS configuration differs from selected HP0+HP1 FSBL")
    required = dict(cpu["record"]["files"])
    for build in builds.values():
        for name, expected in build["record"]["files"].items():
            require(name not in required or required[name] == expected, "CPU/FPGA source conflict: " + name)
            required[name] = expected
    source = source_inventory(required)
    for path in args.evidence:
        require(path.is_file(), "Missing explicit validation evidence: " + str(path))
    run([sys.executable, ROOT / "scripts/check_repo.py"])
    if args.check_only:
        print(json.dumps({"status": "PASS", "firmware_id": "0x" + BUILD_ID, "abi": ABI,
                          "source_files": len(source), "matrix": {n: b["timing"] for n, b in builds.items()}}, indent=2))
        return 0
    out.mkdir(parents=True, exist_ok=False)
    require(not out.with_suffix(".zip").exists(), "Output bundle ZIP already exists")
    for name, path in cpu["paths"].items():
        copy(path, out / "inputs" / {"cpu0_elf": "diagnostic.elf", "cpu1_elf": "video_worker.elf", "cpu1_binary": "video_core1_blob.bin"}[name])
    copy(cpu["fsbl"], out / "inputs/fsbl.elf")
    copy(cpu["xsa"], out / "inputs/platform.xsa")
    copy(workspace / "cpu1_manifest.json", out / "evidence/vitis/cpu1_manifest.json")
    for app in ("diagnostic", "video_worker"):
        for name in ("CMakeLists.txt", "UserConfig.cmake", "lscript.ld", "app.yaml"):
            copy(workspace / app / "src" / name, out / "evidence/vitis" / app / name)
        copy(workspace / app / "build/compile_commands.json", out / "evidence/vitis" / app / "compile_commands.json")
    for name, build in builds.items():
        copy(build["bit"], out / "inputs" / (name + ".bit"))
        copy(Path(str(build["bit"]) + ".kind"), out / "inputs" / (name + ".bit.kind"))
        copy(build["xsa"], out / "inputs" / (name + ".xsa"))
        directory = build["bit"].parents[2]
        copy(directory / "source-inputs.json", out / "evidence" / name / "source-inputs.json")
        for path in build["artifacts"]:
            if path not in (build["bit"], Path(str(build["bit"]) + ".kind"), build["xsa"]):
                copy(path, out / "evidence" / name / path.relative_to(directory))
    for index, path in enumerate(args.evidence):
        copy(path, out / "evidence/validation" / f"{index:02d}-{path.name}")
    archive_files(out / "source.zip", {name: ROOT / name for name in source})
    write_json(out / "source-manifest.json", source)
    platform_files = {"multitini_platform/" + name: cpu["platform"] / name for name in cpu["record"]["platform_files"]}
    archive_files(out / "inputs/used-vitis-platform.zip", platform_files)
    write_json(out / "evidence/vitis/platform-manifest.json", cpu["record"]["platform_files"])
    bootgen = vitis / "bin/bootgen.bat"
    (out / "sd").mkdir()
    run([ROOT / "scripts/make_firmware_bin.bat", out / "sd/FIRMWARE.BIN", out / "inputs/fsbl.elf",
         out / "inputs/supervisor.bit", out / "inputs/diagnostic.elf"], vitis=vitis)
    core_audits = {}
    core_specs = (("c64", "Commodore 64", "0x43363420", [("pal", "PAL", "c64-pal")]),
                  ("gbc", "Game Boy Color", "0x47424320", [("native", "Game Boy / Game Boy Color", "gbc")]),
                  ("vectrex", "Vectrex", "0x56454358", [("native", "Vectrex / Vectrex32", "vectrex")]))
    with tempfile.TemporaryDirectory(prefix="cpu1-video-audit-", dir=ROOT / "build") as temporary:
        scratch = Path(temporary)
        objcopy = vitis / "gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-objcopy.exe"
        run([objcopy, "-O", "binary", out / "inputs/video_worker.elf", scratch / "worker.bin"])
        require((scratch / "worker.bin").read_bytes() == (out / "inputs/video_core1_blob.bin").read_bytes(), "CPU1 ELF/raw binary mismatch")
        for ident, label, core_id, variants in core_specs:
            directory = out / "sd/multitini/cores" / ident
            command = [sys.executable, ROOT / "scripts/make_core_package.py", "--id", ident, "--name", label,
                       "--core-id", core_id, "--abi-major", "0", "--abi-minor", "15", "--content-root",
                       "0:/multitini/cores/" + ident + "/media", "--output", directory, "--bootgen", bootgen]
            for variant, display, build_name in variants:
                command += ["--variant", f"{variant}|{display}|{out / 'inputs' / (build_name + '.bit')}"]
            run(command, vitis=vitis)
            core_audits[ident] = verify_core(directory, {v[0] for v in variants})
            for variant, _, build_name in variants:
                processed, _ = audit.process_bit(bootgen, out / "inputs" / (build_name + ".bit"), scratch, build_name)
                require(digest(processed) == core_audits[ident][variant]["sha256"], "SD core differs from selected BIT")
        images = audit.parse_firmware((out / "sd/FIRMWARE.BIN").read_bytes())
        payload = {"status": "PASS", "fsbl": audit.verify_elf(images[0], out / "inputs/fsbl.elf"),
                   "cpu0": audit.verify_elf(images[2], out / "inputs/diagnostic.elf"), "cores": core_audits,
                   "cpu1_embedded_sha256": sha(out / "inputs/video_core1_blob.bin")}
        processed, log = audit.process_bit(bootgen, out / "inputs/supervisor.bit", scratch, "supervisor")
        require(images[1][0]["payload"] == processed and images[1][0]["attributes"] == 0x20 and
                images[1][0]["load_address"] == images[1][0]["entry_address"] == 0, "Supervisor payload mismatch")
        payload.update(supervisor_processed_sha256=digest(processed), bootgen_process_output=log,
                       bootgen_readback=audit.bootgen(bootgen, ["-read", out / "sd/FIRMWARE.BIN"], scratch))
    write_json(out / "evidence/payload-audit.json", payload)
    write_json(out / "evidence/timing-audit.json", {name: build["timing"] for name, build in builds.items()})
    (out / "README.txt").write_text(
        "Multitini VID4 source-resolution video test bundle / ABI 0.15\n\n"
        "Copy sd/ contents to the SD card, preserving existing user media and settings.\n"
        "Use the existing golden updater for FIRMWARE.BIN. No flash is performed here.\n"
        "The supervisor and all supplied machine cores form one matching HP0+HP1/ABI15 set.\n"
        "C64 supplies one PAL variant; the display output remains nominal 60 Hz.\n"
        "The FPGA scheduler consumes captures in order against the 60 Hz output cadence.\n"
        "HP0 serves capture and scanout; HP1 serves persistence, blur, and bloom.\n"
        "Blur follows each core's source-to-display scale; Vectrex keeps its prior strength.\n"
        "Low-resolution cores use lighter weights without another frame-buffer pass.\n"
        "The hardware scheduler owns frame buffers and reports missed deadlines as faults.\n"
        "CPU1 computes color-control tables only, in private memory and the shared mailbox.\n"
        "CPU0 installs settings and drains all video engines before a core change.\n"
        "Neither CPU schedules frames or reads/writes the FPGA capture/output/history pools.\n"
        "No ROMs, games, user data, or settings file are supplied. Board checks are pending.\n\n"
        "source.zip contains the exact dirty source snapshot and license/provenance files.\n"
        "Per-build inputs, timing/CDC reports, compile commands, and payload audits are in evidence/.\n"
        "Any recovery passes and added implementation settings are archived in validation evidence.\n"
        "Rebuild with Vivado/Vitis 2025.2 and source scripts; selected generics are in each\n"
        "build_manifest.txt and build.tcl. The exported CPU0/CPU1 platform is archived in\n"
        "inputs/used-vitis-platform.zip, with its HP0+HP1 XSA in inputs/platform.xsa.\n"
        "Set MULTITINI_PLATFORM_XPFM to the extracted platform XPFM and MULTITINI_XSA\n"
        "to inputs/platform.xsa for a fresh application workspace. See source scripts and\n"
        "MULTITINI_ONE_PLAN.md after extracting source.zip.\n", encoding="utf-8")
    verify_sources(source)
    # Recheck every selected original after copying and audit work; reject races.
    verify_cpu_images(workspace)
    for name, build in builds.items():
        require(sha(build["bit"]) == sha(out / "inputs" / (name + ".bit")) and
                sha(build["xsa"]) == sha(out / "inputs" / (name + ".xsa")), "FPGA artifact changed during packaging")
    files = {p.relative_to(out).as_posix(): describe(p) for p in sorted(out.rglob("*")) if p.is_file()}
    write_json(out / "manifest.json", {"classification": "Local test; board acceptance pending",
        "created_utc": datetime.now(timezone.utc).isoformat(), "firmware_id": "0x" + BUILD_ID,
        "abi": ABI, "git_head": git("rev-parse", "HEAD").strip(), "dirty_source_snapshot": True,
        "source_files": len(source), "compiled_sources": cpu["record"]["compiled"], "files": files})
    files["manifest.json"] = describe(out / "manifest.json")
    (out / "SHA256SUMS.txt").write_text("".join(f"{v['sha256']}  {name}\n" for name, v in sorted(files.items())), encoding="ascii")
    archive_files(out.with_suffix(".zip"), {p.relative_to(out).as_posix(): p for p in out.rglob("*") if p.is_file()})
    print(json.dumps({"firmware": describe(out / "sd/FIRMWARE.BIN"), "bundle": describe(out.with_suffix(".zip")),
                      "output": str(out), "status": "PASS"}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
