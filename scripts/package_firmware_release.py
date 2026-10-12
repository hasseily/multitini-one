#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package named ARM firmware with explicit, unchanged FPGA/core inputs.

The baseline directory must have its complete manifest and SHA256SUMS.txt.
The fresh Vitis workspace must record its verified CPU images and build inputs.
No FPGA build, hardware write, discovery or fallback is performed.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import json
from pathlib import Path, PurePosixPath
import re
import tempfile
import zipfile

import audit_vectrex_package as payload
import image_manifest
import package_cpu1_video_test as p

ROOT = p.ROOT
CORE_BITS = {
    "c64": {"pal": "c64-pal"}, "gbc": {"native": "gbc"},
    "vectrex": {"native": "vectrex"},
    "genesis": {"ntsc": "genesis-ntsc", "pal": "genesis-pal"},
}
CPU_INPUTS = {"cpu0_elf": "diagnostic.elf", "cpu1_elf": "video_worker.elf",
              "cpu1_binary": "video_core1_blob.bin"}


def safe(name):
    path = PurePosixPath(name)
    p.require(path.parts and not path.is_absolute() and ".." not in path.parts and
              "\\" not in name and ":" not in name, "Unsafe package member: " + name)
    return name


def inventory(directory):
    return {path.relative_to(directory).as_posix(): p.describe(path)
            for path in sorted(directory.rglob("*")) if path.is_file()}


def verify_directory(directory):
    manifest = json.loads((directory / "manifest.json").read_text())
    files = manifest["files"]
    p.require(set(inventory(directory)) == set(files) | {"manifest.json", "SHA256SUMS.txt"},
              "Package directory membership differs from manifest")
    for name, record in files.items():
        p.require(p.describe(directory / safe(name)) == record, "Package input changed: " + name)
    lines = (directory / "SHA256SUMS.txt").read_text().splitlines()
    sums = {safe(name): digest for digest, name in [line.split("  ", 1) for line in lines]}
    p.require(len(sums) == len(lines) and set(sums) == set(files) | {"manifest.json"},
              "Package checksum membership differs")
    p.require(all(p.sha(directory / name) == digest for name, digest in sums.items()),
              "Package checksum mismatch")
    return manifest


def verify_source_archive(directory):
    source = json.loads((directory / "source-manifest.json").read_text())
    with zipfile.ZipFile(directory / "source.zip") as archive:
        names = archive.namelist()
        p.require(len(names) == len(set(names)) and set(names) == set(source) and
                  archive.testzip() is None, "Source archive membership/integrity differs")
        for name, digest in source.items():
            p.require(p.digest(archive.read(safe(name))) == digest, "Source archive differs: " + name)
    return source


def retained_members(manifest):
    retained = {}
    for name in manifest["files"]:
        if name.startswith("inputs/") and name not in {"inputs/" + n for n in CPU_INPUTS.values()}:
            retained[name] = name
        elif name.startswith("sd/multitini/cores/"):
            retained[name] = name
        elif name.startswith("evidence/"):
            retained["evidence/baseline/" + name.removeprefix("evidence/")] = name
    for name in ("source.zip", "source-manifest.json", "manifest.json", "SHA256SUMS.txt", "README.txt"):
        retained["evidence/baseline/" + name] = name
    return retained


def verify_cpu(workspace, baseline, version, build_id, abi_minor):
    identity = ROOT / "firmware/include/multitini_version.h"
    p.require(re.search(r'MT_FIRMWARE_VERSION\s+"' + re.escape(version) + '"', identity.read_text()),
              "Firmware source version mismatch")
    cpu = p.verify_cpu_images(workspace, build_id=build_id, abi_minor=abi_minor,
                              identity_source=identity)
    p.require(p.sha(cpu["fsbl"]) == p.sha(baseline / "inputs/fsbl.elf") and
              p.sha(cpu["xsa"]) == p.sha(baseline / "inputs/platform.xsa"),
              "New applications must reuse the baseline FSBL and XSA")
    old_platform = json.loads((baseline / "evidence/vitis/platform-manifest.json").read_text())
    p.require(cpu["record"]["platform_files"] == old_platform,
              "New applications must reuse the unchanged exported platform")
    p.require(cpu["paths"]["cpu1_binary"].read_bytes() == (baseline / "inputs/video_core1_blob.bin").read_bytes(),
              "CPU1 executable payload changed in an ARM CPU0-only release")
    previous = json.loads((baseline / "evidence/vitis/cpu1_manifest.json").read_text())
    p.require(cpu["record"]["compiled"]["video_worker"]["files"] ==
              previous["build_inputs"]["compiled"]["video_worker"]["files"],
              "CPU1 compiled source changed")
    if "retained_cpu1" in cpu["record"]:
        p.require(Path(cpu["record"]["retained_cpu1"]["baseline_path"]).resolve() == baseline.resolve(),
                  "Retained CPU1 selected a different baseline")
    return cpu


def verify_hardware_source(old, new):
    hardware = lambda files: {n: d for n, d in files.items()
                             if n.startswith(("hdl/", "constraints/", "spec/"))}
    p.require(hardware(old) == hardware(new), "HDL, constraints or interface source changed")


def audit_payload(out, cpu, version, build_id, hardware_id, vitis):
    firmware = out / ("FIRMWARE-" + version + ".BIN")
    p.require(firmware.read_bytes() == (out / "sd/FIRMWARE.BIN").read_bytes(),
              "Updater firmware copy differs")
    update = image_manifest.verify_image(firmware, expected_role=image_manifest.ROLE_FIRMWARE,
                                         expected_recovery=True)
    p.require(firmware.stat().st_size <= 0x00DF0000, "Firmware exceeds updater slot")
    for name, filename in CPU_INPUTS.items():
        p.require(p.sha(cpu["paths"][name]) == p.sha(out / "inputs" / filename),
                  "Packaged CPU input differs: " + filename)
    p.require(p.elf_section(out / "inputs/diagnostic.elf", ".mt_video_core1") ==
              (out / "inputs/video_core1_blob.bin").read_bytes(), "Embedded CPU1 payload differs")
    with tempfile.TemporaryDirectory(prefix="firmware-release-audit-", dir=ROOT / "build") as temporary:
        scratch = Path(temporary)
        raw = scratch / "firmware.raw.bin"
        raw.write_bytes(firmware.read_bytes()[:update.payload_size])
        images = payload.parse_firmware(raw.read_bytes())
        p.require(len(images) == 3, "Expected exactly FSBL, supervisor and CPU0 images")
        bootgen = vitis / "bin/bootgen.bat"
        objcopy = vitis / "gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-objcopy.exe"
        p.run([objcopy, "-O", "binary", out / "inputs/video_worker.elf", scratch / "worker.bin"])
        p.require((scratch / "worker.bin").read_bytes() == (out / "inputs/video_core1_blob.bin").read_bytes(),
                  "CPU1 ELF/raw image mismatch")
        supervisor, _ = payload.process_bit(bootgen, out / "inputs/supervisor.bit", scratch, "supervisor")
        p.require(len(images[1]) == 1 and images[1][0]["payload"] == supervisor and
                  images[1][0]["attributes"] == 0x20 and
                  images[1][0]["load_address"] == images[1][0]["entry_address"] == 0,
                  "Supervisor partition differs from retained bitstream")
        cores = {}
        for core, variants in CORE_BITS.items():
            cores[core] = p.verify_core(out / "sd/multitini/cores" / core, set(variants))
            for variant, machine in variants.items():
                processed, _ = payload.process_bit(bootgen, out / "inputs" / (machine + ".bit"), scratch, machine)
                p.require(p.digest(processed) == cores[core][variant]["sha256"],
                          "Retained core payload differs from selected bitstream")
        audit = {"status": "PASS", "firmware_version": version, "cpu_build_id": "0x" + build_id,
                "hardware_build_id": hardware_id, "image_count": len(images),
                "partition_counts": [len(parts) for parts in images],
                "fsbl": payload.verify_elf(images[0], out / "inputs/fsbl.elf"),
                "cpu0": payload.verify_elf(images[2], out / "inputs/diagnostic.elf"),
                "cpu1_embedded_sha256": p.sha(out / "inputs/video_core1_blob.bin"),
                "supervisor_processed_sha256": p.digest(supervisor), "retained_cores": cores,
                "update_manifest": asdict(update), "raw_bootgen_sha256": p.sha(raw)}
        if "retained_cpu1" in cpu["record"]:
            retained = cpu["record"]["retained_cpu1"]
            directory = out / "evidence/vitis/video_worker"
            rebuilt_elf, rebuilt_binary = directory / "rebuilt_video_worker.elf", directory / "rebuilt_video_worker.bin"
            for name, path in (("rebuilt_elf", rebuilt_elf), ("rebuilt_binary", rebuilt_binary)):
                p.require(p.describe(path) == {k: retained[name][k] for k in ("bytes", "sha256")},
                          "Packaged CPU1 rebuilt comparison input differs")
            p.run([objcopy, "-O", "binary", rebuilt_elf, scratch / "rebuilt-worker.bin"])
            p.require((scratch / "rebuilt-worker.bin").read_bytes() == rebuilt_binary.read_bytes(),
                      "Rebuilt CPU1 comparison ELF/raw differs")
            qualification = p.compare_cpu1_build_id(out / "inputs/video_worker.elf", out / "inputs/video_core1_blob.bin",
                                                   rebuilt_elf, rebuilt_binary)
            p.require(qualification == retained["qualification"], "CPU1 build-ID qualification differs")
            audit["retained_cpu1"] = {"selected_elf": p.describe(out / "inputs/video_worker.elf"),
                                     "rebuilt_elf": p.describe(rebuilt_elf), "qualification": qualification}
        return audit


def complete_check(out, baseline, old, cpu, args, compare_zip=True):
    manifest = verify_directory(out)
    p.require(manifest["firmware_version"] == args.version and
              manifest["firmware_id"] == "0x" + args.build_id and
              manifest["hardware_firmware_id"] == old.get("hardware_firmware_id", old["firmware_id"]) and
              manifest["abi"] == old["abi"] and manifest["provenance"]["baseline_name"] == baseline.name and
              all(manifest["provenance"][name] is False for name in
                  ("cores_rebuilt", "supervisor_rebuilt", "cpu1_payload_changed", "board_validation")) and
              manifest["provenance"]["baseline_manifest_sha256"] == p.sha(baseline / "manifest.json"),
              "Firmware/baseline identity differs")
    retained = retained_members(old)
    p.require(set(retained) == set(manifest["provenance"]["retained_files"]), "Retained file set differs")
    for target, original in retained.items():
        p.require(p.sha(out / target) == p.sha(baseline / original) and
                  manifest["provenance"]["retained_files"][target] ==
                  {"baseline_path": original, **p.describe(baseline / original)},
                  "Retained baseline file differs: " + target)
    source = verify_source_archive(out)
    p.verify_sources(source)
    p.require(all(source.get(n) == d for n, d in cpu["record"]["files"].items()),
              "Source snapshot differs from CPU build capture")
    verify_hardware_source(verify_source_archive(baseline), source)
    audit = audit_payload(out, cpu, args.version, args.build_id, manifest["hardware_firmware_id"], args.vitis)
    p.require(audit == json.loads((out / "evidence/payload-audit.json").read_text()),
              "Firmware payload audit differs")
    if compare_zip:
        with zipfile.ZipFile(Path(str(out) + ".zip")) as archive:
            files = inventory(out)
            names = archive.namelist()
            p.require(len(names) == len(set(names)) and set(names) == set(files) and archive.testzip() is None,
                      "Release ZIP membership/integrity differs")
            p.require(all(p.digest(archive.read(name)) == record["sha256"] for name, record in files.items()),
                      "Release ZIP differs from directory")
    return {"status": "PASS", "firmware_version": args.version,
            "firmware": p.describe(out / ("FIRMWARE-" + args.version + ".BIN")),
            "bundle": p.describe(Path(str(out) + ".zip")) if compare_zip else None,
            "output": str(out), "cores_rebuilt": False, "board_validation": False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--workspace", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--build-id", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--vitis", type=Path, default=Path("C:/Xilinx/2025.2/Vitis"))
    parser.add_argument("--evidence", type=Path, action="append", default=[])
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    p.require(re.fullmatch(r"M\d+\.\d+\.\d+", args.version), "Expected Multitini version Mx.y.z")
    p.require(re.fullmatch(r"[0-9A-Fa-f]{8}", args.build_id), "Build ID must contain eight hexadecimal digits")
    args.build_id = args.build_id.upper()
    baseline, workspace, out, args.vitis = (path.resolve() for path in
                                           (args.baseline, args.workspace, args.output, args.vitis))
    p.require(out.is_relative_to(ROOT / "build") and out != ROOT / "build" and
              not out.is_relative_to(baseline) and not baseline.is_relative_to(out),
              "Output must be a separate child of build/")
    old = verify_directory(baseline)
    p.ABI = old["abi"]
    major, minor = map(int, p.ABI.split("."))
    p.require(major == 0, "Unsupported baseline ABI major")
    old_source = verify_source_archive(baseline)
    cpu = verify_cpu(workspace, baseline, args.version, args.build_id, minor)
    if args.check_only:
        print(json.dumps(complete_check(out, baseline, old, cpu, args), indent=2))
        return 0
    p.require(not out.exists() and not Path(str(out) + ".zip").exists(), "Output or ZIP already exists")
    source = p.source_inventory(cpu["record"]["files"])
    verify_hardware_source(old_source, source)
    for evidence in args.evidence:
        p.require(evidence.is_file(), "Missing requested evidence: " + str(evidence))
    out.mkdir(parents=True)
    retained = {}
    for target, original in retained_members(old).items():
        p.copy(baseline / original, out / target)
        retained[target] = {"baseline_path": original, **p.describe(baseline / original)}
    for name, filename in CPU_INPUTS.items():
        p.copy(cpu["paths"][name], out / "inputs" / filename)
    p.copy(workspace / "cpu1_manifest.json", out / "evidence/vitis/cpu1_manifest.json")
    p.write_json(out / "evidence/vitis/platform-manifest.json", cpu["record"]["platform_files"])
    for app in ("diagnostic", "video_worker"):
        for name in ("CMakeLists.txt", "UserConfig.cmake", "lscript.ld", "app.yaml"):
            p.copy(workspace / app / "src" / name, out / "evidence/vitis" / app / name)
        p.copy(workspace / app / "build/compile_commands.json",
               out / "evidence/vitis" / app / "compile_commands.json")
    p.copy(workspace / "diagnostic/src/video_core1_blob.S", out / "evidence/vitis/diagnostic/video_core1_blob.S")
    if "retained_cpu1" in cpu["record"]:
        retained_cpu1 = cpu["record"]["retained_cpu1"]
        for name, filename in (("rebuilt_elf", "rebuilt_video_worker.elf"), ("rebuilt_binary", "rebuilt_video_worker.bin")):
            p.copy(Path(retained_cpu1[name]["path"]), out / "evidence/vitis/video_worker" / filename)
        p.write_json(out / "evidence/vitis/retained-cpu1.json", retained_cpu1)
    for index, path in enumerate(args.evidence):
        p.copy(path, out / "evidence/validation" / f"{index:02d}-{path.name}")
    p.archive_files(out / "source.zip", {name: ROOT / name for name in source})
    p.write_json(out / "source-manifest.json", source)
    firmware = out / ("FIRMWARE-" + args.version + ".BIN")
    p.run([ROOT / "scripts/make_firmware_bin.bat", firmware, out / "inputs/fsbl.elf",
           out / "inputs/supervisor.bit", out / "inputs/diagnostic.elf"], vitis=args.vitis)
    p.copy(firmware, out / "sd/FIRMWARE.BIN")
    hardware_id = old.get("hardware_firmware_id", old["firmware_id"])
    p.write_json(out / "evidence/payload-audit.json",
                 audit_payload(out, cpu, args.version, args.build_id, hardware_id, args.vitis))
    (out / "README.txt").write_text(
        f"Multitini Firmware {args.version} / ARM build 0x{args.build_id} / ABI {p.ABI}\n\n"
        "Hold SELECT + START together for 0.5 seconds to open or close the menu.\n"
        "Release both buttons before using the gesture again. F12 remains available.\n"
        "In menus: D-pad navigates, A confirms, B goes back/closes, L/R changes tabs.\n\n"
        f"FIRMWARE-{args.version}.BIN and sd/FIRMWARE.BIN are identical.\n"
        "Copy sd/FIRMWARE.BIN to the SD root and use the existing golden updater.\n"
        "Installed baseline cores can remain; all included core files are retained byte for byte.\n"
        "Preserve user media and settings. No FPGA rebuild or hardware write was performed.\n"
        f"Supervisor and machine FPGA build {hardware_id} are unchanged.\n"
        "The CPU1 executable payload and exported Vitis platform are unchanged.\n"
        "A comparison rebuild qualifies the selected original CPU1 ELF; its GNU build-ID may differ.\n"
        "source.zip records the new ARM software and packaging source.\n"
        "evidence/baseline/source.zip and retained evidence preserve exact prior FPGA build inputs.\n"
        "Manifest, checksums and independent Bootgen/ELF/core audits record selected inputs.\n"
        "Vitis/Bootgen 2025.2. No user ROMs/media are supplied; board validation is pending.\n",
        encoding="utf-8")
    p.verify_sources(source)
    verify_directory(baseline)
    verify_cpu(workspace, baseline, args.version, args.build_id, minor)
    p.write_json(out / "manifest.json", {
        "schema": "multitini-named-firmware-package-v1", "classification": "Board acceptance pending",
        "created_utc": datetime.now(timezone.utc).isoformat(), "firmware_version": args.version,
        "firmware_id": "0x" + args.build_id, "hardware_firmware_id": hardware_id, "abi": p.ABI,
        "git_head": p.git("rev-parse", "HEAD").strip(),
        "dirty_source_snapshot": bool(p.git("status", "--porcelain").strip()), "source_files": len(source),
        "provenance": {"baseline_name": baseline.name, "baseline_manifest_sha256": p.sha(baseline / "manifest.json"),
                       "cpu_source_archive": "source.zip", "baseline_source_archive": "evidence/baseline/source.zip",
                       "cores_rebuilt": False, "supervisor_rebuilt": False, "cpu1_payload_changed": False,
                       "board_validation": False, "retained_files": retained}, "files": inventory(out)})
    checksums = inventory(out)
    (out / "SHA256SUMS.txt").write_text("".join(f"{r['sha256']}  {n}\n" for n, r in checksums.items()), encoding="ascii")
    complete_check(out, baseline, old, cpu, args, compare_zip=False)
    p.archive_files(Path(str(out) + ".zip"), {n: out / n for n in inventory(out)})
    print(json.dumps(complete_check(out, baseline, old, cpu, args), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
