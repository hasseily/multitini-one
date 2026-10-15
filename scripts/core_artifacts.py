#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit explicitly selected FPGA artifacts without rewriting their provenance.

Historical sources stay in their original per-image archives.  The caller can
require an effective-input match for build reuse; an explicit package selection
may instead retain a compatible older shell.  Different PS configurations are
not automatically considered compatible.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path, PurePosixPath
import posixpath
import re
import tarfile
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
ABI_SOURCE = "hdl/generated/multitini_regs_pkg.sv"
CONFIG_NAMES = {"source-inputs.json", "generate.tcl", "build.tcl"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def describe(path):
    data = Path(path).read_bytes()
    return {"bytes": len(data), "sha256": digest(data)}


def safe_source_name(name):
    require(isinstance(name, str) and name and "\\" not in name,
            "Unsafe archived source path")
    path = PurePosixPath(name)
    require(not path.is_absolute() and ".." not in path.parts and
            path.as_posix() == name and ":" not in name and
            path.parts[0] not in {"build", ".git", ".aws", ".codex", ".agents"} and
            not path.parts[0].startswith("build-"), "Unsafe archived source path: " + name)
    return path


def parse_reuse_images(values, allowed_names):
    result = {}
    for value in values or []:
        name, separator, directory = value.partition("=")
        require(separator and name in allowed_names and directory,
                "Reuse selection must be NAME=DIR for a selected image: " + value)
        require(name not in result, "Duplicate reuse image: " + name)
        path = Path(directory).resolve()
        require(path.is_dir(), "Reuse image directory is missing: " + str(path))
        result[name] = path
    return result


def audit_source_archives(directory, record):
    """Verify both original archives, including the captured build settings."""
    directory = Path(directory)
    files = record.get("files")
    require(isinstance(files, dict) and files, "Empty archived source inventory")
    for name, expected in files.items():
        safe_source_name(name)
        require(isinstance(expected, str) and re.fullmatch(r"[0-9a-f]{64}", expected),
                "Invalid archived source hash: " + name)
    archive = record.get("archive", {})
    require(archive.get("name") == "source-inputs.sources.tar.gz",
            "Missing original source TAR identity")
    tar_path = directory / archive["name"]
    require(describe(tar_path)["sha256"] == archive.get("sha256"), "Source TAR hash mismatch")
    zip_path = directory / "source-inputs.zip"
    configs = {"build-config/" + name for name in CONFIG_NAMES}
    with zipfile.ZipFile(zip_path) as source:
        names = source.namelist()
        require(len(names) == len(set(names)) and set(names) == set(files) | configs,
                "Source ZIP membership mismatch")
        for name, expected in files.items():
            require(digest(source.read(name)) == expected, "Source ZIP hash mismatch: " + name)
        for name in CONFIG_NAMES:
            require(source.read("build-config/" + name) == (directory / name).read_bytes(),
                    "Original build settings changed: " + name)
        minimum = {ABI_SOURCE, "hdl/hdl_sources.txt", "scripts/create_project.tcl",
                   "scripts/build_and_export_xsa.tcl", "scripts/capture_vectrex_build_inputs.py",
                   "LICENSE", "licenses/files.csv", "provenance/sources.lock.json", "MULTITINI_ONE_PLAN.md"}
        require(minimum <= set(files), "Original source capture lacks build/interface/license inputs")
        selected = {"common", record["machine"].lower()}
        section = "common"
        for line in source.read("hdl/hdl_sources.txt").decode("utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1]
            elif section in selected:
                name = posixpath.normpath("hdl/" + line.split("|", 1)[0].strip())
                safe_source_name(name)
                require(name in files, "Original HDL closure is incomplete: " + name)
        abi_source = source.read(ABI_SOURCE).decode("utf-8")
    with tarfile.open(tar_path, "r:gz") as source:
        members = source.getmembers()
        require(len(members) == len(files) and {m.name for m in members} == set(files) and
                all(m.isfile() for m in members), "Source TAR membership mismatch")
        for member in members:
            stream = source.extractfile(member)
            require(stream is not None and digest(stream.read()) == files[member.name],
                    "Source TAR content mismatch: " + member.name)
    consumed = json.loads((directory / "reports/build_inputs.json").read_text())
    require(all(consumed.get(field) == record.get(field) for field in ("machine", "configuration", "files")),
            "Vivado source capture differs from original image snapshot")
    consumed_archive = consumed.get("archive", {})
    require(consumed_archive.get("name") == "build_inputs.sources.tar.gz",
            "Missing Vivado source archive identity")
    consumed_tar = directory / "reports" / consumed_archive["name"]
    require(describe(consumed_tar)["sha256"] == consumed_archive.get("sha256"),
            "Vivado source TAR hash mismatch")
    with tarfile.open(consumed_tar, "r:gz") as source:
        members = source.getmembers()
        require(len(members) == len(files) and {m.name for m in members} == set(files) and
                all(m.isfile() for m in members), "Vivado source TAR membership mismatch")
        for member in members:
            stream = source.extractfile(member)
            require(stream is not None and digest(stream.read()) == files[member.name],
                    "Vivado source TAR content mismatch: " + member.name)
    abi = []
    for field in ("MAJOR", "MINOR"):
        matches = re.findall(r"\bMT_ABI_" + field + r"\s*=\s*16'd(\d+)\s*;", abi_source)
        require(len(matches) == 1 and 0 <= int(matches[0]) <= 65535,
                "Ambiguous archived interface ABI " + field)
        abi.append(int(matches[0]))
    return {"abi_major": abi[0], "abi_minor": abi[1],
            "abi": f"{abi[0]}.{abi[1]}",
            "source_zip": describe(zip_path), "source_tar": describe(tar_path),
            "source_manifest": describe(directory / "source-inputs.json")}


def archived_build_id(directory, machine):
    """Bind the report identity to both original generator/export settings."""
    directory = Path(directory)
    report = dict(line.split("=", 1) for line in
                  (directory / "reports/build_manifest.txt").read_text().splitlines() if "=" in line)
    matches = re.findall(r"\bSHELL_BUILD_ID=32'h([0-9A-Fa-f]{8})\b", report.get("generic", ""))
    require(len(matches) == 1, "Missing or ambiguous original image build ID")
    build_id = matches[0].upper()
    for filename in ("generate.tcl", "build.tcl"):
        text = (directory / filename).read_text()
        for key, expected in (("MULTITINI_BUILD_ID", build_id), ("MULTITINI_MACHINE", machine)):
            values = re.findall(r"^set ::env\(" + key + r"\) \{([^{}\r\n]+)\}\s*$", text, re.M)
            require(len(values) == 1 and values[0].upper() == expected.upper(),
                    "Original build setting/report mismatch: " + filename + " " + key)
    return build_id


def ancillary_input(name):
    path = PurePosixPath(name)
    return (name in {"LICENSE", "MULTITINI_ONE_PLAN.md"} or
            path.parts[0] in {"docs", "licenses", "provenance"} or
            (path.parts[0] == "spec" and path.suffix.lower() == ".md"))


def compare_current_inputs(record, machine):
    import capture_vectrex_build_inputs as inputs
    old = record["files"]
    current = {path.relative_to(ROOT).as_posix(): digest(path.read_bytes())
               for path in inputs.inputs(machine)}
    changed = sorted(name for name in set(old) | set(current) if old.get(name) != current.get(name))
    required = [name for name in changed if not ancillary_input(name)]
    return {"current_input_match": not required, "current_snapshot_match": not changed,
            "changed_required": required,
            "changed_ancillary": [name for name in changed if ancillary_input(name)]}


def require_compatible_inputs(metadata):
    require(metadata["current_input_match"], "Effective FPGA inputs changed: " +
            ", ".join(metadata["changed_required"]))


def require_runtime_abi(identity, firmware_major, firmware_minor):
    # The shared dense UI starts at 0.11.  Do not assume an unknown newer ABI
    # is supported merely because the runtime's basic probe accepts it.
    require(identity["abi_major"] == firmware_major == 0 and
            11 <= identity["abi_minor"] <= firmware_minor,
            "Image ABI is not supported by selected firmware: " + identity["abi"])


def platform_contract(image_hash, firmware_hash):
    require(isinstance(image_hash, str) and re.fullmatch(r"[0-9a-f]{64}", image_hash) and
            image_hash == firmware_hash,
            "PS7/FSBL configuration mismatch; differing PS settings require explicit qualification")
    return {"status": "PASS", "method": "exact-ps7-init-sha256",
            "image_ps7_init_sha256": image_hash, "firmware_ps7_init_sha256": firmware_hash}


def inspect_artifact(directory, machine, *, ntsc=False, gbc_psram=False,
                     genesis_pal=False, require_current_inputs=False):
    """Reusable JSON-safe audit for an explicitly selected completed build."""
    import package_cpu1_video_test as package
    try:
        build = package.verify_hardware(Path(directory).resolve(), machine, ntsc=ntsc,
                                        gbc_psram=gbc_psram, genesis_pal=genesis_pal, reuse=True)
    except (OSError, KeyError, TypeError, tarfile.TarError, zipfile.BadZipFile,
            zlib.error, RuntimeError) as error:
        raise ValueError("Invalid FPGA artifact: " + str(error)) from error
    result = dict(build["identity"], directory=str(Path(directory).resolve()), machine=machine,
                  files=build["record"]["files"], timing=build["timing"],
                  ps7_init_sha256=build["ps7_init_sha256"],
                  bit=describe(build["bit"]), xsa=describe(build["xsa"]))
    if require_current_inputs:
        require_compatible_inputs(result)
    return result
