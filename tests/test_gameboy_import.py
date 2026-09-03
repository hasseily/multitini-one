# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import csv
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class GameBoyImportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(
            (ROOT / "third_party/gameboy/import_manifest.json").read_text())
        cls.lock = json.loads(
            (ROOT / "provenance/imports.lock.json").read_text())

    def test_rights_gate_is_resolved(self):
        self.assertEqual(self.manifest["unresolved"], [])
        licenses = {group["license"]
                    for group in self.manifest["resolved_groups"]}
        self.assertIn("MIT", licenses)
        self.assertIn("GPL-2.0-or-later", licenses)

    def test_allowlist_equals_staged_and_locked_source(self):
        expected = {
            path
            for group in self.manifest["resolved_groups"]
            for path in group["paths"]
        }
        destination = ROOT / self.manifest["destination"]
        staged = {
            path.relative_to(destination).as_posix()
            for path in destination.rglob("*") if path.is_file()
        }
        locked = {
            entry["source_path"] for entry in self.lock["imports"]
            if entry["source"] == "MiSTer Game Boy"
        }
        self.assertEqual(staged, expected)
        self.assertEqual(locked, expected)

    def test_only_the_approved_open_boot_image_is_staged(self):
        destination = ROOT / self.manifest["destination"]
        image_suffixes = {".bin", ".gb", ".gbc", ".mif", ".rbf", ".rom"}
        images = {
            path.relative_to(destination).as_posix()
            for path in destination.rglob("*")
            if path.is_file() and path.suffix.lower() in image_suffixes
        }
        self.assertEqual(images, {"BootROMs/cgb_boot.mif"})

    def test_every_imported_file_has_an_explicit_license_row(self):
        with (ROOT / "licenses/files.csv").open(newline="", encoding="utf-8") as stream:
            rows = list(csv.DictReader(stream))
        by_path = {row["path"]: row for row in rows}
        destination = ROOT / self.manifest["destination"]
        for path in destination.rglob("*"):
            if path.is_file():
                relative = path.relative_to(ROOT).as_posix()
                self.assertIn(relative, by_path)
                self.assertEqual(by_path[relative]["commit"],
                                 self.manifest["source"]["commit"])

    def test_product_port_uses_generated_hex_not_upstream_mif_path(self):
        upstream = (ROOT / "third_party/gameboy/upstream/rtl/gb.v").read_text()
        product = (ROOT / "hdl/core/gameboy/ports/gb.v").read_text()
        self.assertIn('"BootROMs/cgb_boot.mif"', upstream)
        self.assertNotIn('"BootROMs/cgb_boot.mif"', product)
        self.assertIn('"cgb_boot.mem"', product)
        self.assertIn("Multitini port:", product)

        upstream_cart = (ROOT / "third_party/gameboy/upstream/rtl/cart.v").read_text()
        product_cart = (ROOT / "hdl/core/gameboy/ports/cart.v").read_text()
        self.assertIn("module cart", upstream_cart)
        self.assertIn("Multitini port:", product_cart)

    def test_boot_image_has_source_and_attribution(self):
        readme = (ROOT / "third_party/gameboy/upstream/BootROMs/README.md").read_text()
        source = (ROOT / "third_party/gameboy/upstream/BootROMs/src/cgb_boot.asm").read_text()
        generated = ROOT / "hdl/core/gameboy/boot/cgb_boot.mem"
        self.assertIn("SameBoy", readme)
        self.assertIn("MIT", source)
        self.assertEqual(len(generated.read_text().splitlines()), 4096)


if __name__ == "__main__":
    unittest.main()
