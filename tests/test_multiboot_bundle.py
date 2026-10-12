# SPDX-License-Identifier: GPL-3.0-only
"""Explicit dual-package inputs and independent golden payload/source gates."""
from __future__ import annotations

from dataclasses import asdict
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import package_multiboot_bundle as bundle


def elf(body, address):
    data = bytearray(0x100 + len(body))
    struct.pack_into("<16sHHIIIIIHHHHHH", data, 0,
                     b"\x7fELF\x01\x01\x01" + bytes(9), 2, 40, 1, address, 52,
                     0, 0, 52, 32, 1, 0, 0, 0)
    struct.pack_into("<8I", data, 52, 1, 0x100, address, address,
                     len(body), len(body), 5, 4)
    data[0x100:] = body
    return bytes(data)


def golden_raw(fsbl, app):
    data = bytearray(512 + len(fsbl) + len(app))
    struct.pack_into("<11I", data, 0x20, 0xAA995566, 0x584C4E58, 0, 0x01010000,
                     512, len(fsbl), 0xFFFC0000, 0xFFFC0000, len(fsbl), 0, 0)
    values = struct.unpack_from("<10I", data, 0x20)
    struct.pack_into("<I", data, 0x48, ~sum(values) & 0xFFFFFFFF)
    struct.pack_into("<2I", data, 0x98, 0xA0, 0x100)
    struct.pack_into("<5I", data, 0xA0, 0x01020000, 2, 0x100 // 4, 0xE0 // 4, 0)
    struct.pack_into("<4I", data, 0xE0, 0x1C0 // 4, 0x100 // 4, 0, 1)
    struct.pack_into("<4I", data, 0x1C0, 0, 0x140 // 4, 0, 1)
    for off, ih, body, start, address in ((0x100, 0xE0, fsbl, 512, 0xFFFC0000),
                                         (0x140, 0x1C0, app, 512 + len(fsbl), 0x100000)):
        words = [len(body) // 4] * 3 + [address, address, start // 4, 0x10, 1,
                                       0, ih // 4, 0, 0, 0, 0, 0]
        struct.pack_into("<16I", data, off, *words, ~sum(words) & 0xFFFFFFFF)
    struct.pack_into("<I", data, 0x1BC, 0xFFFFFFFF)
    data[512:512 + len(fsbl)] = fsbl
    data[512 + len(fsbl):] = app
    return bytes(data)


def appletini_raw(fsbl, bit, app):
    data = bytearray(1024 + len(fsbl) + len(bit) + len(app))
    struct.pack_into("<11I", data, 0x20, 0xAA995566, 0x584C4E58, 0, 0x01010000,
                     1024, len(fsbl), 0xFFFC0000, 0xFFFC0000, len(fsbl), 0, 0)
    struct.pack_into("<I", data, 0x48, ~sum(struct.unpack_from("<10I", data, 0x20)) & 0xFFFFFFFF)
    struct.pack_into("<2I", data, 0x98, 0xA0, 0x100)
    struct.pack_into("<5I", data, 0xA0, 0x01020000, 3, 0x100 // 4, 0xE0 // 4, 0)
    start = 1024
    parts = ((0x100, 0xE0, 0x200, fsbl, 0xFFFC0000, 0x10),
             (0x140, 0x200, 0x240, bit, 0, 0x20), (0x180, 0x240, 0, app, 0x100000, 0x10))
    for offset, ih, next_ih, body, address, attributes in parts:
        struct.pack_into("<4I", data, ih, next_ih // 4, offset // 4, 0, 1)
        words = [len(body) // 4] * 3 + [address, address, start // 4, attributes, 1,
                                       0, ih // 4, 0, 0, 0, 0, 0]
        struct.pack_into("<16I", data, offset, *words, ~sum(words) & 0xFFFFFFFF)
        data[start:start + len(body)] = body
        start += len(body)
    struct.pack_into("<I", data, 0x1FC, 0xFFFFFFFF)
    return bytes(data)


def append(raw, role, product=None):
    image = bundle.image_manifest
    flags = image.FLAG_RECOVERY_CAPABLE
    version = 1
    if product:
        flags |= image.PRODUCT_BY_NAME[product] | image.FLAG_MULTIBOOT
        version = 2
    return raw + image.encode_manifest(role, flags, len(raw), image._crc32(raw), version)


def seal(directory, metadata):
    (directory / "manifest.json").unlink(missing_ok=True)
    (directory / "SHA256SUMS.txt").unlink(missing_ok=True)
    bundle.p.write_json(directory / "manifest.json", {**metadata, "files": bundle.release.inventory(directory)})
    files = bundle.release.inventory(directory)
    (directory / "SHA256SUMS.txt").write_text(
        "".join(f"{item['sha256']}  {name}\n" for name, item in files.items()), encoding="ascii")


class BundleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.build = self.root / "build"
        self.build.mkdir()
        self.fsbl_body = b"FSBL" + bytes(60)
        self.golden_body = (bundle.GOLDEN_MARKER + b"\0" + bundle.image_manifest.RECOVERY_MARKER).ljust(128, b"\0")
        self.fsbl = elf(self.fsbl_body, 0xFFFC0000)
        self.golden_elf = elf(self.golden_body, 0x100000)
        self.raw = golden_raw(self.fsbl_body, self.golden_body)
        self.golden = self.build / "golden-fixture"
        bundle.p.write_json(self.golden / "placeholder.json", {})
        (self.golden / "placeholder.json").unlink()
        bundle.p.copy(self.write("fsbl.elf", self.fsbl), self.golden / "inputs/fsbl.elf")
        bundle.p.copy(self.write("golden.elf", self.golden_elf), self.golden / "inputs/golden.elf")
        (self.golden / "BOOT.BIN").write_bytes(append(self.raw, 1))
        self.multi = self.component("multitini")
        self.app = self.component("appletini")
        self.workspace = self.build / "workspace"
        self.platform = self.build / "platform"
        bundle.p.copy(self.multi / "inputs/fsbl.elf", self.platform / "sw/boot/fsbl.elf")
        bundle.p.copy(self.multi / "inputs/platform.xsa", self.platform / "hw/platform.xsa")
        source = self.write("firmware/golden/bootloader/main.c", b"golden compiled source")
        files = {"firmware/golden/bootloader/main.c": bundle.p.sha(source)}
        for name in bundle.SUPPORT_SOURCES:
            self.write(name, (ROOT / name).read_bytes())
        commands = [{"file": str(source), "command": "arm-none-eabi-gcc -O2 -mfpu=neon -I" +
                     str(self.platform).replace("\\", "/") + " " + str(source)}]
        bundle.p.write_json(self.workspace / "golden/build/compile_commands.json", commands)
        bundle.p.copy(self.write("selected-golden.elf", self.golden_elf), self.workspace / "golden/build/golden.elf")
        for name in ("CMakeLists.txt", "UserConfig.cmake", "lscript.ld", "app.yaml"):
            self.write("build/workspace/golden/src/" + name, b"selected golden settings")
        self.record = {
            "schema": 1, "files": files, "platform_path": str(self.platform),
            "platform_files": {path.relative_to(self.platform).as_posix(): bundle.p.sha(path)
                               for path in self.platform.rglob("*") if path.is_file()},
            "xsa": {"path": str(self.multi / "inputs/platform.xsa"),
                    "sha256": bundle.p.sha(self.multi / "inputs/platform.xsa")},
            "compiled": {"golden": {"files": files,
                                      "compile_commands_sha256": bundle.p.sha(self.workspace / "golden/build/compile_commands.json")}},
        }
        self.write("vitis/bin/bootgen.bat", b"explicit selected Bootgen wrapper")
        bundle.p.write_json(self.workspace / "golden_build_inputs.json", self.record)
        patch_root = patch.object(bundle, "ROOT", self.root)
        patch_sources = patch.object(bundle.p, "ROOT", self.root)
        patch_root.start(); patch_sources.start()
        self.addCleanup(patch_root.stop); self.addCleanup(patch_sources.stop)

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def component(self, product):
        directory = self.build / product
        directory.mkdir()
        source = {"source.c": bundle.p.digest(b"source")}
        bundle.p.write_json(directory / "source-manifest.json", source)
        with zipfile.ZipFile(directory / ("source.zip" if product == "multitini" else "cpu-sources.zip"), "w") as archive:
            archive.writestr("source.c", b"source")
        image = bundle.image_manifest
        raw = image.RECOVERY_MARKER + b"\0" + image.PRODUCT_MARKER[image.PRODUCT_BY_NAME[product]]
        if product == "multitini":
            firmware = directory / "FIRMWARE-M0.2.0.BIN"
            firmware.write_bytes(append(raw, 2, product))
            bundle.p.copy(firmware, directory / "sd/FIRMWARE.BIN")
            bundle.p.copy(self.write("component-fsbl.elf", self.fsbl), directory / "inputs/fsbl.elf")
            self.write("build/multitini/inputs/platform.xsa", b"selected XSA")
            schema, version = "multitini-named-firmware-package-v1", "M0.2.0"
        else:
            self.appletini_evidence(directory, raw)
            schema, version = "appletini-multiboot-firmware-package-v1", "F1.3.0-dev"
        seal(directory, {"schema": schema, "firmware_version": version})
        return directory

    def appletini_evidence(self, directory, identity):
        blob = b"CPU1 executable".ljust(64, b"\0")
        frontend = identity.ljust(64, b"\0") + blob
        bit = b"retained FPGA config".ljust(64, b"\0")
        (directory / "FIRMWARE.BIN").write_bytes(append(appletini_raw(self.fsbl_body, bit, frontend), 2, "appletini"))
        for name, contents in (("fsbl.elf", self.fsbl), ("frontend.elf", elf(frontend, 0x100000)),
                               ("frontend_core1.elf", elf(blob, 0x20000000)), ("core1_blob.bin", blob),
                               ("appletini_yarz_top.bit", b"retained BIT source"),
                               ("appletini_yarz_top.xsa", b"retained XSA")):
            path = directory / "inputs" / name; path.parent.mkdir(exist_ok=True); path.write_bytes(contents)
        source = {"ps_sources/lib/worker.c": b"compiled Appletini source"}
        hashes = {name: bundle.p.digest(data) for name, data in source.items()}
        records = {name: {"bytes": len(data), "sha256": hashes[name]} for name, data in source.items()}
        with zipfile.ZipFile(directory / "cpu-sources.zip", "w") as archive:
            for name, data in source.items(): archive.writestr(name, data)
        bundle.p.write_json(directory / "source-manifest.json", {"files": records})
        hardware = b"retained HDL source"
        with zipfile.ZipFile(directory / "hardware-sources.zip", "w") as archive:
            archive.writestr("hdl/top.sv", hardware)
        bundle.p.write_json(directory / "hardware-source-manifest.json", {
            "files": {"hdl/top.sv": {"bytes": len(hardware), "sha256": bundle.p.digest(hardware)}}})
        bundle.p.write_json(directory / "build-records/source-before.json", hashes)
        bundle.p.write_json(directory / "build-records/source-after.json", hashes)
        compiled = {"path": str(self.root / "ps_sources/lib/worker.c"),
                    "bytes": len(source["ps_sources/lib/worker.c"]), "sha256": hashes["ps_sources/lib/worker.c"]}
        for app in ("frontend", "bootloader"):
            bundle.p.write_json(directory / "build-records" / (app + "-compile_commands.json"),
                                [{"file": compiled["path"], "command": "gcc -O2 -mfpu=neon"}])
        def record(path): return {"path": str(path), **bundle.p.describe(path)}
        report = {"version": "F1.3.0-dev", "hardware_flashed": False, "hardware_tested": False,
                  "firmware": record(directory / "FIRMWARE.BIN"),
                  "inputs": {path.name: record(path) for path in (directory / "inputs").iterdir()},
                  "cpu_source_archive": record(directory / "cpu-sources.zip"),
                  "hardware_source_archive": record(directory / "hardware-sources.zip"),
                  "source_snapshot": {"before_equals_after": True, "files": hashes},
                  "compiled_sources": {app: [compiled] for app in ("frontend", "bootloader")},
                  "cpu1": {"size_word_matches": True, "elf_raw_matches": True,
                           "elf_sha256": bundle.p.sha(directory / "inputs/frontend_core1.elf"),
                           "raw_sha256": bundle.p.digest(blob), "embedded_sha256": bundle.p.digest(blob),
                           "embedded_bytes": len(blob)}}
        bundle.p.write_json(directory / "ARM_BUILD.json", report)
        selected = {key: record(directory / name) for key, name in (
            ("firmware", "FIRMWARE.BIN"), ("frontend", "inputs/frontend.elf"), ("fsbl", "inputs/fsbl.elf"),
            ("bitstream", "inputs/appletini_yarz_top.bit"), ("xsa", "inputs/appletini_yarz_top.xsa"))}
        fresh = {"status": "PASS", "inputs": selected,
                 "manifest": asdict(bundle.image_manifest.verify_image(directory / "FIRMWARE.BIN")),
                 "core1": {"checked": True, "frontend_size_word_matches": True,
                           "blob_sha256": bundle.p.digest(blob), "blob_address": 0x100000 + 64}}
        retained = {"status": "PASS", "inputs": selected,
                    "partitions": [{"attributes": 0x20, "sha256": bundle.p.digest(bit), "length": len(bit)}]}
        bundle.p.write_json(directory / "FIRMWARE_AUDIT.json", fresh)
        bundle.p.write_json(directory / "retained-d1/FIRMWARE_AUDIT.json", retained)

    def test_independent_two_elf_golden_payload_verification(self):
        audited = bundle.audit_golden(self.golden)
        self.assertEqual(audited["image_count"], 2)
        self.assertEqual(audited["partition_counts"], [1, 1])
        self.assertEqual(audited["golden"]["path"], "golden/inputs/golden.elf")
        wrong = elf(b"WRONG".ljust(128, b"\0"), 0x100000)
        (self.golden / "inputs/golden.elf").write_bytes(wrong)
        with self.assertRaisesRegex(ValueError, "ELF payload mismatch"):
            bundle.audit_golden(self.golden)

    def test_golden_header_checksums_bounds_capability_and_trailing_bytes(self):
        for offset in (0x20, 0x48, 0x100 + 60, 0x140 + 60, 0x1BC):
            with self.subTest(offset=offset):
                broken = bytearray(self.raw); broken[offset] ^= 1
                with self.assertRaises(ValueError):
                    bundle.parse_golden(broken)
        with self.assertRaisesRegex(ValueError, "Bytes after final"):
            bundle.parse_golden(self.raw + bytes(4))
        missing = self.raw.replace(bundle.GOLDEN_MARKER, b"X" * len(bundle.GOLDEN_MARKER))
        (self.golden / "BOOT.BIN").write_bytes(append(missing, 1))
        with self.assertRaisesRegex(ValueError, "linked multiboot"):
            bundle.audit_golden(self.golden)

    def test_component_identity_checksums_and_source_archive_gate(self):
        bundle.verify_component(self.multi, "multitini")
        with self.assertRaises(ValueError):
            bundle.verify_component(self.multi, "appletini")
        (self.multi / "inputs/fsbl.elf").write_bytes(b"tampered")
        with self.assertRaisesRegex(ValueError, "Package input changed"):
            bundle.verify_component(self.multi, "multitini")
        seal(self.multi, {"schema": "multitini-named-firmware-package-v1", "firmware_version": "M0.2.0"})
        with zipfile.ZipFile(self.multi / "source.zip", "w") as archive:
            archive.writestr("source.c", b"modified")
        seal(self.multi, {"schema": "multitini-named-firmware-package-v1", "firmware_version": "M0.2.0"})
        with self.assertRaisesRegex(ValueError, "Source archive differs"):
            bundle.verify_component(self.multi, "multitini")

    def test_source_platform_and_compiler_inputs_must_remain_exact(self):
        bundle.verify_golden_build(self.workspace, self.multi)
        source = self.root / "firmware/golden/bootloader/main.c"
        saved = source.read_bytes(); source.write_bytes(b"changed during build")
        with self.assertRaisesRegex(ValueError, "Build source changed"):
            bundle.verify_golden_build(self.workspace, self.multi)
        source.write_bytes(saved)
        (self.platform / "extra.file").write_bytes(b"unrecorded")
        with self.assertRaisesRegex(ValueError, "platform changed"):
            bundle.verify_golden_build(self.workspace, self.multi)
        (self.platform / "extra.file").unlink()
        (self.workspace / "golden/build/compile_commands.json").write_text("[]")
        with self.assertRaisesRegex(ValueError, "commands changed"):
            bundle.verify_golden_build(self.workspace, self.multi)

    def test_appletini_source_snapshot_cpu1_and_retained_fpga_proofs_are_required(self):
        bundle.verify_component(self.app, "appletini")
        report_path = self.app / "ARM_BUILD.json"
        original = report_path.read_bytes()
        for field, value, message in (("source_snapshot", "before_equals_after", "source changed"),
                                      ("cpu1", "size_word_matches", "CPU1 selection")):
            report = json.loads(original)
            report[field][value] = False
            bundle.p.write_json(report_path, report)
            seal(self.app, {"firmware_version": "F1.3.0-dev"})
            with self.assertRaisesRegex(ValueError, message):
                bundle.verify_component(self.app, "appletini")
            report_path.write_bytes(original)
        retained_path = self.app / "retained-d1/FIRMWARE_AUDIT.json"
        retained = bundle.load(retained_path)
        retained["partitions"][0]["sha256"] = "0" * 64
        bundle.p.write_json(retained_path, retained)
        seal(self.app, {"firmware_version": "F1.3.0-dev"})
        with self.assertRaisesRegex(ValueError, "FPGA bytes differ"):
            bundle.verify_component(self.app, "appletini")

    def test_output_collision_and_wrong_platform_fsbl_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "collides"):
            bundle.validate_output(self.multi / "inside", (self.multi, self.app, self.workspace))
        with self.assertRaisesRegex(ValueError, "child"):
            bundle.validate_output(self.root / "outside", (self.multi, self.app, self.workspace))
        existing = self.build / "existing"; existing.mkdir()
        with self.assertRaisesRegex(ValueError, "already exists"):
            bundle.validate_output(existing, (self.multi, self.app, self.workspace))
        (self.multi / "inputs/fsbl.elf").write_bytes(elf(b"wrong FSBL".ljust(64, b"\0"), 0xFFFC0000))
        with self.assertRaisesRegex(ValueError, "platform FSBL differs"):
            bundle.verify_golden_build(self.workspace, self.multi)

    def test_generated_assembly_has_exact_bytes_without_c_optimization_flags(self):
        stub = self.app / "build-records/frontend-core1_blob.S"
        stub.write_text('.section .rodata.core1_blob,"a"\n.incbin "core1_blob.bin"\n')
        path = str(self.workspace / "frontend/src/core1_blob.S")
        report = bundle.load(self.app / "ARM_BUILD.json")
        report["compiled_sources"]["frontend"].append({"path": path, **bundle.p.describe(stub)})
        bundle.p.write_json(self.app / "ARM_BUILD.json", report)
        commands_path = self.app / "build-records/frontend-compile_commands.json"
        commands = bundle.load(commands_path)
        commands.append({"file": path, "command": "gcc -c core1_blob.S"})
        bundle.p.write_json(commands_path, commands)
        seal(self.app, {"firmware_version": "F1.3.0-dev"})
        bundle.verify_component(self.app, "appletini")
        commands[0]["command"] = "gcc -O0 -mfpu=neon"
        bundle.p.write_json(commands_path, commands)
        seal(self.app, {"firmware_version": "F1.3.0-dev"})
        with self.assertRaisesRegex(ValueError, "optimization differs"):
            bundle.verify_component(self.app, "appletini")

    def test_complete_bundle_directory_and_zip_use_archived_inputs(self):
        out = self.build / "dual-bundle"
        calls = []

        def explicit_bootgen(command, **kwargs):
            calls.append(command)
            self.assertEqual(Path(command[0]), self.root / "scripts/make_boot_bin.bat")
            self.assertEqual(Path(command[2]), out / "golden/inputs/fsbl.elf")
            self.assertEqual(Path(command[3]), out / "golden/inputs/golden.elf")
            self.assertEqual(kwargs, {"vitis": self.root / "vitis"})
            Path(command[1]).write_bytes(append(self.raw, 1))

        with patch.object(bundle.p, "run", explicit_bootgen):
            result = bundle.create_bundle(self.multi, self.workspace, self.app, out, self.root / "vitis")
        self.assertEqual(result["status"], "PASS")
        self.assertEqual(len(calls), 1)
        self.assertFalse(result["flash_performed"])
        self.assertFalse((out / "FLASH.BIN").exists())
        self.assertEqual(bundle.verify_directory(out, compare_zip=True)["status"], "PASS")
        self.assertEqual(bundle.verify_zip(Path(str(out) + ".zip"))["status"], "PASS")
        # A delivered ZIP stays checkable without the original selected sources.
        (self.root / "firmware/golden/bootloader/main.c").unlink()
        self.assertEqual(bundle.verify_zip(Path(str(out) + ".zip"))["status"], "PASS")
        (out / "golden/inputs/golden.elf").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "Package input changed"):
            bundle.verify_directory(out)

    def test_zip_traversal_and_archive_membership_are_rejected(self):
        path = self.build / "unsafe.zip"
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("../outside", b"no")
        with self.assertRaisesRegex(ValueError, "Unsafe package member"):
            bundle.verify_zip(path)
        sources = self.build / "sources.zip"
        with zipfile.ZipFile(sources, "w") as archive:
            archive.writestr("source.c", b"source")
            archive.writestr("unrecorded.c", b"extra")
        with self.assertRaisesRegex(ValueError, "membership"):
            bundle.verify_archive(sources, {"source.c": bundle.p.digest(b"source")})


if __name__ == "__main__":
    unittest.main()
