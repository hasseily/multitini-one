# SPDX-License-Identifier: GPL-3.0-only
from __future__ import annotations

import importlib.util
import unittest
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts" / "serial_c64_rom_upload.py"
SPEC = importlib.util.spec_from_file_location("serial_c64_rom_upload", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
UPLOAD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(UPLOAD)


def patterned(size: int, seed: int) -> bytes:
    return bytes((seed + index) & 0xFF for index in range(size))


class SerialC64RomUploadTests(unittest.TestCase):
    def test_bundled_roms_are_the_command_line_defaults(self):
        args = UPLOAD.parse_args([])

        self.assertEqual(args.basic, ROOT / "third_party/c64/ROMs/basic.901226-01.bin")
        self.assertEqual(args.kernal, ROOT / "third_party/c64/ROMs/kernal.901227-03.bin")
        self.assertEqual(
            args.character,
            ROOT / "third_party/c64/ROMs/characters.901225-01.bin",
        )

        overrides = UPLOAD.parse_args(
            [
                "--basic", "alternate-basic.bin",
                "--kernal", "alternate-kernal.bin",
                "--character", "alternate-character.bin",
            ]
        )
        self.assertEqual(overrides.basic, Path("alternate-basic.bin"))
        self.assertEqual(overrides.kernal, Path("alternate-kernal.bin"))
        self.assertEqual(overrides.character, Path("alternate-character.bin"))

    def test_bundle_order_size_and_exact_command(self):
        basic = patterned(UPLOAD.BASIC_SIZE, 0x10)
        kernal = patterned(UPLOAD.KERNAL_SIZE, 0x40)
        character = patterned(UPLOAD.CHARACTER_SIZE, 0x80)
        bundle = UPLOAD.build_bundle(basic, kernal, character)

        self.assertEqual(len(bundle), 20480)
        self.assertEqual(bundle, basic + kernal + character)
        crc32 = zlib.crc32(bundle) & 0xFFFFFFFF
        self.assertEqual(
            UPLOAD.command_for_bundle(bundle),
            f":c64rx 20480 0x{crc32:08X}\r".encode("ascii"),
        )

    def test_wrong_size_and_uniform_roms_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "exactly 8192"):
            UPLOAD.validate_rom_bytes("BASIC", b"x" * 8191, 8192)
        with self.assertRaisesRegex(ValueError, "uniform"):
            UPLOAD.validate_rom_bytes("KERNAL", b"\xff" * 8192, 8192)
        with self.assertRaisesRegex(ValueError, "uniform"):
            UPLOAD.validate_rom_bytes("character", b"\x00" * 4096, 4096)

    def test_xmodem_crc_and_packet_are_exact(self):
        self.assertEqual(UPLOAD.crc16_ccitt(b"123456789"), 0x31C3)
        payload = patterned(UPLOAD.XMODEM_BLOCK_SIZE, 0x21)
        packet = UPLOAD.xmodem_packet(7, payload)
        self.assertEqual(len(packet), 1029)
        self.assertEqual(packet[:3], bytes([UPLOAD.XMODEM_STX, 7, 0xF8]))
        self.assertEqual(packet[3:-2], payload)
        crc = UPLOAD.crc16_ccitt(payload)
        self.assertEqual(packet[-2:], bytes([crc >> 8, crc & 0xFF]))

    def test_script_never_writes_or_caches_rom_files(self):
        source = SCRIPT.read_text(encoding="utf-8")
        self.assertNotIn("write_bytes(", source)
        self.assertNotIn("write_text(", source)
        self.assertNotIn("NamedTemporaryFile", source)


if __name__ == "__main__":
    unittest.main()
