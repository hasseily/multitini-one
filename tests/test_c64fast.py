# SPDX-License-Identifier: GPL-3.0-only
"""Simulator tests for the Multitini fast-disk ROM pair in the private ROM collection.

The pair is exercised on the py65 C64 + 1541 simulator with the parallel
cable and the 8 KiB drive RAM expansion enabled, exactly as the FPGA
enables them for the fast ROM profile. Set MULTITINI_SLOW_TESTS=1 to run
the 202 block LOAD.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(ROOT / "tests"))

from c64sim.d64 import D64  # noqa: E402
from c64sim.system import System  # noqa: E402
from test_c64sim import hello_program, make_test_disk  # noqa: E402

from private_core_roms import default_rom_source
STOCK = default_rom_source() / "c64/roms"
FAST_KERNAL = STOCK / "fast/kernal.bin"
FAST_DOS = STOCK / "fast/1541.bin"
PROVENANCE = ROOT / "c64fast" / "bin" / "PROVENANCE.json"
SLOW = os.environ.get("MULTITINI_SLOW_TESTS") == "1"
ENGINE_MAGIC_OFFSET = 0x9AF3 - 0x8000


def fast_system(image, **extra) -> System:
    return System(d64=image, parallel_cable=True, extram=True,
                  kernal_bytes=FAST_KERNAL.read_bytes(),
                  drive_rom_bytes=FAST_DOS.read_bytes(), **extra)


@unittest.skipUnless(FAST_KERNAL.is_file() and FAST_DOS.is_file(), "private fast ROM pair not supplied")
class FastRomBuildTests(unittest.TestCase):
    def test_images_are_present_and_sized(self):
        self.assertEqual(FAST_KERNAL.stat().st_size, 8192)
        self.assertEqual(FAST_DOS.stat().st_size, 16384)

    def test_provenance_matches_images(self):
        import hashlib
        record = json.loads(PROVENANCE.read_text(encoding="utf-8"))
        self.assertEqual(record["schema_version"], 1)
        outputs = record["outputs"]
        for name, path in (("kernal.bin", FAST_KERNAL), ("1541.bin", FAST_DOS)):
            self.assertEqual(outputs[name]["sha256"], hashlib.sha256(path.read_bytes()).hexdigest())
        stock = STOCK
        self.assertEqual(record["inputs"]["kernal"]["sha256"],
                         hashlib.sha256((stock / "kernal.bin").read_bytes()).hexdigest())
        self.assertEqual(record["inputs"]["1541"]["sha256"],
                         hashlib.sha256((stock / "1541.bin").read_bytes()).hexdigest())

    def test_rebuild_matches_committed_images(self):
        acme = shutil.which("acme") or shutil.which("acme.exe")
        if acme is None and not (Path.home() / "tools" / "acme" / "acme.exe").exists():
            self.skipTest("acme assembler not installed")
        result = subprocess.run([sys.executable, str(ROOT / "c64fast" / "build.py"), "--check"],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_stock_bytes_survive_outside_patch_regions(self):
        stock = (STOCK / "kernal.bin").read_bytes()
        fast = FAST_KERNAL.read_bytes()
        # KERNAL jump table, vectors and the message table are untouched.
        self.assertEqual(stock[0x1F81:0x2000], fast[0x1F81:0x2000])
        self.assertEqual(stock[0x10BD:0x10D8], fast[0x10BD:0x10D8])
        stock_dos = (STOCK / "1541.bin").read_bytes()
        fast_dos = FAST_DOS.read_bytes()
        self.assertEqual(stock_dos[0x3FE6:0x4000], fast_dos[0x3FE6:0x4000])
        self.assertEqual(stock_dos[0x0000:0x009F], fast_dos[0x0000:0x009F])


@unittest.skipUnless(FAST_KERNAL.is_file() and FAST_DOS.is_file(), "private fast ROM pair not supplied")
class FastRomAcceptanceTests(unittest.TestCase):
    """The fast pair talking over the simulated cable and IEC bus."""

    booted = None
    boot_seconds = 0.0

    @classmethod
    def setUpClass(cls):
        cls.image = make_test_disk()
        system = fast_system(cls.image)
        cls.boot_seconds = system.boot_to_ready(5.0)
        cls.booted = system

    def setUp(self):
        self.system = self.booted.snapshot()

    def run_line(self, line, budget=30.0):
        self.system.type_line(line)
        return self.system.run_until_ready(budget)

    def screen(self):
        return [line.rstrip() for line in self.system.screen_text() if line.strip()]

    def test_boot_to_ready(self):
        self.assertLess(self.boot_seconds, 3.0)
        self.assertTrue(any("READY." in line for line in self.system.screen_text()))
        drive = self.system.drive.cpu
        self.assertEqual(drive.ByteAt(0x35) & 0x80, 0x80, "drive did not find its RAM expansion")
        self.assertEqual(drive.ByteAt(0x1803), 0x00, "drive port A must be input at rest")
        self.assertEqual(drive.ByteAt(0x180C), 0x0F, "CA2 must idle high in manual mode")

    def test_directory_listing(self):
        self.run_line('LOAD"$",8')
        self.run_line("LIST")
        screen = self.screen()
        self.assertTrue(any('"TESTDISK        " TD 2A' in line for line in screen))
        self.assertTrue(any('"HELLO"' in line and "PRG" in line for line in screen))
        self.assertTrue(any("655 BLOCKS FREE." in line for line in screen))
        self.assertFalse(any("ERROR" in line for line in screen))

    def check_hello_loaded(self):
        program = hello_program()
        end = 0x0801 + len(program) - 2
        self.assertEqual(self.system.ram_bytes(0x0801, end), program[2:])
        self.assertEqual(self.system.peek_word(0xAE), end)
        self.assertEqual(self.system.peek(0x90), 0x40)

    def test_load_prg_is_byte_exact_and_fast(self):
        seconds = self.run_line('LOAD"HELLO",8')
        screen = self.screen()
        self.assertFalse(any("ERROR" in line for line in screen))
        self.assertEqual(screen[-3:], ["SEARCHING FOR HELLO", "LOADING", "READY."])
        self.check_hello_loaded()
        self.assertLess(seconds, 4.0, "the stock pair needs about 6.5 s here")
        extram = self.system.drive_extram()
        self.assertEqual(extram[ENGINE_MAGIC_OFFSET:ENGINE_MAGIC_OFFSET + 2], b"MT")

    def test_load_with_secondary_address_one(self):
        self.run_line('LOAD"HELLO",8,1')
        self.assertFalse(any("ERROR" in line for line in self.screen()))
        self.check_hello_loaded()

    def test_second_load_reuses_the_resident_engine(self):
        first = self.run_line('LOAD"HELLO",8')
        self.run_line("NEW")
        second = self.run_line('LOAD"HELLO",8')
        self.check_hello_loaded()
        self.assertLessEqual(second, first + 0.05)

    def test_verify(self):
        self.run_line('LOAD"HELLO",8')
        self.run_line('VERIFY"HELLO",8')
        screen = self.screen()
        self.assertTrue(any("VERIFYING" in line for line in screen))
        self.assertTrue(any(line.strip() == "OK" for line in screen))
        self.assertFalse(any("ERROR" in line for line in screen))
        self.system.poke(0x0810, self.system.peek(0x0810) ^ 0xFF)
        self.run_line('VERIFY"HELLO",8')
        self.assertTrue(any("?VERIFY  ERROR" in line for line in self.screen()))

    def test_file_not_found(self):
        self.run_line('LOAD"NOPE",8')
        self.assertTrue(any("?FILE NOT FOUND  ERROR" in line for line in self.screen()))

    def test_save_round_trip(self):
        self.run_line('LOAD"HELLO",8')
        seconds = self.run_line('SAVE"NEW",8')
        self.assertFalse(any("ERROR" in line for line in self.screen()))
        # SAVE is bound by the DOS write-and-verify job (about 330 ms per
        # block on both pairs); the stock pair needs about 7.1 s here.
        self.assertLess(seconds, 6.5)
        image = self.system.disk_image()
        self.assertEqual(image.read_file("NEW"), hello_program())
        entry = image.find_entry("NEW")
        self.assertEqual(entry.ftype, "PRG")

    def test_sequential_file_input(self):
        self.run_line('10 OPEN 2,8,2,"NOTES,S,R"')
        self.run_line("20 INPUT#2,A$")
        self.run_line("30 CLOSE 2")
        self.run_line("40 PRINT A$")
        self.run_line("RUN")
        screen = self.screen()
        self.assertFalse(any("ERROR" in line for line in screen))
        self.assertEqual(screen[-2:], ["HELLO SEQ", "READY."])

    def test_error_channel(self):
        self.run_line("10 OPEN 15,8,15")
        self.run_line("20 INPUT#15,A,B$,C,D")
        self.run_line("30 PRINT A;B$;C;D")
        self.run_line("40 CLOSE 15")
        self.run_line("RUN")
        screen = self.screen()
        self.assertFalse(any("ERROR" in line for line in screen))
        # The first read after power-on returns the DOS version message.
        self.assertTrue(any(" 73 CBM DOS V2.6 1541 0  0" in line for line in screen), screen[-3:])
        self.run_line("RUN")
        self.assertTrue(any(" 0 OK 0  0" in line for line in self.screen()), self.screen()[-3:])

    def test_stop_key_breaks_a_load(self):
        image = D64.blank("STOPDISK", "ST")
        data = bytes((i * 3) & 0xFF for i in range(40 * 254 - 2))
        image.add_file("LONG", b"\x00\x40" + data)
        system = fast_system(image)
        system.boot_to_ready(5.0)
        system.type_line('LOAD"LONG",8,1')
        system.run_for(1.2)
        system.press_stop()
        system.run_until_ready(10.0)
        system.release_keys()
        self.assertTrue(any("BREAK" in line for line in system.screen_text()))

    @unittest.skipUnless(SLOW, "set MULTITINI_SLOW_TESTS=1 to run the 202 block LOAD")
    def test_slow_big_load(self):
        image = D64.blank("BIGDISK", "BG")
        data = bytearray((i * 7 + 3) & 0xFF for i in range(202 * 254 - 2))
        data[0x400:0x404] = b"\x00" * 4
        _, _, blocks = image.add_file("BIG", b"\x00\x04" + bytes(data))
        self.assertEqual(blocks, 202)
        system = fast_system(image)
        system.boot_to_ready(5.0)
        system.type_line('LOAD"BIG",8,1')
        seconds = system.run_until_ready(120.0)
        print("\n  202 block LOAD: %.3f simulated s (stock pair: about 127 s)" % seconds)
        self.assertLess(seconds, 20.0)
        loaded = system.ram_bytes(0x0400, 0x0400 + len(data))
        # BASIC relinks the program at $0801 after LOAD, so skip its link bytes.
        self.assertEqual(loaded[:0x400], bytes(data[:0x400]))
        self.assertEqual(loaded[0x406:], bytes(data[0x406:]))


if __name__ == "__main__":
    unittest.main()
