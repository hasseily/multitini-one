#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package two explicit video-core updates while retaining a pinned VID4 baseline.

Only C64 PAL and GBC SD payloads are emitted. Retained CPU, supervisor and Vectrex
are verified against their immutable baseline archive, never current sources.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import io
import json
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
import zipfile

import audit_vectrex_package as audit
import package_cpu1_video_test as common

ROOT = Path(__file__).resolve().parents[1]
ABI = "0.15"
BASELINE_ID = "56494434"
BUILD_ID = "56494435"
IMAGES = {"c64-pal": "C64", "gbc": "GBC"}
CHANGED_HDL = "hdl/common/multitini_video_frame_filter.sv"
RETAINED = (
    "sd/FIRMWARE.BIN", "inputs/fsbl.elf", "inputs/diagnostic.elf",
    "inputs/video_worker.elf", "inputs/video_core1_blob.bin",
    "inputs/supervisor.bit", "inputs/supervisor.xsa", "inputs/platform.xsa",
    "sd/multitini/cores/vectrex/core.cfg",
    "sd/multitini/cores/vectrex/vectrex-native.bin",
)
SD_FILES = {"multitini/cores/c64/core.cfg", "multitini/cores/c64/c64-pal.bin",
            "multitini/cores/gbc/core.cfg", "multitini/cores/gbc/gbc-native.bin"}
require = common.require


def safe_members(archive):
    names = [item.orig_filename for item in archive.infolist()]
    require(names == archive.namelist(), "Unsafe ZIP member normalization")
    require(len(names) == len(set(names)), "Duplicate ZIP member")
    for name in names:
        path = PurePosixPath(name)
        require(name and name == path.as_posix() and not path.is_absolute() and
                ".." not in path.parts and "\\" not in name and ":" not in name,
                "Unsafe ZIP member: " + name)
    require(archive.testzip() is None, "ZIP CRC failure")
    return set(names)


def verify_source_archive(path, sources):
    with zipfile.ZipFile(path) as archive:
        require(safe_members(archive) == set(sources), "Source archive membership mismatch")
        require(all(common.digest(archive.read(name)) == expected for name, expected in sources.items()),
                "Source archive bytes mismatch")


def verify_xsa_bit(xsa, bit):
    with zipfile.ZipFile(xsa) as archive:
        names = safe_members(archive)
        bits = [name for name in names if name.lower().endswith(".bit")]
        require(len(bits) == 1, "XSA must contain exactly one BIT")
        require(archive.read(bits[0]) == bit.read_bytes(), "XSA embedded BIT differs from selected BIT")


def incremental_script(original):
    """Only permit the reviewed static-reference transformation of the full gate."""
    newline = b"\r\n" if b"\r\n" in original else b"\n"
    anchor = b"set script_dir [file dirname [file normalize [info script]]]"
    require(original.count(anchor) == 1, "Unexpected original script directory binding")
    result = original.replace(anchor, ("set script_dir [file normalize {" +
                                       (ROOT / "scripts").as_posix() + "}]").encode(), 1)
    anchor = b"reset_run impl_1" + newline
    require(result.count(anchor) == 1, "Unexpected original implementation launch")
    setup = "\n".join((
        "# Explicit immutable same-core reference; fresh synthesis remains above.",
        "set reference [file normalize [file join $build_dir incremental-reference reference.dcp]]",
        "set_property AUTO_INCREMENTAL_CHECKPOINT 0 [get_runs impl_1]",
        "set_property incremental_checkpoint.directive TimingClosure [get_runs impl_1]",
        "add_files -fileset utils_1 -norecurse $reference",
        "set_property incremental_checkpoint $reference [get_runs impl_1]",
        "foreach property {AUTO_INCREMENTAL_CHECKPOINT INCREMENTAL_CHECKPOINT INCREMENTAL_CHECKPOINT.DIRECTIVE} {",
        '    puts "STATIC_INCREMENTAL $property=[get_property $property [get_runs impl_1]]"',
        "}", "")).encode().replace(b"\n", newline)
    return result.replace(anchor, setup + anchor, 1) + newline + \
        b"report_incremental_reuse -file [file join $report_dir incremental_reuse.rpt]" + newline


def verify_incremental_recipe(directory, sources):
    reference = directory / "incremental-reference"
    original = (reference / "build_and_export_xsa.original.tcl").read_bytes()
    require(common.digest(original) == sources["scripts/build_and_export_xsa.tcl"],
            "Original gates differ from captured source")
    selected = reference / "build_and_export_xsa.incremental.tcl"
    require(selected.read_bytes() == incremental_script(original), "Build changed the fresh-synthesis or full gate recipe")
    calls = re.findall(r"^source \{([^}]+)\}$", (directory / "build.tcl").read_text(), re.M)
    require(calls == [selected.as_posix()], "Build does not select exactly the audited gate recipe")


def verify_reuse(directory, expected):
    text = (directory / "reports/incremental_reuse.rpt").read_text()
    table = [[field.strip() for field in line.strip().strip("|").split("|")]
             for line in text.splitlines() if line.strip().startswith("|")]
    headers = [row for row in table if row[0].lower() == "type" and
               any("reuse" in field.lower() for field in row[1:])]
    require(len(headers) == 1, "Missing or ambiguous reuse table")
    header = [field.lower() for field in headers[0]]
    current = [i for i, field in enumerate(header) if "current" in field and "reuse" in field]
    if not current:
        current = [i for i, field in enumerate(header) if "reuse" in field and "initial" not in field]
    total = [i for i, field in enumerate(header) if field == "total"]
    require(len(current) == len(total) == 1, "Missing or ambiguous reuse columns")
    rows = {}
    for row in table:
        name = row[0].lower()
        if name in ("cells", "nets"):
            require(name not in rows and len(row) == len(header), "Duplicate or malformed reuse row")
            rows[name] = {"current_reuse_percent": float(row[current[0]].rstrip("%")),
                          "total": int(row[total[0]].replace(",", ""))}
    require(set(rows) == {"cells", "nets"} and
            all(0 < row["current_reuse_percent"] <= 100 and row["total"] > 0 for row in rows.values()),
            "No actual cell/net reuse")
    require(expected == {"actual_reuse": True, "fallback": False, "rows": rows},
            "Reuse summary differs from actual report")
    reference = (directory / "incremental-reference/reference.dcp").as_posix()
    for name, value in (("Auto Incremental", "No"), ("Incremental Directive", "TimingClosure"),
                        ("DCP Location:", reference)):
        require([row[1] for row in table if row[0] == name] == [value], "Unexpected reported " + name)
    actual = re.findall(r"^STATIC_INCREMENTAL ([^=\r\n]+)=([^\r\n]+)$",
                        (directory / "build.log").read_text(errors="replace"), re.M)
    settings = {"AUTO_INCREMENTAL_CHECKPOINT": "0", "INCREMENTAL_CHECKPOINT": reference,
                "INCREMENTAL_CHECKPOINT.DIRECTIVE": "TimingClosure"}
    require(len(actual) == 3 and dict(actual) == settings, "Actual incremental settings differ")


def verify_baseline(path, expected_sha256):
    """Verify the retained package and its historical sources without live-tree reads."""
    require(re.fullmatch(r"[0-9a-fA-F]{64}", expected_sha256) is not None,
            "Expected baseline SHA-256 must contain 64 hex digits")
    require(common.sha(path) == expected_sha256.lower(), "Wrong baseline ZIP SHA-256")
    with zipfile.ZipFile(path) as archive:
        names = safe_members(archive)
        manifest = json.loads(archive.read("manifest.json"))
        require(manifest.get("firmware_id") == "0x" + BASELINE_ID and manifest.get("abi") == ABI,
                "Baseline must be VID4 / ABI 0.15")
        files = manifest["files"]
        require(names == set(files) | {"manifest.json", "SHA256SUMS.txt"}, "Baseline membership mismatch")
        for name, expected in files.items():
            data = archive.read(name)
            require(len(data) == expected["bytes"] and common.digest(data) == expected["sha256"],
                    "Baseline file mismatch: " + name)
        sums = {}
        for line in archive.read("SHA256SUMS.txt").decode("ascii").splitlines():
            match = re.fullmatch(r"([0-9a-f]{64})  (.+)", line)
            require(match is not None and match[2] not in sums, "Invalid or duplicate baseline checksum")
            sums[match[2]] = match[1]
        require(set(sums) == set(files) | {"manifest.json"}, "Baseline checksum membership mismatch")
        require(all(common.digest(archive.read(name)) == expected for name, expected in sums.items()),
                "Baseline checksum mismatch")
        require(set(RETAINED) <= names, "Baseline retained component missing")
        require(json.loads(archive.read("evidence/payload-audit.json"))["status"] == "PASS",
                "Baseline payload audit did not pass")
        sources = json.loads(archive.read("source-manifest.json"))
        with zipfile.ZipFile(io.BytesIO(archive.read("source.zip"))) as source_zip:
            require(safe_members(source_zip) == set(sources), "Baseline source membership mismatch")
            require(all(common.digest(source_zip.read(name)) == expected for name, expected in sources.items()),
                    "Baseline historical source mismatch")
        ps7 = common.video_xsa(io.BytesIO(archive.read("inputs/platform.xsa")))
        qualifications = {}
        for name in names:
            if name.endswith("-qualification.json"):
                record = json.loads(archive.read(name))
                image = record.get("image")
                if image in IMAGES:
                    require(image not in qualifications, "Duplicate baseline qualification")
                    require(record.get("passed") is True and record.get("firmware_id") == "0x" + BASELINE_ID,
                            "Baseline core not qualified")
                    qualifications[image] = record
        require(set(qualifications) == set(IMAGES), "Baseline core qualifications missing")
        snapshots = {image: archive.read("evidence/" + image + "/source-inputs.json") for image in IMAGES}
        return {"zip": common.describe(path), "zip_name": path.name, "manifest": manifest,
                "retained": {name: files[name] for name in RETAINED}, "ps7_init_sha256": ps7,
                "source_archive": files["source.zip"], "source_manifest": files["source-manifest.json"],
                "qualifications": qualifications, "source_snapshots": snapshots}


def verify_new_build(directory, image, baseline):
    result = common.verify_hardware(directory, IMAGES[image], build_id=BUILD_ID)
    verify_xsa_bit(result["xsa"], result["bit"])
    require(result["ps7_init_sha256"] == baseline["ps7_init_sha256"], "New core changes retained PS initialization")
    qualification = json.loads((directory / "qualification.json").read_text())
    require(qualification == json.loads((directory / "result.json").read_text()) and
            qualification.get("passed") is True and qualification.get("image") == image and
            qualification.get("firmware_id") == "0x" + BUILD_ID and qualification.get("abi") == ABI,
            "New core qualification identity mismatch")
    require(qualification["bit"] == common.describe(result["bit"]) and
            qualification["xsa"] == common.describe(result["xsa"]) and qualification["timing"] == result["timing"],
            "New core qualification artifacts differ")
    reference = directory / "incremental-reference"
    record = json.loads((reference / "manifest.json").read_text())
    require(record["image"] == image and record["reference_qualified"] is True and
            record["fresh_synthesis"] is True and record["original_gate_tail_unchanged"] is True,
            "Missing fresh-synthesis/reference/gate provenance")
    require(record["reference_result"] == baseline["qualifications"][image], "Reference is not the pinned VID4 core")
    require((reference / "reference-build/source-inputs.json").read_bytes() == baseline["source_snapshots"][image],
            "Reference source capture differs from pinned baseline")
    old_sources = json.loads(baseline["source_snapshots"][image])["files"]
    new_sources = result["record"]["files"]
    require(set(old_sources) == set(new_sources), "Unexpected new core source inventory")
    changed = sorted(name for name in old_sources if old_sources[name] != new_sources[name])
    require(changed == record["source_delta_from_reference"] == [CHANGED_HDL], "Unexpected HDL delta from VID4")
    settings = {"AUTO_INCREMENTAL_CHECKPOINT": 0, "INCREMENTAL_CHECKPOINT.DIRECTIVE": "TimingClosure"}
    require(record["settings"] == qualification["incremental"]["settings"] == settings,
            "Unexpected incremental settings")
    for name, expected in record["files"].items():
        require(common.describe(reference / name) == expected, "Reference file mismatch: " + name)
    archive_path = directory / "incremental-inputs.zip"
    with zipfile.ZipFile(archive_path) as archive:
        names = safe_members(archive)
        require(names == {"incremental-reference/" + name for name in record["files"]} |
                {"incremental-reference/manifest.json"}, "Reference ZIP membership mismatch")
        require(all(archive.read(name) == (directory / name).read_bytes() for name in names), "Reference ZIP mismatch")
    require(common.describe(archive_path) == qualification["incremental"]["input_archive"] and
            common.describe(reference / "reference.dcp") == record["reference"] == qualification["incremental"]["reference"],
            "Reference checkpoint/archive hash mismatch")
    reuse = qualification["incremental"]["reuse_summary"]
    require(reuse.get("actual_reuse") is True and reuse.get("fallback") is False and
            all(0 < reuse["rows"][name]["current_reuse_percent"] <= 100 and reuse["rows"][name]["total"] > 0
                for name in ("cells", "nets")), "No recorded actual cell/net reuse")
    require(common.describe(directory / "reports/incremental_reuse.rpt") == qualification["incremental"]["actual_reuse_report"],
            "Incremental reuse report changed")
    verify_incremental_recipe(directory, new_sources)
    verify_reuse(directory, reuse)
    result["input_snapshot"] = {name: common.describe(path)
                                for name, path in package_inputs(directory, image, result).items()}
    require(result["input_snapshot"]["inputs/" + image + ".bit"] == qualification["bit"] and
            result["input_snapshot"]["inputs/" + image + ".xsa"] == qualification["xsa"],
            "Qualified payload changed during input capture")
    return result


def package_inputs(directory, image, build):
    """Map each selected immutable input to its exact package destination."""
    payloads = {"inputs/" + image + ".bit": build["bit"],
                "inputs/" + image + ".xsa": build["xsa"],
                "inputs/" + image + ".bit.kind": Path(str(build["bit"]) + ".kind")}
    result = dict(payloads)
    for path in build["artifacts"]:
        if path not in payloads.values():
            result["evidence/" + image + "/" + path.relative_to(directory).as_posix()] = path
    for name in ("source-inputs.json", "qualification.json", "result.json", "incremental-inputs.zip"):
        result["evidence/" + image + "/" + name] = directory / name
    return result


def verify_final_inputs(directories, builds, output):
    """Fence original inputs and copied payloads against the qualified snapshot."""
    for image, build in builds.items():
        common.verify_sources(build["record"]["files"])
        inputs = package_inputs(directories[image], image, build)
        require(set(inputs) == set(build["input_snapshot"]), "Selected input inventory changed")
        for name, path in inputs.items():
            expected = build["input_snapshot"][name]
            require(common.describe(path) == expected, "Selected input changed during packaging: " + str(path))
            require(common.describe(output / name) == expected, "Copied input changed during packaging: " + name)


def verify_sd_scope(directory):
    actual = {path.relative_to(directory).as_posix() for path in directory.rglob("*") if path.is_file()}
    require(actual == SD_FILES, "Core-only update must contain exactly C64 PAL and GBC cfg/payload files")


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--baseline-zip", required=True, type=Path)
    result.add_argument("--baseline-sha256", required=True)
    result.add_argument("--c64-pal-dir", required=True, type=Path)
    result.add_argument("--gbc-dir", required=True, type=Path)
    result.add_argument("--output", required=True, type=Path)
    result.add_argument("--vitis", type=Path, default=Path("C:/Xilinx/2025.2/Vitis"))
    result.add_argument("--evidence", action="append", default=[], type=Path)
    result.add_argument("--check-only", action="store_true")
    return result


def main():
    args = parser().parse_args()
    output = args.output.resolve()
    require(output.is_relative_to(ROOT / "build") and output != ROOT / "build", "Output must be a child of build/")
    baseline = verify_baseline(args.baseline_zip.resolve(), args.baseline_sha256)
    directories = {"c64-pal": args.c64_pal_dir.resolve(), "gbc": args.gbc_dir.resolve()}
    builds = {name: verify_new_build(path, name, baseline) for name, path in directories.items()}
    required = {}
    for build in builds.values():
        for name, expected in build["record"]["files"].items():
            require(name not in required or required[name] == expected, "New core source conflict")
            required[name] = expected
    sources = common.source_inventory(required)
    for path in args.evidence:
        require(path.is_file(), "Missing explicit validation evidence: " + str(path))
    common.run([sys.executable, ROOT / "scripts/check_repo.py"])
    if args.check_only:
        print(json.dumps({"status": "PASS", "pl_build_id": "0x" + BUILD_ID, "abi": ABI,
                          "baseline": baseline["zip"], "source_files": len(sources),
                          "matrix": {name: value["timing"] for name, value in builds.items()}}, indent=2))
        return 0
    require(not output.exists() and not output.with_suffix(".zip").exists(), "Output must be fresh")
    output.mkdir()
    dependency = {name: baseline[name] for name in ("zip", "zip_name", "retained", "ps7_init_sha256", "source_archive", "source_manifest")}
    dependency.update({"baseline_id": "0x" + BASELINE_ID, "abi": ABI,
                       "rule": "Retain these installed bytes; this update supplies no CPU, supervisor or Vectrex replacement"})
    common.write_json(output / "baseline-dependency.json", dependency)
    with zipfile.ZipFile(args.baseline_zip) as archive:
        for name in ("manifest.json", "source-manifest.json", "evidence/payload-audit.json"):
            target = output / "evidence/baseline" / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(archive.read(name))
    for image, build in builds.items():
        for name, path in package_inputs(directories[image], image, build).items():
            common.copy(path, output / name)
    for index, path in enumerate(args.evidence):
        common.copy(path, output / "evidence/validation" / f"{index:02d}-{path.name}")
    common.archive_files(output / "source.zip", {name: ROOT / name for name in sources})
    common.write_json(output / "source-manifest.json", sources)
    bootgen = args.vitis / "bin/bootgen.bat"
    core_audits = {}
    with tempfile.TemporaryDirectory(prefix="blur-core-audit-", dir=ROOT / "build") as temporary:
        for ident, label, core_id, variant, variant_label, image in (
                ("c64", "Commodore 64", "0x43363420", "pal", "PAL", "c64-pal"),
                ("gbc", "Game Boy Color", "0x47424320", "native", "Game Boy / Game Boy Color", "gbc")):
            directory = output / "sd/multitini/cores" / ident
            common.run([sys.executable, ROOT / "scripts/make_core_package.py", "--id", ident, "--name", label,
                        "--core-id", core_id, "--abi-major", "0", "--abi-minor", "15", "--content-root",
                        "0:/multitini/cores/" + ident + "/media", "--output", directory, "--bootgen", bootgen,
                        "--variant", f"{variant}|{variant_label}|{output / 'inputs' / (image + '.bit')}"], vitis=args.vitis)
            core_audits[ident] = common.verify_core(directory, {variant})
            processed, _ = audit.process_bit(bootgen, output / "inputs" / (image + ".bit"), Path(temporary), image)
            require(common.digest(processed) == core_audits[ident][variant]["sha256"], "Core payload differs from selected BIT")
    verify_sd_scope(output / "sd")
    common.write_json(output / "evidence/payload-audit.json", {"status": "PASS", "cores": core_audits,
                      "pl_build_id": "0x" + BUILD_ID, "retained_baseline_id": "0x" + BASELINE_ID,
                      "abi": ABI, "retained_files_emitted": False})
    common.write_json(output / "evidence/timing-audit.json", {name: build["timing"] for name, build in builds.items()})
    (output / "README.txt").write_text(
        "Multitini VID5 blur update: C64 PAL and Game Boy Color SD cores only / ABI 0.15\n\n"
        "Copy the four files under sd/ onto the matching SD paths, preserving media, saves and settings.\n"
        "Keep the installed VID4 FIRMWARE.BIN, CPU/FSBL/supervisor and Vectrex unchanged. No firmware\n"
        "flash or board operation is performed or required by this package. Reload a core to use it.\n"
        "The new C64/GBC FPGA build ID is 0x56494435; the retained firmware/supervisor ID is 0x56494434.\n"
        "This intentional identity difference keeps the same ABI 0.15 register/control contract.\n"
        "This is a local test update. Board acceptance and intermittent GBC startup remain separate.\n\n"
        "baseline-dependency.json pins the exact retained VID4 ZIP and component hashes. Obtain and\n"
        "retain that original archive for the CPU/supervisor/Vectrex sources; historical sources are\n"
        "verified inside it, not against the newer checkout. source.zip here is the complete current\n"
        "source/license snapshot for the two NEW FPGA builds only. Each core has its exact prebuild\n"
        "source-inputs.zip and static reference DCP/source/settings archive plus all signoff reports.\n"
        "Rebuild using the captured per-core generate/build Tcl with Vivado2025.2 and the archived\n"
        "incremental-reference. Keep original input ZIPs beside extracted records. The generated Tcl\n"
        "contains the captured repository root; a relocated replay must record only path-prefix\n"
        "changes and preserve every source, setting, clock, constraint and full gate/export body.\n",
        encoding="utf-8")
    common.verify_sources(sources)
    verify_source_archive(output / "source.zip", sources)
    verify_final_inputs(directories, builds, output)
    require(common.describe(args.baseline_zip) == baseline["zip"], "Retained baseline changed during packaging")
    files = {path.relative_to(output).as_posix(): common.describe(path) for path in sorted(output.rglob("*")) if path.is_file()}
    common.write_json(output / "manifest.json", {"classification": "Two-core SD update; board acceptance pending",
                      "created_utc": datetime.now(timezone.utc).isoformat(), "pl_build_id": "0x" + BUILD_ID,
                      "retained_baseline_id": "0x" + BASELINE_ID, "abi": ABI,
                      "git_head": common.git("rev-parse", "HEAD").strip(), "source_files": len(sources),
                      "source_scope": "New C64 PAL/GBC FPGA images only", "files": files})
    files["manifest.json"] = common.describe(output / "manifest.json")
    (output / "SHA256SUMS.txt").write_text("".join(f"{record['sha256']}  {name}\n" for name, record in sorted(files.items())), encoding="ascii")
    common.archive_files(output.with_suffix(".zip"), {path.relative_to(output).as_posix(): path for path in output.rglob("*") if path.is_file()})
    print(json.dumps({"status": "PASS", "output": str(output), "bundle": common.describe(output.with_suffix(".zip")),
                      "baseline_unchanged": baseline["zip"], "cores": core_audits}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
