# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import csv
import json
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def without_trailing_whitespace(text: str) -> str:
    """Compare imported VHDL bodies independent of checkout whitespace."""
    return "\n".join(line.rstrip() for line in text.splitlines())


class C64ImportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.manifest = json.loads(
            (ROOT / "third_party/c64/import_manifest.json").read_text())
        cls.lock = json.loads(
            (ROOT / "provenance/imports.lock.json").read_text())

    def test_assumed_grant_is_explicit_and_gate_is_open(self):
        self.assertEqual(self.manifest["unresolved"], [])
        assumption = self.manifest["redistribution_assumption"]
        self.assertEqual(assumption["authorized_by"], "repository owner")
        self.assertIn("GPL-2.0-or-later", assumption["terms"])
        for excluded in ("ROMs", "media", "generated IP", "binaries"):
            self.assertIn(excluded, assumption["excludes"])

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
            if entry["source"] == "MiSTer C64"
        }
        self.assertEqual(staged, expected)
        self.assertEqual(locked, expected)

    def test_no_restricted_content_was_staged(self):
        restricted = {
            ".bin", ".crt", ".d64", ".d81", ".exe", ".g64", ".hex",
            ".html", ".mem", ".mif", ".prg", ".rbf", ".rom", ".tap",
        }
        destination = ROOT / self.manifest["destination"]
        self.assertFalse(any(path.suffix.lower() in restricted
                             for path in destination.rglob("*") if path.is_file()))

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

    def test_product_buslogic_has_no_embedded_rom_path(self):
        source = (ROOT / "hdl/core/c64/multitini_c64_buslogic.vhd").read_text()
        self.assertNotIn(".mif", source.lower())
        self.assertIn('system_rom_wr <= c64rom_wr when bios = "00"', source)
        self.assertIn('char_rom_wr <= c64rom_wr when bios = "01"', source)
        self.assertEqual(source.count("entity work.dprom"), 2)

    def test_product_memory_ports_disable_initialization(self):
        dprom = (ROOT / "hdl/core/c64/ports/dprom.vhd").read_text()
        self.assertIn('assert INIT_FILE = ""', dprom)
        self.assertNotIn("ram_init_file", dprom)
        sources = (ROOT / "hdl/hdl_sources.txt").read_text()
        self.assertIn("core/c64/ports/dprom.vhd", sources)
        self.assertIn("core/c64/ports/spram.vhd", sources)
        self.assertNotIn("upstream/rtl/dprom.vhd", sources)
        self.assertNotIn("upstream/rtl/spram.vhd", sources)

    def test_cpu_port_only_uses_direct_t65_entity_instantiation(self):
        upstream = (ROOT / "third_party/c64/upstream/rtl/cpu_6510.vhd").read_text()
        product = (ROOT / "hdl/core/c64/ports/cpu_6510.vhd").read_text()
        marker = "-- BEGIN PINNED SOURCE BODY\n"
        self.assertIn(marker, product)
        expected = upstream.replace(
            "\tcpu: work.T65", "\tcpu: entity work.T65", 1
        )
        self.assertEqual(
            without_trailing_whitespace(product.split(marker, 1)[1]),
            without_trailing_whitespace(expected),
        )
        sources = (ROOT / "hdl/hdl_sources.txt").read_text()
        self.assertIn("core/c64/ports/cpu_6510.vhd", sources)
        self.assertNotIn("../third_party/c64/upstream/rtl/cpu_6510.vhd", sources)

    def test_video_sync_port_only_exposes_locked_pixel_tick(self):
        upstream = (
            ROOT / "third_party/c64/upstream/rtl/video_sync.vhd"
        ).read_text()
        product = (
            ROOT / "hdl/core/c64/ports/video_sync.vhd"
        ).read_text()
        marker = "-- BEGIN PINNED SOURCE BODY WITH PIXEL_TICK PORT\n"
        self.assertIn(marker, product)
        expected = upstream.replace(
            "\tvblank : out std_logic\n",
            "\tvblank : out std_logic;\n\tpixel_tick : out std_logic\n",
            1,
        ).replace(
            "\nend architecture;",
            "\npixel_tick <= '1' when clk_cnt = \"00\" and "
            "pause = '0' else '0';\n\nend architecture;",
            1,
        )
        self.assertEqual(
            without_trailing_whitespace(product.split(marker, 1)[1]),
            without_trailing_whitespace(expected),
        )
        sources = (ROOT / "hdl/hdl_sources.txt").read_text()
        self.assertIn("core/c64/ports/video_sync.vhd", sources)
        self.assertNotIn(
            "../third_party/c64/upstream/rtl/video_sync.vhd", sources
        )
        with (ROOT / "licenses/files.csv").open(
            newline="", encoding="utf-8"
        ) as stream:
            by_path = {row["path"]: row for row in csv.DictReader(stream)}
        row = by_path["hdl/core/c64/ports/video_sync.vhd"]
        self.assertEqual(row["commit"], self.manifest["source"]["commit"])
        self.assertIn("pixel phase", row["notes"])


if __name__ == "__main__":
    unittest.main()
