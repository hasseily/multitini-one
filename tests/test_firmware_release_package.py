# SPDX-License-Identifier: GPL-3.0-only
"""Named firmware release integrity gates; no native tools or hardware."""
from __future__ import annotations

import json
from pathlib import Path
import sys
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import package_firmware_release as release


class FirmwareReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def freeze(self):
        files = release.inventory(self.root)
        release.p.write_json(self.root / "manifest.json", {"files": files})
        (self.root / "SHA256SUMS.txt").write_text("".join(
            f"{record['sha256']}  {name}\n" for name, record in release.inventory(self.root).items()))

    def test_baseline_rejects_corruption_extra_files_and_missing_checksum(self):
        member = self.root / "supervisor.bit"
        member.write_bytes(b"selected FPGA image")
        self.freeze()
        release.verify_directory(self.root)
        member.write_bytes(b"different FPGA image")
        with self.assertRaisesRegex(ValueError, "input changed"):
            release.verify_directory(self.root)
        member.write_bytes(b"selected FPGA image")
        extra = self.root / "unexpected.bin"
        extra.write_bytes(b"extra")
        with self.assertRaisesRegex(ValueError, "membership"):
            release.verify_directory(self.root)
        extra.unlink()
        (self.root / "SHA256SUMS.txt").write_text("")
        with self.assertRaisesRegex(ValueError, "checksum membership"):
            release.verify_directory(self.root)

    def test_source_archive_rejects_changed_member(self):
        expected = b"exact build source"
        (self.root / "source-manifest.json").write_text(json.dumps({"hdl/core.sv": release.p.digest(expected)}))
        with zipfile.ZipFile(self.root / "source.zip", "w") as archive:
            archive.writestr("hdl/core.sv", b"different source")
        with self.assertRaisesRegex(ValueError, "Source archive differs"):
            release.verify_source_archive(self.root)

    def test_source_archive_rejects_duplicate_members(self):
        import warnings
        expected = b"exact build source"
        (self.root / "source-manifest.json").write_text(json.dumps({"hdl/core.sv": release.p.digest(expected)}))
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            with zipfile.ZipFile(self.root / "source.zip", "w") as archive:
                archive.writestr("hdl/core.sv", expected)
                archive.writestr("hdl/core.sv", expected)
        with self.assertRaisesRegex(ValueError, "membership/integrity"):
            release.verify_source_archive(self.root)

    def test_retained_members_preserve_hardware_cores_and_exact_baseline_source(self):
        names = ["inputs/fsbl.elf", "inputs/supervisor.bit", "inputs/platform.xsa",
                 "inputs/used-vitis-platform.zip", "inputs/diagnostic.elf",
                 "inputs/video_worker.elf", "inputs/video_core1_blob.bin", "sd/FIRMWARE.BIN",
                 "sd/multitini/cores/c64/core.cfg", "sd/multitini/cores/c64/pal.bin",
                 "evidence/supervisor/source-inputs.sources.tar.gz"]
        retained = release.retained_members({"files": dict.fromkeys(names)})
        for name in names[:4] + names[8:10]:
            self.assertEqual(retained[name], name)
        for name in names[4:8]:
            self.assertNotIn(name, retained)
        self.assertEqual(retained["evidence/baseline/source.zip"], "source.zip")
        self.assertEqual(retained["evidence/baseline/supervisor/source-inputs.sources.tar.gz"],
                         "evidence/supervisor/source-inputs.sources.tar.gz")

    def test_firmware_only_gate_rejects_changed_or_added_hardware_sources(self):
        previous = {"hdl/core.sv": "a", "constraints/board.xdc": "b", "spec/interface_v0.json": "c",
                    "firmware/main.c": "old"}
        current = {**previous, "firmware/main.c": "new"}
        release.verify_hardware_source(previous, current)
        for changed in ({**current, "hdl/core.sv": "changed"},
                        {**current, "constraints/new.xdc": "added"},
                        {**current, "spec/interface_v0.json": "changed"}):
            with self.assertRaisesRegex(ValueError, "HDL, constraints or interface"):
                release.verify_hardware_source(previous, changed)

    def test_archive_paths_reject_traversal_absolute_and_drive_paths(self):
        for unsafe in ("../escape", "/absolute", "a\\b", "C:/drive", ""):
            with self.assertRaisesRegex(ValueError, "Unsafe package member"):
                release.safe(unsafe)

    def test_cpu1_retains_only_exact_original_with_build_id_only_rebuild(self):
        note = struct.pack("<III", 4, 20, 3) + b"GNU\0"
        old_text, new_text = b"instructions" + note + b"a" * 20, b"instructions" + note + b"b" * 20
        old, new = self.root / "old.bin", self.root / "new.bin"
        old.write_bytes(old_text + b"unchanged data")
        new.write_bytes(new_text + b"unchanged data")
        with patch.object(release.p, "elf_section", side_effect=[old_text, new_text]):
            comparison = release.p.compare_cpu1_build_id("old.elf", old, "new.elf", new)
        self.assertEqual(comparison["comparison"], "GNU-build-id-only")
        self.assertEqual(comparison["different_bytes"], 20)
        new.write_bytes(new_text + b"changed data")
        with patch.object(release.p, "elf_section", side_effect=[old_text, new_text]):
            with self.assertRaisesRegex(ValueError, "payload size changed"):
                release.p.compare_cpu1_build_id("old.elf", old, "new.elf", new)
        new.write_bytes(new_text + b"unchanged datX")
        with patch.object(release.p, "elf_section", side_effect=[old_text, new_text]):
            with self.assertRaisesRegex(ValueError, "outside the GNU"):
                release.p.compare_cpu1_build_id("old.elf", old, "new.elf", new)

    def test_cpu1_note_qualification_rejects_instruction_changes_and_wrong_note(self):
        note = struct.pack("<III", 4, 20, 3) + b"GNU\0"
        old_text, new_text = b"instructions" + note + b"a" * 20, b"instructionX" + note + b"b" * 20
        old, new = self.root / "old.bin", self.root / "new.bin"
        old.write_bytes(old_text)
        new.write_bytes(new_text)
        with patch.object(release.p, "elf_section", side_effect=[old_text, new_text]):
            with self.assertRaisesRegex(ValueError, "outside the GNU"):
                release.p.compare_cpu1_build_id("old.elf", old, "new.elf", new)
        bad_text = b"instructions" + note.replace(b"GNU", b"BAD") + b"b" * 20
        new.write_bytes(bad_text)
        with patch.object(release.p, "elf_section", side_effect=[old_text, bad_text]):
            with self.assertRaisesRegex(ValueError, "lacks a terminal GNU"):
                release.p.compare_cpu1_build_id("old.elf", old, "new.elf", new)


if __name__ == "__main__":
    unittest.main()
