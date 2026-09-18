# SPDX-License-Identifier: GPL-3.0-only
"""Core-only delivery preserves its pinned baseline and rejects mixed inputs."""
from __future__ import annotations

import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import package_video_blur_update as package

common = package.common


def encoded(value):
    return json.dumps(value, sort_keys=True).encode()


def zipped(files):
    stream = io.BytesIO()
    with zipfile.ZipFile(stream, "w") as archive:
        for name, value in files.items():
            archive.writestr(name, value)
    return stream.getvalue()


def xsa(bit=b"selected BIT"):
    description = "\n".join(f'<PARAMETER NAME="{name}" VALUE="{value}"/>'
                              for port in ("HP0", "HP1")
                              for name, value in (("PCW_USE_S_AXI_" + port, 1),
                                                  ("C_S_AXI_" + port + "_DATA_WIDTH", 64)))
    return zipped({"ps7_init.c": b"PS initialization", "design.hwh": description,
                   "design.bit": bit})


class BlurPackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.baseline_files = {name: b"immutable " + name.encode() for name in package.RETAINED}
        self.baseline_files.update({
            "inputs/platform.xsa": xsa(),
            "evidence/payload-audit.json": encoded({"status": "PASS"}),
            "source.zip": zipped({"old/source.sv": b"historical source"}),
            "source-manifest.json": encoded({"old/source.sv": common.digest(b"historical source")}),
        })
        for image in package.IMAGES:
            self.baseline_files[f"evidence/{image}-qualification.json"] = encoded({
                "passed": True, "image": image, "firmware_id": "0x56494434"})
            self.baseline_files[f"evidence/{image}/source-inputs.json"] = encoded({
                "files": {package.CHANGED_HDL: common.digest(b"old HDL")}})

    def baseline(self, **manifest_changes):
        manifest = {"firmware_id": "0x56494434", "abi": "0.15", "files": {
            name: {"bytes": len(data), "sha256": common.digest(data)}
            for name, data in self.baseline_files.items()}}
        manifest.update(manifest_changes)
        files = dict(self.baseline_files, **{"manifest.json": encoded(manifest)})
        files["SHA256SUMS.txt"] = "".join(common.digest(data) + "  " + name + "\n"
                                           for name, data in sorted(files.items())).encode()
        path = self.root / "baseline.zip"
        path.write_bytes(zipped(files))
        return path, common.sha(path)

    def test_baseline_is_exactly_pinned_and_historical_sources_need_no_live_tree(self):
        path, digest = self.baseline()
        with patch.object(common, "verify_sources", side_effect=AssertionError("must not read live sources")):
            baseline = package.verify_baseline(path, digest)
        self.assertEqual(set(baseline["retained"]), set(package.RETAINED))
        self.assertEqual(baseline["source_archive"]["sha256"], common.digest(self.baseline_files["source.zip"]))
        with self.assertRaisesRegex(ValueError, "Wrong baseline ZIP"):
            package.verify_baseline(path, "0" * 64)
        with self.assertRaisesRegex(ValueError, "64 hex digits"):
            package.verify_baseline(path, "not a hash")

    def test_baseline_rejects_wrong_abi_identity_and_nonpassing_audit(self):
        for changes in ({"abi": "0.14"}, {"firmware_id": "0x56494435"}):
            with self.assertRaisesRegex(ValueError, "VID4 / ABI"):
                package.verify_baseline(*self.baseline(**changes))
        self.baseline_files["evidence/payload-audit.json"] = encoded({"status": "FAIL"})
        with self.assertRaisesRegex(ValueError, "payload audit"):
            package.verify_baseline(*self.baseline())

    def test_baseline_rejects_historical_source_corruption_and_missing_retained_file(self):
        self.baseline_files["source.zip"] = zipped({"old/source.sv": b"different historical source"})
        with self.assertRaisesRegex(ValueError, "historical source mismatch"):
            package.verify_baseline(*self.baseline())
        del self.baseline_files["sd/FIRMWARE.BIN"]
        with self.assertRaisesRegex(ValueError, "retained component missing"):
            package.verify_baseline(*self.baseline())

    def test_baseline_requires_one_qualification_per_selected_core(self):
        del self.baseline_files["evidence/gbc-qualification.json"]
        with self.assertRaisesRegex(ValueError, "qualifications missing"):
            package.verify_baseline(*self.baseline())
        self.baseline_files["evidence/duplicate-qualification.json"] = self.baseline_files["evidence/c64-pal-qualification.json"]
        with self.assertRaisesRegex(ValueError, "Duplicate baseline qualification"):
            package.verify_baseline(*self.baseline())

    def test_archive_member_paths_duplicates_and_source_copy_are_checked(self):
        for name in ("../outside", "/absolute", "C:/drive", "a\\b", "./alias"):
            raw = zipped({name: b"x"})
            if name == "a\\b":
                # Windows ZipInfo normalizes write names; craft the unsafe raw headers.
                raw = raw.replace(b"a/b", b"a\\b")
            with self.subTest(name=name), zipfile.ZipFile(io.BytesIO(raw)) as archive:
                with self.assertRaisesRegex(ValueError, "Unsafe ZIP"):
                    package.safe_members(archive)
        stream = io.BytesIO()
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(stream, "w") as archive:
                archive.writestr("duplicate", b"a")
                archive.writestr("duplicate", b"b")
        with zipfile.ZipFile(stream) as archive:
            with self.assertRaisesRegex(ValueError, "Duplicate ZIP"):
                package.safe_members(archive)
        path = self.root / "sources.zip"
        path.write_bytes(zipped({"source.sv": b"original"}))
        package.verify_source_archive(path, {"source.sv": common.digest(b"original")})
        with self.assertRaisesRegex(ValueError, "bytes mismatch"):
            package.verify_source_archive(path, {"source.sv": common.digest(b"changed")})
        with self.assertRaisesRegex(ValueError, "membership mismatch"):
            package.verify_source_archive(path, {"other.sv": common.digest(b"original")})

    def test_xsa_requires_exact_single_embedded_selected_bit(self):
        bit, platform = self.root / "selected.bit", self.root / "selected.xsa"
        bit.write_bytes(b"selected BIT")
        platform.write_bytes(xsa())
        package.verify_xsa_bit(platform, bit)
        for files, message in (({"one.bit": b"wrong"}, "differs"),
                               ({"one.bit": bit.read_bytes(), "two.bit": bit.read_bytes()}, "exactly one"),
                               ({"unrelated": b"x"}, "exactly one")):
            platform.write_bytes(zipped(files))
            with self.assertRaisesRegex(ValueError, message):
                package.verify_xsa_bit(platform, bit)

    def test_sd_scope_emits_only_two_core_pairs(self):
        sd = self.root / "sd"
        for name in package.SD_FILES:
            path = sd / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"payload or cfg")
        package.verify_sd_scope(sd)
        for name in ("FIRMWARE.BIN", "multitini/cores/vectrex/core.cfg", "multitini/cores/c64/c64-ntsc.bin",
                     "multitini/cores/gbc/media/game.gb"):
            path = sd / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"must not ship")
            with self.assertRaisesRegex(ValueError, "exactly C64 PAL and GBC"):
                package.verify_sd_scope(sd)
            path.unlink()

    def fixture_build(self):
        directory = self.root / "gbc"
        reference = directory / "incremental-reference"
        (reference / "reference-build").mkdir(parents=True)
        (directory / "reports").mkdir()
        original = (ROOT / "scripts/build_and_export_xsa.tcl").read_bytes()
        source_record = {package.CHANGED_HDL: common.digest(b"new HDL"),
                         "scripts/build_and_export_xsa.tcl": common.digest(original)}
        old_sources = dict(source_record, **{package.CHANGED_HDL: common.digest(b"old HDL")})
        old_snapshot = encoded({"files": old_sources})
        old_qualification = {"passed": True, "image": "gbc", "firmware_id": "0x56494434"}
        baseline = {"ps7_init_sha256": common.digest(b"PS initialization"),
                    "qualifications": {"gbc": old_qualification}, "source_snapshots": {"gbc": old_snapshot}}
        (reference / "reference-build/source-inputs.json").write_bytes(old_snapshot)
        (reference / "reference.dcp").write_bytes(b"qualified static checkpoint")
        (reference / "build_and_export_xsa.original.tcl").write_bytes(original)
        (reference / "build_and_export_xsa.incremental.tcl").write_bytes(package.incremental_script(original))
        (directory / "build.tcl").write_text("source {" + (reference / "build_and_export_xsa.incremental.tcl").as_posix() + "}\n")
        settings = {"AUTO_INCREMENTAL_CHECKPOINT": 0, "INCREMENTAL_CHECKPOINT.DIRECTIVE": "TimingClosure"}
        record = {"image": "gbc", "reference_qualified": True, "fresh_synthesis": True,
                  "original_gate_tail_unchanged": True, "reference_result": old_qualification,
                  "reference": common.describe(reference / "reference.dcp"), "settings": settings,
                  "source_delta_from_reference": [package.CHANGED_HDL], "files": {
                      path.relative_to(reference).as_posix(): common.describe(path)
                      for path in reference.rglob("*") if path.is_file()}}
        common.write_json(reference / "manifest.json", record)
        (directory / "incremental-inputs.zip").write_bytes(zipped({
            "incremental-reference/" + path.relative_to(reference).as_posix(): path.read_bytes()
            for path in reference.rglob("*") if path.is_file()}))
        bit, platform = directory / "selected.bit", directory / "selected.xsa"
        bit.write_bytes(b"selected BIT")
        Path(str(bit) + ".kind").write_text("full")
        platform.write_bytes(xsa())
        expected = {"actual_reuse": True, "fallback": False, "rows": {
            "cells": {"current_reuse_percent": 95.5, "total": 100},
            "nets": {"current_reuse_percent": 88.0, "total": 200}}}
        report = directory / "reports/incremental_reuse.rpt"
        report.write_text("| Auto Incremental | No |\n| Incremental Directive | TimingClosure |\n"
                          "| DCP Location: | " + (reference / "reference.dcp").as_posix() + " |\n"
                          "| Type | Initial Reuse % | Current Reuse % | Total |\n"
                          "| Cells | 96 | 95.5 | 100 |\n| Nets | 90 | 88.0 | 200 |\n"
                          "| Type | % |\n| Non-Reused Cells | 4.5 |\n")
        (directory / "build.log").write_text(
            "STATIC_INCREMENTAL AUTO_INCREMENTAL_CHECKPOINT=0\n"
            "STATIC_INCREMENTAL INCREMENTAL_CHECKPOINT=" + (reference / "reference.dcp").as_posix() + "\n"
            "STATIC_INCREMENTAL INCREMENTAL_CHECKPOINT.DIRECTIVE=TimingClosure\n")
        common.write_json(directory / "source-inputs.json", {"files": source_record})
        result = {"bit": bit, "xsa": platform, "ps7_init_sha256": baseline["ps7_init_sha256"],
                  "timing": {"timing_ns": {"WNS": .1, "WHS": .01, "WPWS": .2}},
                  "record": {"files": source_record}, "artifacts": [bit, platform, report, directory / "build.tcl"]}
        qualification = {"passed": True, "image": "gbc", "firmware_id": "0x56494435", "abi": "0.15",
                         "bit": common.describe(bit), "xsa": common.describe(platform), "timing": result["timing"],
                         "incremental": {"settings": settings, "input_archive": common.describe(directory / "incremental-inputs.zip"),
                                         "reference": record["reference"], "reuse_summary": expected,
                                         "actual_reuse_report": common.describe(report)}}
        for name in ("qualification.json", "result.json"):
            common.write_json(directory / name, qualification)
        return directory, baseline, result, expected

    def test_selected_new_build_requires_explicit_vid5_and_qualified_vid4_reference(self):
        directory, baseline, result, _ = self.fixture_build()
        with patch.object(common, "verify_hardware", return_value=result) as verify:
            checked = package.verify_new_build(directory, "gbc", baseline)
            verify.assert_called_once_with(directory, "GBC", build_id="56494435")
            self.assertEqual(checked["input_snapshot"]["inputs/gbc.bit"], common.describe(result["bit"]))
            baseline["qualifications"]["gbc"]["firmware_id"] = "0x56494433"
            with self.assertRaisesRegex(ValueError, "not the pinned VID4"):
                package.verify_new_build(directory, "gbc", baseline)

    def test_recipe_rejects_synthesis_skips_or_rehashed_gate_modification(self):
        directory, _, result, _ = self.fixture_build()
        sources = result["record"]["files"]
        package.verify_incremental_recipe(directory, sources)
        path = directory / "incremental-reference/build_and_export_xsa.incremental.tcl"
        original = path.read_bytes()
        for before, after in ((b"launch_runs synth_1", b"# skip synthesis"),
                              (b"require_complete_run impl_1", b"# skip signoff")):
            self.assertIn(before, original)
            path.write_bytes(original.replace(before, after, 1))
            with self.assertRaisesRegex(ValueError, "fresh-synthesis or full gate"):
                package.verify_incremental_recipe(directory, sources)
        path.write_bytes(original)
        (directory / "build.tcl").write_text("source {other.tcl}\n")
        with self.assertRaisesRegex(ValueError, "audited gate recipe"):
            package.verify_incremental_recipe(directory, sources)

    def test_reuse_report_and_actual_settings_override_stale_positive_json(self):
        directory, _, _, expected = self.fixture_build()
        package.verify_reuse(directory, expected)
        report = directory / "reports/incremental_reuse.rpt"
        original = report.read_text()
        for before, after, error in (("95.5", "0", "No actual"), ("95.5", "nan", "No actual"),
                                     ("95.5", "80.0", "differs from actual"),
                                     ("| No |", "| Yes |", "Auto Incremental"),
                                     ("TimingClosure", "RuntimeOptimized", "Directive"),
                                     ("reference.dcp", "different.dcp", "DCP Location")):
            report.write_text(original.replace(before, after))
            with self.assertRaisesRegex(ValueError, error):
                package.verify_reuse(directory, expected)
        report.write_text(original)
        log = directory / "build.log"
        log.write_text(log.read_text().replace("CHECKPOINT=0", "CHECKPOINT=1"))
        with self.assertRaisesRegex(ValueError, "Actual incremental settings"):
            package.verify_reuse(directory, expected)

    def test_final_fence_rejects_original_copied_payload_and_source_changes(self):
        directory, baseline, result, _ = self.fixture_build()
        with patch.object(common, "verify_hardware", return_value=result):
            build = package.verify_new_build(directory, "gbc", baseline)
        output = self.root / "package"
        for name, path in package.package_inputs(directory, "gbc", build).items():
            common.copy(path, output / name)
        for name, value in ((package.CHANGED_HDL, b"new HDL"),
                            ("scripts/build_and_export_xsa.tcl", (ROOT / "scripts/build_and_export_xsa.tcl").read_bytes())):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(value)
        with patch.object(common, "ROOT", self.root):
            package.verify_final_inputs({"gbc": directory}, {"gbc": build}, output)
            for path, error in ((result["bit"], "Selected input changed"),
                                (result["xsa"], "Selected input changed"),
                                (output / "inputs/gbc.bit", "Copied input changed"),
                                (output / "inputs/gbc.xsa", "Copied input changed"),
                                (self.root / package.CHANGED_HDL, "Build source changed")):
                original = path.read_bytes()
                path.write_bytes(original + b"corruption")
                with self.assertRaisesRegex(ValueError, error):
                    package.verify_final_inputs({"gbc": directory}, {"gbc": build}, output)
                path.write_bytes(original)

    def test_cli_accepts_only_two_explicit_core_dirs_and_pinned_baseline(self):
        args = ["--baseline-zip", "baseline.zip", "--baseline-sha256", "a" * 64,
                "--c64-pal-dir", "pal", "--gbc-dir", "gbc", "--output", "result"]
        parsed = package.parser().parse_args(args)
        self.assertEqual(parsed.c64_pal_dir, Path("pal"))
        for extra in ("--c64-ntsc-dir", "--supervisor-dir", "--workspace"):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                package.parser().parse_args(args + [extra, "unexpected"])
        self.assertEqual(common.BUILD_ID, "56494434")
        for invalid in ("5649443", "56494435extra", "../56494435"):
            with self.assertRaisesRegex(ValueError, "Invalid expected FPGA build ID"):
                common.verify_hardware(self.root, "GBC", build_id=invalid)


if __name__ == "__main__":
    unittest.main()
