#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Original synthetic fixtures for package-audit address and corruption checks."""
import copy
from pathlib import Path
import struct
import tempfile
import unittest

import audit_vectrex_package as audit


def executable(path):
    data = bytearray(256)
    data[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHIIIIIHHHHHH", data, 16,
                     2, 40, 1, 0x1000, 52, 0, 0, 52, 32, 3, 0, 0, 0)
    # Separate initialized regions and a BSS-only region, like a split ELF.
    struct.pack_into("<8I", data, 52, 1, 200, 0x1000, 0x1000, 3, 16, 5, 4)
    struct.pack_into("<8I", data, 84, 1, 220, 0x8000, 0x8000, 4, 100, 6, 4)
    struct.pack_into("<8I", data, 116, 1, 240, 0xf000, 0xf000, 0, 64, 6, 4)
    data[200:203] = b"ABC"
    data[220:224] = b"WXYZ"
    path.write_bytes(data)


def partition(address, data, entry):
    return {"attributes": 0x10, "entry_address": entry,
            "load_address": address, "bytes": len(data), "payload": data}


class PackageAuditTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "original.elf"
        executable(self.path)
        self.parts = [partition(0x1000, b"ABC\0", 0x1000),
                      partition(0x8000, b"WXYZ", 0)]

    def test_split_segments_and_bss(self):
        result = audit.verify_elf(self.parts, self.path)
        self.assertEqual([p["elf_segments"] for p in result["partitions"]], [[0], [1]])
        self.assertFalse(result["pt_load"][2]["initialized_bytes_verified"])
        self.assertEqual(result["pt_load"][2]["bss_bytes_not_shipped"], 64)

    def test_payload_corruption(self):
        self.parts[1]["payload"] = b"WX!Z"
        with self.assertRaisesRegex(ValueError, "physical address 0x8002"):
            audit.verify_elf(self.parts, self.path)

    def test_load_address_must_match(self):
        self.parts[1]["load_address"] += 4
        with self.assertRaisesRegex(ValueError, "Unmatched partition"):
            audit.verify_elf(self.parts, self.path)

    def test_missing_and_duplicate_segments(self):
        with self.assertRaisesRegex(ValueError, "segment missing"):
            audit.verify_elf(self.parts[:1], self.path)
        self.parts.append(copy.deepcopy(self.parts[1]))
        with self.assertRaisesRegex(ValueError, "packaged twice"):
            audit.verify_elf(self.parts, self.path)

    def test_nonzero_padding_and_wrong_entry(self):
        self.parts[0]["payload"] = b"ABC!"
        with self.assertRaisesRegex(ValueError, "physical address 0x1003"):
            audit.verify_elf(self.parts, self.path)
        self.parts[0]["entry_address"] = 0x2000
        with self.assertRaisesRegex(ValueError, "entry address"):
            audit.verify_elf(self.parts, self.path)

    def test_boot_header_checksum(self):
        data = bytearray(512)
        struct.pack_into("<2I", data, 0x20, 0xaa995566, 0x584c4e58)
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            audit.parse_firmware(data)

    def test_core_manifest_corruption(self):
        root = Path(self.directory.name)
        binary = root / "vectrex-native.bin"
        data = b"original test vector package"
        binary.write_bytes(data)
        manifest = root / "core.cfg"
        text = ("schema=1\nid=vectrex\nname=Vectrex\ncore_id=0x56454358\nabi=0.11\n"
                "content_root=0:/multitini/cores/vectrex/media\n"
                f"# native.sha256={audit.digest(data)}\n"
                f"variant=native|Vectrex|vectrex-native.bin|{len(data)}|{audit.binascii.crc32(data)}\n")
        manifest.write_text(text, encoding="ascii")
        audit.verify_core_manifest(manifest, binary, data, "0.11")
        with self.assertRaisesRegex(ValueError, "size/CRC32/SHA256 mismatch"):
            audit.verify_core_manifest(manifest, binary, data + b"!", "0.11")
        manifest.write_text(text.replace("abi=0.11", "abi=0.10"), encoding="ascii")
        with self.assertRaisesRegex(ValueError, "ABI"):
            audit.verify_core_manifest(manifest, binary, data, "0.11")


if __name__ == "__main__":
    unittest.main()
