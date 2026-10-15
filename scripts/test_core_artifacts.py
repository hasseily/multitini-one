#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Focused historical-artifact, compatibility and selection mutation checks."""
import argparse
import binascii
from contextlib import ExitStack
import hashlib
import io
import json
from pathlib import Path
from types import SimpleNamespace
import tarfile
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import core_artifacts as artifacts
import package_cpu1_video_test as package
import package_phos1 as phos


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.directory = self.root / "original-build"
        self.directory.mkdir()
        self.sources = {
            artifacts.ABI_SOURCE: b"localparam MT_ABI_MAJOR = 16'd0;\nlocalparam MT_ABI_MINOR = 16'd20;\n",
            "hdl/hdl_sources.txt": b"[common]\ngenerated/multitini_regs_pkg.sv\n[c64]\ncore/demo.sv\n",
            "hdl/core/demo.sv": b"module demo; // original qualified circuit\nendmodule\n",
            "scripts/create_project.tcl": b"# original generator\n",
            "scripts/build_and_export_xsa.tcl": b"# original signoff\n",
            "scripts/capture_vectrex_build_inputs.py": b"# original capture\n",
            "LICENSE": b"GPL-3.0-only\n", "licenses/files.csv": b"path,license\n",
            "provenance/sources.lock.json": b"{}\n", "MULTITINI_ONE_PLAN.md": b"original plan\n",
        }
        for name, data in self.sources.items():
            target = self.root / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
        (self.root / "hdl/core/demo.sv").write_text("module demo; // current changed circuit\nendmodule\n")
        self.record = {"machine": "C64", "captured_utc": "2000-01-01T00:00:00+00:00",
                       "configuration": {"c64_video_standard": "PAL", "gbc_psram": False, "genesis_pal": False}}
        for name in ("generate.tcl", "build.tcl"):
            (self.directory / name).write_text("set ::env(MULTITINI_MACHINE) {C64}\n"
                                              "set ::env(MULTITINI_BUILD_ID) {50484F35}\n")
        reports = self.directory / "reports"
        reports.mkdir()
        (reports / "build_manifest.txt").write_text(
            "image_kind=C64\npart=xc7z020clg484-2\ntop=multitini_one_top\n"
            "generic=SHELL_BUILD_ID=32'h50484F35 C64_NTSC_MODE=1'b0 ACTIVE_MACHINE=0 "
            "ENABLE_GBC_PSRAM=1'b0 GENESIS_PAL_MODE=1'b0\n")
        (reports / "timing-fixture.rpt").write_text("Qualified signoff report retained byte-for-byte\n")
        self.bit = self.directory / "multitini_one.runs/impl_1/multitini_one_top.bit"
        self.bit.parent.mkdir(parents=True)
        self.bit.write_bytes(b"qualified original bitstream")
        Path(str(self.bit) + ".kind").write_text("C64\n")
        self.ps = b"original audited PS register initialization"
        self.write_xsa()
        (self.directory / "build.log").write_text(
            "Timing gates passed and XSA exported: " + (self.directory / "multitini_one.xsa").as_posix())
        self.seal_sources()
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        self.stack.enter_context(patch.object(artifacts, "ROOT", self.root))
        self.stack.enter_context(patch.object(package, "ROOT", self.root))
        self.stack.enter_context(patch.object(phos, "ROOT", self.root))
        self.stack.enter_context(patch.object(package.hardware_inputs, "inputs",
            lambda machine: [self.root / name for name in self.sources]))
        # Source/settings/identity/XSA checks are real; normal signoff parsing is
        # separately exercised on the seven real routed images in preflight.
        self.stack.enter_context(patch.object(package, "timing", return_value={"status": "fixture-PASS"}))
        self.stack.enter_context(patch.object(phos, "MATRIX", {"c64-pal": ("C64", False, False)}))

    def seal_sources(self):
        tar_path = self.directory / "source-inputs.sources.tar.gz"
        with tarfile.open(tar_path, "w:gz") as archive:
            for name, data in self.sources.items():
                info = tarfile.TarInfo(name)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
        self.record["files"] = {name: artifacts.digest(data) for name, data in self.sources.items()}
        self.record["archive"] = {"name": tar_path.name, "sha256": artifacts.describe(tar_path)["sha256"]}
        (self.directory / "source-inputs.json").write_text(json.dumps(self.record))
        consumed = dict(self.record, archive=dict(self.record["archive"], name="build_inputs.sources.tar.gz"))
        (self.directory / "reports/build_inputs.json").write_text(json.dumps(consumed))
        (self.directory / "reports/build_inputs.sources.tar.gz").write_bytes(tar_path.read_bytes())
        with zipfile.ZipFile(self.directory / "source-inputs.zip", "w") as archive:
            for name, data in self.sources.items():
                archive.writestr(name, data)
            for name in artifacts.CONFIG_NAMES:
                archive.write(self.directory / name, "build-config/" + name)

    def write_xsa(self, bit=None):
        with zipfile.ZipFile(self.directory / "multitini_one.xsa", "w") as archive:
            archive.writestr("design.bit", self.bit.read_bytes() if bit is None else bit)
            archive.writestr("ps7_init.c", self.ps)
            archive.writestr("design.hwh", '\n'.join(
                f'<P NAME="PCW_USE_S_AXI_{port}" VALUE="1"/>\n'
                f'<P NAME="C_S_AXI_{port}_DATA_WIDTH" VALUE="64"/>' for port in ("HP0", "HP1")))

    def inspect(self):
        return artifacts.inspect_artifact(self.directory, "C64")

    def args(self, manifest=None):
        return SimpleNamespace(reuse_image=[] if manifest else ["c64-pal=" + str(self.directory)],
            selection_manifest=manifest, supervisor_dir=None, hardware=self.root / "unused",
            build_id="4D000301", abi_minor=21)

    def test_historical_source_and_identity_reuse_preserved(self):
        build = phos.verify_image_set(self.args(), {"ps7_init_sha256": artifacts.digest(self.ps)})["c64-pal"]
        identity = build["identity"]
        self.assertEqual((identity["build_id"], identity["abi"]), ("0x50484F35", "0.20"))
        self.assertTrue(build["reused"])
        self.assertFalse(identity["current_input_match"])
        self.assertIn("hdl/core/demo.sv", identity["changed_required"])
        with zipfile.ZipFile(self.directory / "source-inputs.zip") as archive:
            self.assertEqual(archive.read("hdl/core/demo.sv"), self.sources["hdl/core/demo.sv"])

    def test_new_build_still_requires_current_sources(self):
        with self.assertRaisesRegex(ValueError, "Build source changed"):
            package.verify_hardware(self.directory, "C64", build_id="4D000301")

    def test_build_reuse_requires_effective_inputs_but_allows_ancillary(self):
        with self.assertRaisesRegex(ValueError, "Effective FPGA inputs"):
            artifacts.inspect_artifact(self.directory, "C64", require_current_inputs=True)
        (self.root / "hdl/core/demo.sv").write_bytes(self.sources["hdl/core/demo.sv"])
        (self.root / "MULTITINI_ONE_PLAN.md").write_text("current release notes")
        result = artifacts.inspect_artifact(self.directory, "C64", require_current_inputs=True)
        self.assertTrue(result["current_input_match"])
        self.assertFalse(result["current_snapshot_match"])

    def test_incompatible_abi_rejected_from_actual_archived_interface(self):
        for major, minor in ((0, 10), (0, 22), (1, 20)):
            with self.subTest(major=major, minor=minor):
                self.sources[artifacts.ABI_SOURCE] = f"localparam MT_ABI_MAJOR = 16'd{major};\nlocalparam MT_ABI_MINOR = 16'd{minor};\n".encode()
                self.seal_sources()
                with self.assertRaisesRegex(ValueError, "Image ABI"):
                    phos.verify_image_set(self.args(), {"ps7_init_sha256": artifacts.digest(self.ps)})

    def test_platform_mismatch_fails_closed(self):
        with self.assertRaisesRegex(ValueError, "PS7/FSBL configuration mismatch"):
            phos.verify_image_set(self.args(), {"ps7_init_sha256": "0" * 64})

    def test_source_tar_tampering_rejected(self):
        with (self.directory / "source-inputs.sources.tar.gz").open("ab") as stream:
            stream.write(b"tamper")
        with self.assertRaisesRegex(ValueError, "TAR hash"):
            self.inspect()

    def test_source_zip_tampering_rejected(self):
        path = self.directory / "source-inputs.zip"
        with zipfile.ZipFile(path) as archive:
            members = {name: archive.read(name) for name in archive.namelist()}
        members["hdl/core/demo.sv"] = b"changed original"
        with zipfile.ZipFile(path, "w") as archive:
            for name, data in members.items():
                archive.writestr(name, data)
        with self.assertRaisesRegex(ValueError, "archive mismatch|ZIP hash"):
            self.inspect()

    def test_original_build_settings_tampering_rejected(self):
        (self.directory / "build.tcl").write_text("set ::env(MULTITINI_BUILD_ID) {4D000301}\n")
        with self.assertRaisesRegex(ValueError, "configuration changed|settings changed"):
            self.inspect()

    def test_consistent_archives_cannot_omit_hdl_closure(self):
        del self.sources["hdl/core/demo.sv"]
        self.seal_sources()
        with self.assertRaisesRegex(ValueError, "HDL closure"):
            self.inspect()

    def test_relative_hdl_dependency_normalizes_inside_repository(self):
        self.sources["third_party/demo/original.sv"] = b"module original; endmodule\n"
        target = self.root / "third_party/demo/original.sv"
        target.parent.mkdir(parents=True)
        target.write_bytes(self.sources["third_party/demo/original.sv"])
        self.sources["hdl/hdl_sources.txt"] += b"../third_party/demo/original.sv\n"
        self.seal_sources()
        self.assertEqual(self.inspect()["abi"], "0.20")

    def test_relative_hdl_dependency_cannot_escape_repository(self):
        self.sources["hdl/hdl_sources.txt"] += b"../../outside.sv\n"
        self.seal_sources()
        with self.assertRaisesRegex(ValueError, "Unsafe archived source path"):
            self.inspect()

    def test_original_build_identity_must_match_report_even_if_archives_agree(self):
        for name in ("generate.tcl", "build.tcl"):
            path = self.directory / name
            path.write_text(path.read_text().replace("50484F35", "4D000301"))
        self.seal_sources()
        with self.assertRaisesRegex(ValueError, "setting/report mismatch"):
            self.inspect()

    def test_xsa_bitstream_tampering_rejected(self):
        self.write_xsa(b"different bitstream")
        with self.assertRaisesRegex(ValueError, "XSA bitstream differs"):
            self.inspect()

    def test_vivado_consumed_capture_must_match_original_sources(self):
        path = self.directory / "reports/build_inputs.json"
        consumed = json.loads(path.read_text())
        consumed["files"]["hdl/core/demo.sv"] = "0" * 64
        path.write_text(json.dumps(consumed))
        with self.assertRaisesRegex(ValueError, "Vivado source capture differs"):
            self.inspect()

    def test_selection_manifest_pins_reports_against_later_mutation(self):
        original = self.inspect()
        manifest = self.root / "selection.json"
        manifest.write_text(json.dumps({"schema_version": 1, "images": {"c64-pal": {
            "machine": "C64", "action": "reuse", "directory": str(self.directory), "artifact": original}}}))
        phos.verify_image_set(self.args(manifest), {"ps7_init_sha256": artifacts.digest(self.ps)})
        (self.directory / "reports/timing-fixture.rpt").write_text("changed after selection")
        with self.assertRaisesRegex(ValueError, "Selected artifact changed"):
            phos.verify_image_set(self.args(manifest), {"ps7_init_sha256": artifacts.digest(self.ps)})

    def test_actual_older_abi_is_used_for_core_cfg_validation(self):
        directory = self.root / "core-package"
        directory.mkdir()
        payload = b"example processed image"
        (directory / "core.bin").write_bytes(payload)
        (directory / "core.cfg").write_text(f"abi=0.20\n# pal.sha256={artifacts.digest(payload)}\n"
            f"variant=pal|PAL|core.bin|{len(payload)}|0x{binascii.crc32(payload):08X}\n")
        package.verify_core(directory, {"pal"}, abi=self.inspect()["abi"])
        with self.assertRaisesRegex(ValueError, "Core package ABI"):
            package.verify_core(directory, {"pal"}, abi="0.21")

    def test_corrupt_archive_has_consistent_error_api(self):
        (self.directory / "source-inputs.zip").write_bytes(b"not a ZIP")
        with self.assertRaises(ValueError):
            self.inspect()

    def test_reuse_selection_rejects_unknown_and_duplicate(self):
        for values in (["unknown=" + str(self.directory)], ["c64-pal=" + str(self.directory)] * 2):
            with self.assertRaises(ValueError):
                artifacts.parse_reuse_images(values, {"c64-pal"})

    def test_native_delivery_guides_are_copied_and_hashed_before_sealing(self):
        for name in phos.GS_GUIDES:
            path = self.root / "docs" / name
            path.parent.mkdir(exist_ok=True)
            path.write_text("Frozen guide " + name)
        out = self.root / "delivery"
        records = phos.copy_delivery_guides(out)
        self.assertEqual(set(records), {"docs/" + name for name in phos.GS_GUIDES})
        for name, description in records.items():
            self.assertEqual((out / name).read_bytes(), (self.root / name).read_bytes())
            self.assertEqual(description, artifacts.describe(out / name))

    def test_fresh_package_extraction_has_separate_rom_and_media_folders(self):
        out = self.root / "delivery"
        asset = self.root / "source-built-hdd.rom"
        asset.write_bytes(bytes(range(256)))
        phos.prepare_iigs_content(out, asset)
        core = out / "sd/multitini/cores/apple-iigs"
        (core / "core.cfg").write_text("content_root=0:/multitini/cores/apple-iigs/media\n")
        archive_path = self.root / "delivery.zip"
        package.archive_files(archive_path, {p.relative_to(out).as_posix(): p
                                           for p in out.rglob("*") if p.is_file()})
        fresh = self.root / "fresh-unzip"
        with zipfile.ZipFile(archive_path) as archive:
            archive.extractall(fresh)
        installed = fresh / "sd/multitini/cores/apple-iigs"
        self.assertTrue((installed / "media").is_dir())
        self.assertTrue((installed / "roms").is_dir())
        self.assertEqual({p.name for p in (installed / "roms").iterdir()}, {"hdd.rom"})
        self.assertEqual((installed / "roms/hdd.rom").read_bytes(), asset.read_bytes())
        self.assertEqual({p.name for p in (installed / "media").iterdir()}, {"README.txt"})
        text = (installed / "media/README.txt").read_text()
        self.assertIn("pram.bin", text)
        self.assertIn("WOZ2", text)
        self.assertIn("../roms/", text)
        self.assertIn("validated owner-supplied Apple ROMs", text)
        self.assertIn("No games or operating-system disk images", text)
        self.assertNotIn("No Apple ROMs", text)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/core-gs/core-artifact-tests"))
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ArtifactTests))
    args.build_dir.mkdir(parents=True, exist_ok=True)
    inputs = ("scripts/core_artifacts.py", "scripts/package_phos1.py",
              "scripts/package_cpu1_video_test.py", "scripts/test_core_artifacts.py")
    (args.build_dir / "validation.json").write_text(json.dumps({"status": "PASS" if result.wasSuccessful() else "FAIL",
        "tests": result.testsRun, "failures": len(result.failures) + len(result.errors),
        "files": {name: hashlib.sha256(Path(name).read_bytes()).hexdigest() for name in inputs}}, indent=2) + "\n")
    raise SystemExit(0 if result.wasSuccessful() else 1)
