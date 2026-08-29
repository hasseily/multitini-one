# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import hashlib
import json
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROVENANCE = ROOT / "provenance" / "c64_roms.json"
EXPECTED_ROMS = (
    (
        "third_party/c64/ROMs/basic.901226-01.bin",
        8192,
        "89878cea0a268734696de11c4bae593eaaa506465d2029d619c0e0cbccdfa62d",
    ),
    (
        "third_party/c64/ROMs/kernal.901227-03.bin",
        8192,
        "83c60d47047d7beab8e5b7bf6f67f80daa088b7a6a27de0d7e016f6484042721",
    ),
    (
        "third_party/c64/ROMs/characters.901225-01.bin",
        4096,
        "fd0d53b8480e86163ac98998976c72cc58d5dd8eb824ed7b829774e74213b420",
    ),
)


class C64RomProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.record = json.loads(PROVENANCE.read_text(encoding="utf-8"))

    def test_each_rom_matches_the_exact_provenance_record(self):
        self.assertEqual(self.record["schema_version"], 1)
        self.assertEqual(
            [item["path"] for item in self.record["roms"]],
            [path for path, _, _ in EXPECTED_ROMS],
        )

        for item, (relative, expected_size, expected_sha256) in zip(
            self.record["roms"], EXPECTED_ROMS, strict=True
        ):
            data = (ROOT / relative).read_bytes()
            self.assertEqual(item["size"], expected_size)
            self.assertEqual(item["sha256"], expected_sha256)
            self.assertEqual(len(data), expected_size)
            self.assertEqual(hashlib.sha256(data).hexdigest(), expected_sha256)

    def test_bundle_order_size_crc_and_sha256_are_exact(self):
        expected_order = [path for path, _, _ in EXPECTED_ROMS]
        bundle = b"".join((ROOT / path).read_bytes() for path in expected_order)
        record = self.record["bundle"]

        self.assertEqual(record["order"], expected_order)
        self.assertEqual(record["size"], 20480)
        self.assertEqual(record["crc32"], "0x260CD1D8")
        self.assertEqual(
            record["sha256"],
            "6d86861abdf674bc3d256e101ffd44f8297004aa8990a267d55c940d755f9fff",
        )
        self.assertEqual(len(bundle), record["size"])
        self.assertEqual(f"0x{zlib.crc32(bundle) & 0xFFFFFFFF:08X}", record["crc32"])
        self.assertEqual(hashlib.sha256(bundle).hexdigest(), record["sha256"])

    def test_record_does_not_invent_a_license(self):
        decision = self.record["distribution_decision"]
        self.assertEqual(decision["authority"], "repository owner")
        self.assertEqual(decision["license_status"], "NOASSERTION")
        self.assertIn("No explicit license", decision["notice"])


if __name__ == "__main__":
    unittest.main()
