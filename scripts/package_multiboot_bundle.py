#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle explicit Multitini, Appletini and shared golden update images.

No flash operation or combined raw flash image is produced. Creation requires
complete selected firmware packages and the exact Multitini golden workspace.
Check-only validation uses the delivered directory/ZIP and its archived inputs.
"""
from __future__ import annotations

import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import tempfile
import zipfile

import audit_vectrex_package as payload
import image_manifest
import package_cpu1_video_test as p
import package_firmware_release as release

ROOT = p.ROOT
SCHEMA = "multiboot-dual-firmware-bundle-v1"
GOLDEN_MARKER = b"APPLETINI-MULTIBOOT-GOLDEN-V1"
SD_ROOT_FIRMWARE = {
    "appletini": "sd-root/FIRMWARE-APPLETINI.BIN",
    "multitini": "sd-root/FIRMWARE-MULTITINI.BIN",
}
LAYOUT = {
    "flash_bytes": 0x1000000,
    "golden_primary": {"offset": 0, "capacity": 0x100000},
    "golden_trial": {"offset": 0x100000, "capacity": 0x100000},
    "appletini": {"offset": 0x200000, "capacity": 0x600000},
    "multitini": {"offset": 0x800000, "capacity": 0x7E0000},
    "metadata_sectors": [0xFE0000, 0xFF0000],
}
SUPPORT_SOURCES = (
    "scripts/package_multiboot_bundle.py", "scripts/make_boot_bin.bat",
    "scripts/image_manifest.py", "scripts/audit_vectrex_package.py",
    "scripts/package_firmware_release.py", "scripts/package_cpu1_video_test.py",
    "scripts/run_native_tool.py",
)


def load(path):
    return json.loads(Path(path).read_text(encoding="utf-8"))


def verify_archive(path, sources):
    sources = sources.get("files", sources)
    p.require(sources, "Empty source/platform archive manifest")
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        p.require(len(names) == len(set(names)) and set(names) == set(sources) and
                  archive.testzip() is None, "Archive membership/integrity differs: " + str(path))
        for name, expected in sources.items():
            data = archive.read(release.safe(name))
            digest = expected if isinstance(expected, str) else expected["sha256"]
            p.require(p.digest(data) == digest and
                      (isinstance(expected, str) or len(data) == expected["bytes"]),
                      "Archive content differs: " + name)


def source_hashes(manifest):
    return {name: value if isinstance(value, str) else value["sha256"]
            for name, value in manifest.get("files", manifest).items()}


def verify_native_package(directory):
    """Appletini's native ARM builder hashes every member; some versions omit
    the manifest itself from SHA256SUMS. Accept either exact documented set."""
    manifest = load(directory / "manifest.json")
    files = manifest["files"]
    p.require(set(release.inventory(directory)) == set(files) | {"manifest.json", "SHA256SUMS.txt"},
              "Appletini package membership differs from manifest")
    for name, expected in files.items():
        p.require(p.describe(directory / release.safe(name)) == expected, "Appletini package input changed: " + name)
    lines = (directory / "SHA256SUMS.txt").read_text().splitlines()
    sums = {release.safe(name): digest for digest, name in [line.split("  ", 1) for line in lines]}
    p.require(len(sums) == len(lines) and set(sums) in (set(files), set(files) | {"manifest.json"}) and
              all(p.sha(directory / name) == digest for name, digest in sums.items()),
              "Appletini package checksum membership/content differs")
    return manifest


def record_matches(path, record):
    return p.sha(path) == record["sha256"] and path.stat().st_size == record.get("bytes", record.get("size_bytes"))


def verify_appletini_build(directory, sources):
    report = load(directory / "ARM_BUILD.json")
    p.require(report["hardware_flashed"] is False and report["hardware_tested"] is False,
              "Appletini ARM report overstates board validation")
    p.require(record_matches(directory / "FIRMWARE.BIN", report["firmware"]) and
              record_matches(directory / "cpu-sources.zip", report["cpu_source_archive"]) and
              record_matches(directory / "hardware-sources.zip", report["hardware_source_archive"]),
              "Appletini firmware/source selection differs from build report")
    for name, selected in report["inputs"].items():
        p.require(record_matches(directory / "inputs" / release.safe(name), selected),
                  "Appletini selected input changed: " + name)
    required = {"fsbl.elf", "frontend.elf", "frontend_core1.elf", "core1_blob.bin",
                "appletini_yarz_top.bit", "appletini_yarz_top.xsa"}
    p.require(required <= set(report["inputs"]), "Appletini build lacks required selected inputs")
    snapshot = report["source_snapshot"]
    hashes = source_hashes(sources)
    p.require(snapshot["before_equals_after"] is True and snapshot["files"] == hashes and
              load(directory / "build-records/source-before.json") == hashes and
              load(directory / "build-records/source-after.json") == hashes,
              "Appletini source changed during its build or archive differs")
    p.require(set(report["compiled_sources"]) == {"frontend", "bootloader"},
              "Appletini compiler source audit is incomplete")
    for app, declared in report["compiled_sources"].items():
        by_path = {str(item["path"]).replace("\\", "/"): item for item in declared}
        p.require(len(by_path) == len(declared) and by_path, "Duplicated/empty Appletini compiled source list")
        for path, item in by_path.items():
            matches = [name for name in hashes if path.endswith("/" + name)]
            if path.endswith("/core1_blob.S"):
                p.require(record_matches(directory / "build-records/frontend-core1_blob.S", item),
                          "Appletini generated CPU1 wrapper changed")
            else:
                p.require(len(matches) == 1 and hashes[matches[0]] == item["sha256"],
                          "Appletini compiled source differs from archive: " + path)
        commands = load(directory / "build-records" / (app + "-compile_commands.json"))
        actual = set()
        for command in commands:
            path = str(command["file"]).replace("\\", "/")
            p.require(path in by_path, "Unrecorded Appletini compiler input: " + path)
            text = command.get("command", " ".join(command.get("arguments", [])))
            assembly = Path(path).suffix.lower() in {".s", ".asm"}
            p.require(assembly or ("-O2" in text and "-O0" not in text and
                      (app != "frontend" or "-mfpu=neon" in text)), "Appletini compiler optimization differs")
            actual.add(path)
        p.require(actual == set(by_path), "Appletini actual compiler source membership differs")
    cpu1 = report["cpu1"]
    p.require(cpu1["size_word_matches"] is True and cpu1["elf_raw_matches"] is True and
              cpu1["elf_sha256"] == p.sha(directory / "inputs/frontend_core1.elf") and
              cpu1["raw_sha256"] == cpu1["embedded_sha256"] == p.sha(directory / "inputs/core1_blob.bin") and
              cpu1["embedded_bytes"] == (directory / "inputs/core1_blob.bin").stat().st_size,
              "Appletini retained CPU1 selection/embedding differs")
    return report


def audit_appletini(directory, update):
    fresh = load(directory / "FIRMWARE_AUDIT.json")
    retained = load(directory / "retained-d1/FIRMWARE_AUDIT.json")
    p.require(fresh["status"] == retained["status"] == "PASS" and fresh["manifest"] == asdict(update),
              "Appletini payload audit is missing, failed or describes another image")
    for key, name in (("firmware", "FIRMWARE.BIN"), ("frontend", "inputs/frontend.elf"),
                      ("fsbl", "inputs/fsbl.elf"), ("bitstream", "inputs/appletini_yarz_top.bit"),
                      ("xsa", "inputs/appletini_yarz_top.xsa")):
        p.require(record_matches(directory / name, fresh["inputs"][key]),
                  "Appletini fresh audit selected a different input: " + name)
    for key, name in (("bitstream", "inputs/appletini_yarz_top.bit"), ("xsa", "inputs/appletini_yarz_top.xsa")):
        p.require(record_matches(directory / name, retained["inputs"][key]),
                  "Appletini retained hardware differs from d1: " + name)
    raw = (directory / "FIRMWARE.BIN").read_bytes()[:update.payload_size]
    images = payload.parse_firmware(raw)
    payload.verify_elf(images[0], directory / "inputs/fsbl.elf")
    payload.verify_elf(images[2], directory / "inputs/frontend.elf")
    bit = images[1][0]
    old_bits = [part for part in retained["partitions"] if part["attributes"] == 0x20]
    p.require(len(old_bits) == 1 and bit["attributes"] == 0x20 and
              bit["load_address"] == bit["entry_address"] == 0 and
              bit["sha256"] == old_bits[0]["sha256"] and bit["bytes"] == old_bits[0]["length"],
              "Appletini packaged FPGA bytes differ from retained d1")
    core = fresh["core1"]
    p.require(core["checked"] is True and core["frontend_size_word_matches"] is True and
              core["blob_sha256"] == p.sha(directory / "inputs/core1_blob.bin"),
              "Appletini fresh audit lacks CPU1 embedding proof")
    blob = (directory / "inputs/core1_blob.bin").read_bytes()
    address = core["blob_address"]
    contained = [part for part in images[2] if part["load_address"] <= address and
                 address + len(blob) <= part["load_address"] + part["bytes"]]
    p.require(len(contained) == 1 and
              contained[0]["payload"][address-contained[0]["load_address"]:address-contained[0]["load_address"]+len(blob)] == blob,
              "Appletini actual packaged CPU1 blob differs")


def parse_golden(data):
    """Strict two-ELF variant of audit_vectrex_package.parse_firmware.

    Use that auditor's word decoder and unchanged ELF/PT_LOAD comparison. A
    golden BIF has FSBL and golden application, with no FPGA BIT partition.
    """
    words = payload.words
    p.require(words(data, 0x20, 2) == (0xAA995566, 0x584C4E58), "Not a Zynq golden image")
    p.require(sum(words(data, 0x20, 11)) & 0xFFFFFFFF == 0xFFFFFFFF,
              "Golden boot-header checksum mismatch")
    p.require(words(data, 0x28, 2) == (0, 0x01010000), "Encrypted/unsupported golden image")
    iht_offset, pht_offset = words(data, 0x98, 2)
    p.require(iht_offset >= 0xA0 and iht_offset % 4 == pht_offset % 4 == 0,
              "Invalid golden header-table placement")
    iht = words(data, iht_offset, 5)
    p.require(iht[0] == 0x01020000 and iht[4] == 0 and iht[2] * 4 == pht_offset,
              "Unsupported or inconsistent golden image table")
    p.require(2 <= iht[1] <= 14, "Unexpected golden partition count")
    image_offset = iht[3] * 4
    images, seen, occupied = [], set(), []
    total = 0
    while image_offset:
        p.require(image_offset not in seen and len(images) < 2, "Extra/cyclic golden image header")
        seen.add(image_offset)
        ih = words(data, image_offset, 4)
        p.require(1 <= ih[3] <= 14, "Invalid golden image partition count")
        partitions = []
        for index in range(ih[3]):
            ph_offset = ih[1] * 4 + index * 64
            p.require(ph_offset == pht_offset + total * 64, "Golden partition order differs from BIF")
            ph = words(data, ph_offset, 16)
            p.require(sum(ph) & 0xFFFFFFFF == 0xFFFFFFFF, "Golden partition checksum mismatch")
            p.require(ph[0] == ph[1] == ph[2] and ph[1] != 0,
                      "Compressed/encrypted/empty golden partition")
            p.require(ph[8] == ph[10] == 0 and ph[9] * 4 == image_offset,
                      "Authenticated/checksummed/mislinked golden partition")
            p.require(ph[7] == (ih[3] if index == 0 else 0) and not any(ph[11:15]),
                      "Unexpected golden partition header fields")
            p.require(ph[6] == 0x10, "Golden must contain ARM partitions only")
            length, offset = ph[1] * 4, ph[5] * 4
            p.require(offset >= pht_offset + (iht[1] + 1) * 64 and offset + length <= len(data),
                      "Golden partition outside image")
            p.require(all(offset + length <= a or offset >= b for a, b in occupied),
                      "Overlapping golden partition payloads")
            occupied.append((offset, offset + length))
            part = data[offset:offset + length]
            partitions.append({"offset": offset, "bytes": length,
                               "load_address": ph[3], "entry_address": ph[4],
                               "attributes": ph[6], "sha256": p.digest(part), "payload": part})
            total += 1
        images.append(partitions)
        image_offset = ih[0] * 4
    p.require(len(images) == 2 and total == iht[1] and len(images[0]) == 1,
              "Expected exactly FSBL and golden ELF images")
    p.require(words(data, pht_offset + total * 64, 16) == (0,) * 15 + (0xFFFFFFFF,),
              "Golden partition sentinel differs")
    p.require(max(end for _, end in occupied) == len(data), "Bytes after final golden partition")
    fsbl = images[0][0]
    p.require(words(data, 0x30, 5) == (fsbl["offset"], fsbl["bytes"], fsbl["load_address"],
                                      fsbl["entry_address"], fsbl["bytes"]),
              "Golden boot-header FSBL fields differ")
    return images


def audit_golden(directory):
    firmware = directory / "BOOT.BIN"
    update = image_manifest.verify_image(firmware, expected_role=image_manifest.ROLE_GOLDEN,
                                         expected_recovery=True)
    p.require(update.version == 1 and firmware.stat().st_size <= 0x100000,
              "Golden requires manifest v1 and the 1 MiB slot bound")
    raw = firmware.read_bytes()[:update.payload_size]
    p.require(GOLDEN_MARKER in raw, "Golden lacks its linked multiboot capability")
    p.require(not any(marker in raw for marker in image_manifest.PRODUCT_MARKER.values()),
              "Golden contains a product runtime identity")
    images = parse_golden(raw)
    fsbl = payload.verify_elf(images[0], directory / "inputs/fsbl.elf")
    golden = payload.verify_elf(images[1], directory / "inputs/golden.elf")
    fsbl["path"], golden["path"] = "golden/inputs/fsbl.elf", "golden/inputs/golden.elf"
    p.require(0x100000 <= golden["entry_address"] < 0x20000000 and
              all(0x100000 <= item["load_address"] and
                  item["load_address"] + item["memory_bytes"] <= 0x20000000
                  for item in golden["pt_load"]), "Golden ELF overlaps reserved DDR")
    return {"status": "PASS", "image_count": 2, "partition_counts": list(map(len, images)),
            "update_manifest": asdict(update), "raw_bootgen_sha256": p.digest(raw),
            "fsbl": fsbl, "golden": golden}


def verify_compilation(record, commands_path, sources, source_root=None):
    p.require(set(record["compiled"]) == {"golden"}, "Missing/extra golden compiler audit")
    compiled = record["compiled"]["golden"]
    p.require(p.sha(commands_path) == compiled["compile_commands_sha256"],
              "Golden compiler commands changed")
    commands = load(commands_path)
    actual = set()
    for command in commands:
        path = str(command["file"]).replace("\\", "/")
        matches = [name for name in compiled["files"] if path == name or path.endswith("/" + name)]
        p.require(len(matches) == 1, "Golden compiler input is outside its captured sources: " + path)
        name = matches[0]
        if source_root is not None:
            expected = str(source_root).replace("\\", "/").rstrip("/") + "/" + name
            p.require(path.casefold() == expected.casefold() if ":" in expected else path == expected,
                      "Golden compiler selected a different source root: " + path)
        p.require(sources.get(name) == compiled["files"][name] == record["files"].get(name),
                  "Golden compiled source changed: " + name)
        text = command.get("command", " ".join(command.get("arguments", [])))
        flags = text.replace("\\", "/")
        platform = str(record["platform_path"]).replace("\\", "/")
        p.require(not any(flag in flags for flag in ("MT_VIDEO_WORKER_HOST_TEST", "-O0")) and
                  "-O2" in flags and "-mfpu=neon" in flags and platform.casefold() in flags.casefold(),
                  "Golden compiler platform/optimization differs or enables host tests")
        actual.add(name)
    p.require(actual and actual == set(compiled["files"]), "Golden compiler source membership differs")


def verify_golden_build(workspace, multitini):
    p.require(workspace.is_relative_to(ROOT / "build") and workspace != ROOT / "build",
              "Golden workspace must be an explicit child of this checkout's build directory")
    record = load(workspace / "golden_build_inputs.json")
    p.require(record.get("schema") == 1, "Unsupported golden build record")
    p.verify_sources(record["files"])
    platform = Path(record["platform_path"]).resolve()
    p.require(platform.is_relative_to(ROOT / "build"), "Golden platform is outside this checkout")
    actual = {path.relative_to(platform).as_posix(): p.sha(path)
              for path in platform.rglob("*") if path.is_file()}
    p.require(actual == record["platform_files"], "Golden exported platform changed")
    fsbl = multitini / "inputs/fsbl.elf"
    xsa = Path(record["xsa"]["path"]).resolve()
    p.require(p.sha(xsa) == record["xsa"]["sha256"] == p.sha(multitini / "inputs/platform.xsa"),
              "Golden selected XSA differs from Multitini package")
    p.require(p.sha(platform / "sw/boot/fsbl.elf") == p.sha(fsbl), "Golden platform FSBL differs")
    commands = workspace / "golden/build/compile_commands.json"
    verify_compilation(record, commands, record["files"], ROOT)
    for name in ("CMakeLists.txt", "UserConfig.cmake", "lscript.ld", "app.yaml"):
        p.require((workspace / "golden/src" / name).is_file(), "Missing golden build setting: " + name)
    elf = workspace / "golden/build/golden.elf"
    p.require(elf.is_file(), "Explicit golden ELF is missing")
    _, entry, segments = payload.elf_segments(elf)
    p.require(0x100000 <= entry < 0x20000000 and
              all(0x100000 <= part["load_address"] and
                  part["load_address"] + part["memory_bytes"] <= 0x20000000 for part in segments),
              "Selected golden ELF overlaps reserved DDR")
    p.require(any(GOLDEN_MARKER in part["payload"] for part in segments) and
              any(image_manifest.RECOVERY_MARKER in part["payload"] for part in segments),
              "Golden ELF lacks linked multiboot/recovery capabilities")
    return record, platform, elf


def verify_component(directory, product):
    if product == "multitini":
        manifest = release.verify_directory(directory)
        p.require(manifest.get("schema") == "multitini-named-firmware-package-v1",
                  "Expected a complete named Multitini firmware package")
        source = release.verify_source_archive(directory)
        firmware = directory / ("FIRMWARE-" + manifest["firmware_version"] + ".BIN")
        p.require(firmware.read_bytes() == (directory / "sd/FIRMWARE.BIN").read_bytes(),
                  "Multitini SD firmware copy differs")
    else:
        manifest = verify_native_package(directory)
        p.require(manifest.get("schema") in (None, "appletini-multiboot-firmware-package-v1"),
                  "Unsupported Appletini package schema")
        source = load(directory / "source-manifest.json")
        verify_archive(directory / "cpu-sources.zip", source)
        verify_archive(directory / "hardware-sources.zip", load(directory / "hardware-source-manifest.json"))
        firmware = directory / "FIRMWARE.BIN"
        p.require((directory / "ARM_BUILD.json").is_file() and
                  (directory / "FIRMWARE_AUDIT.json").is_file(), "Appletini build/payload evidence is missing")
        report = verify_appletini_build(directory, source)
        manifest["firmware_version"] = report["version"]
    update = image_manifest.verify_image(firmware, expected_role=image_manifest.ROLE_FIRMWARE,
                                         expected_recovery=True, expected_product=product)
    p.require(update.version == 2 and firmware.stat().st_size <= LAYOUT[product]["capacity"],
              "Firmware must be tagged v2 and fit its product slot")
    if product == "appletini":
        audit_appletini(directory, update)
    return manifest, source, firmware


def verify_sd_root_firmware(directory, firmwares, required=False):
    # Previous bundles did not include this convenience directory. If present,
    # both aliases must independently validate and match the selected inputs.
    if not required and not (directory / "sd-root").exists():
        return
    for product, name in SD_ROOT_FIRMWARE.items():
        alias = directory / name
        p.require(alias.is_file(), "Missing SD-root firmware alias: " + name)
        image_manifest.verify_image(alias, expected_role=image_manifest.ROLE_FIRMWARE,
                                    expected_recovery=True, expected_product=product)
        p.require(alias.read_bytes() == firmwares[product].read_bytes(),
                  "SD-root firmware alias differs from selected component: " + product)


def verify_directory(directory, compare_zip=False):
    manifest = release.verify_directory(directory)
    p.require(manifest.get("schema") == SCHEMA and manifest["layout"] == LAYOUT,
              "Unsupported multiboot bundle or flash layout")
    p.require(manifest["flash_performed"] is False and manifest["combined_raw_flash_image"] is False and
              manifest["board_validation"] is False, "Bundle overstates hardware validation")
    firmwares = {}
    for product in ("multitini", "appletini"):
        component, _, firmware = verify_component(directory / product, product)
        firmwares[product] = firmware
        record = manifest["components"][product]
        p.require(record["package_manifest_sha256"] == p.sha(directory / product / "manifest.json") and
                  record["firmware"] == firmware.relative_to(directory).as_posix() and
                  record["firmware_identity"] == asdict(image_manifest.verify_image(firmware,
                                                          expected_product=product)),
                  "Bundle component identity differs: " + product)
        p.require(record["firmware_version"] == component["firmware_version"], "Component version differs")
    install_files = manifest.get("install_files")
    if install_files is not None:
        p.require(install_files == SD_ROOT_FIRMWARE, "Bundle install-file mapping differs")
    verify_sd_root_firmware(directory, firmwares, required=install_files is not None)
    golden = directory / "golden"
    record = load(golden / "build-records/build_inputs.json")
    selected = load(golden / "build-records/selected-inputs.json")
    sources = load(golden / "source-manifest.json")
    verify_archive(golden / "source.zip", sources)
    p.require(all(sources.get(name) == expected for name, expected in record["files"].items()),
              "Golden source archive differs from build inputs")
    verify_compilation(record, golden / "build-records/compile_commands.json", sources, selected["source_root"])
    p.require(load(golden / "platform-manifest.json") == record["platform_files"],
              "Golden platform archive manifest differs")
    verify_archive(golden / "platform.zip", record["platform_files"])
    p.require(p.describe(golden / "inputs/fsbl.elf") == selected["fsbl"]["artifact"] and
              p.describe(golden / "inputs/golden.elf") == selected["golden_elf"]["artifact"] and
              p.sha(golden / "inputs/fsbl.elf") == p.sha(directory / "multitini/inputs/fsbl.elf") and
              p.sha(directory / "multitini/inputs/platform.xsa") == record["xsa"]["sha256"],
              "Golden selected input differs")
    p.require(selected["packaging_script_sha256"] == sources.get("scripts/make_boot_bin.bat"),
              "Golden packaging source differs from selected input record")
    with zipfile.ZipFile(golden / "platform.zip") as platform:
        p.require(p.digest(platform.read("sw/boot/fsbl.elf")) == p.sha(golden / "inputs/fsbl.elf"),
                  "Archived golden platform FSBL differs")
    audit = audit_golden(golden)
    p.require(audit == load(golden / "payload-audit.json"), "Golden payload audit differs")
    if compare_zip:
        verify_zip(Path(str(directory) + ".zip"), directory)
    return {"status": "PASS", "components": manifest["components"],
            "golden": p.describe(golden / "BOOT.BIN"), "flash_performed": False,
            "board_validation": False}


def verify_zip(path, directory=None):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        p.require(len(names) == len(set(names)) and archive.testzip() is None, "Bundle ZIP is corrupt or duplicated")
        for name in names:
            release.safe(name)
            p.require(not name.endswith("/"), "Bundle ZIP contains an unexpected directory member")
        if directory is not None:
            files = release.inventory(directory)
            p.require(set(names) == set(files) and
                      all(p.digest(archive.read(name)) == item["sha256"] for name, item in files.items()),
                      "Bundle ZIP differs from its directory")
            return
        with tempfile.TemporaryDirectory(prefix="multiboot-check-") as temporary:
            target = Path(temporary)
            for name in names:
                destination = target / name
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(archive.read(name))
            return verify_directory(target)


def validate_output(out, inputs):
    build = (ROOT / "build").resolve()
    p.require(out.is_relative_to(build) and out != build, "Output must be a child of this checkout's build directory")
    for selected in inputs:
        p.require(not out.is_relative_to(selected) and not selected.is_relative_to(out),
                  "Output collides with a selected input directory")
    p.require(not out.exists() and not Path(str(out) + ".zip").exists(), "Output directory or ZIP already exists")


def create_bundle(multitini, workspace, appletini, out, vitis):
    validate_output(out, (multitini, workspace, appletini))
    components = {}
    for product, directory in (("multitini", multitini), ("appletini", appletini)):
        manifest, _, firmware = verify_component(directory, product)
        components[product] = {"selected_package": str(directory), "firmware_version": manifest["firmware_version"],
                               "package_manifest_sha256": p.sha(directory / "manifest.json"),
                               "firmware": product + "/" + firmware.relative_to(directory).as_posix(),
                               "firmware_identity": asdict(image_manifest.verify_image(firmware, expected_product=product))}
    record, platform, elf = verify_golden_build(workspace, multitini)
    p.require((vitis / "bin/bootgen.bat").is_file(), "Selected Vitis Bootgen wrapper is missing")
    out.mkdir(parents=True)
    for product, directory in (("multitini", multitini), ("appletini", appletini)):
        shutil.copytree(directory, out / product)
        p.copy(out / components[product]["firmware"], out / SD_ROOT_FIRMWARE[product])
    verify_sd_root_firmware(out, {product: out / component["firmware"]
                                for product, component in components.items()}, required=True)
    golden = out / "golden"
    p.copy(multitini / "inputs/fsbl.elf", golden / "inputs/fsbl.elf")
    p.copy(elf, golden / "inputs/golden.elf")
    p.copy(workspace / "golden_build_inputs.json", golden / "build-records/build_inputs.json")
    p.copy(workspace / "golden/build/compile_commands.json", golden / "build-records/compile_commands.json")
    for name in ("CMakeLists.txt", "UserConfig.cmake", "lscript.ld", "app.yaml"):
        p.copy(workspace / "golden/src" / name, golden / "build-records" / name)
    sources = {**record["files"], **{name: p.sha(ROOT / name) for name in SUPPORT_SOURCES}}
    p.archive_files(golden / "source.zip", {name: ROOT / name for name in sources})
    p.write_json(golden / "source-manifest.json", sources)
    p.archive_files(golden / "platform.zip", {name: platform / name for name in record["platform_files"]})
    p.write_json(golden / "platform-manifest.json", record["platform_files"])
    p.write_json(golden / "build-records/selected-inputs.json", {
        "fsbl": {"selected_path": str(multitini / "inputs/fsbl.elf"), "artifact": p.describe(multitini / "inputs/fsbl.elf")},
        "golden_elf": {"selected_path": str(elf), "artifact": p.describe(elf)},
        "workspace": str(workspace), "source_root": str(ROOT), "vitis": str(vitis),
        "packaging_script_sha256": p.sha(ROOT / "scripts/make_boot_bin.bat"),
    })
    p.run([ROOT / "scripts/make_boot_bin.bat", golden / "BOOT.BIN",
           golden / "inputs/fsbl.elf", golden / "inputs/golden.elf"], vitis=vitis)
    p.write_json(golden / "payload-audit.json", audit_golden(golden))
    documentation = ROOT / "docs/MULTIBOOT.md"
    if documentation.is_file():
        p.copy(documentation, out / "docs/MULTIBOOT.md")
    (out / "README.txt").write_text(
        "Appletini + Multitini multiboot bundle; board acceptance is pending.\n\n"
        "This bundle contains separate update images. It does not contain a raw full-flash image.\n"
        "No flash operation was performed while creating or checking this bundle.\n\n"
        "Firmware-first migration from Appletini on Golden 1.x:\n"
        "Copy appletini/FIRMWARE.BIN as FIRMWARE.BIN, golden/BOOT.BIN as BOOT.BIN,\n"
        "and sd-root/FIRMWARE-MULTITINI.BIN to the SD root. Reboot to install Appletini.\n"
        "From the initial boot menu, use About > Update Golden Boot... and choose Update and reboot.\n"
        "On a physical Apple II this action is disabled after boot handoff. ONE//e remains eligible.\n"
        "The new Golden repairs primary and automatically installs Multitini.\n"
        "This menu-driven migration still needs board validation.\n\n"
        "Existing serial migration remains available:\n"
        "1. Preserve user SD media and settings. Install golden/BOOT.BIN FIRST: copy it as\n"
        "   BOOT.BIN on the SD root and invoke the running firmware's :selfupdate command.\n"
        "   Use the golden serial selfupdate/selfrx recovery path if required.\n"
        "2. After Golden B2.0.1 or later is installed, copy both files from sd-root/ to the SD\n"
        "   root and enter Golden once. It installs them serially, using each file's\n"
        "   contents to choose the product slot, and renames successful updates .OK.\n"
        "   The aliases are exact copies of the component firmware images.\n"
        "   FIRMWARE.BIN also remains supported. Scan order: FIRMWARE.BIN,\n"
        "   FIRMWARE-APPLETINI.BIN, FIRMWARE-MULTITINI.BIN. Incorrect filenames cannot\n"
        "   change the destination. A failed file stays .BIN; scanning continues.\n"
        "   If several valid files target one product, the last successful update wins.\n"
        "3. Keep Multitini core packages on SD. Both config menus expose Startup firmware\n"
        "   and an explicit switch to the other installed firmware. Startup selection is\n"
        "   persistent; an immediate switch is a one-shot request.\n\n"
        "A missing, erased, wrong-product, corrupt or unreadable firmware is never selected\n"
        "from cached presence alone. Golden verifies its live header, bounds and full CRC,\n"
        "then uses the verified other image or stays in recovery.\n"
        "Verified old single-slot firmware can remain as an unknown-family fallback.\n"
        "If that legacy image exceeds 6 MiB, installing Multitini is refused: install the\n"
        "tagged Appletini image first to free the Multitini slot safely.\n\n"
        "Golden primary/trial: 0x000000/0x100000, 1 MiB each. Appletini: 0x200000,\n"
        "6 MiB. Multitini: 0x800000, 0x7e0000 bytes. Metadata: 0xfe0000/0xff0000.\n"
        "Use package_multiboot_bundle.py --check-only --output PATH (directory or ZIP)\n"
        "to recheck checksums, exact sources and golden Bootgen/ELF payloads.\n",
        encoding="utf-8")
    # Recheck selected live inputs after every copy and Bootgen invocation.
    verify_component(multitini, "multitini")
    verify_component(appletini, "appletini")
    verify_golden_build(workspace, multitini)
    p.require(p.describe(elf) == p.describe(golden / "inputs/golden.elf"), "Golden ELF changed during packaging")
    p.write_json(out / "manifest.json", {
        "schema": SCHEMA, "created_utc": datetime.now(timezone.utc).isoformat(), "layout": LAYOUT,
        "classification": "Board acceptance pending", "components": components,
        "install_files": SD_ROOT_FIRMWARE,
        "flash_performed": False, "combined_raw_flash_image": False, "board_validation": False,
        "files": release.inventory(out),
    })
    files = release.inventory(out)
    (out / "SHA256SUMS.txt").write_text("".join(f"{item['sha256']}  {name}\n" for name, item in files.items()), encoding="ascii")
    verify_directory(out)
    p.archive_files(Path(str(out) + ".zip"), {name: out / name for name in release.inventory(out)})
    return verify_directory(out, compare_zip=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--multitini-package", type=Path)
    parser.add_argument("--multitini-workspace", type=Path)
    parser.add_argument("--appletini-package", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--vitis", type=Path, default=Path("C:/Xilinx/2025.2/Vitis"))
    parser.add_argument("--check-only", action="store_true")
    args = parser.parse_args()
    out = args.output.resolve()
    if args.check_only:
        result = verify_zip(out) if out.suffix.lower() == ".zip" else verify_directory(out, compare_zip=Path(str(out) + ".zip").exists())
    else:
        for name in ("multitini_package", "multitini_workspace", "appletini_package"):
            p.require(getattr(args, name) is not None, "Creation requires --" + name.replace("_", "-"))
        result = create_bundle(args.multitini_package.resolve(), args.multitini_workspace.resolve(),
                               args.appletini_package.resolve(), out, args.vitis.resolve())
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
