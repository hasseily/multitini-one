# SPDX-License-Identifier: GPL-3.0-only
"""Tests for the scripts/c64sim C64 + 1541 simulator.

The acceptance tests boot the stock ROMs once per class and continue
from deep copies of that state. Set MULTITINI_SLOW_TESTS=1 to run the
202 block LOAD, which takes about two simulated minutes.
"""

from __future__ import annotations

import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from c64sim import D64, GCRDisk, SimTimeout, System, basic_program  # noqa: E402
from c64sim import gcr  # noqa: E402
from c64sim.__main__ import main as cli_main  # noqa: E402
from c64sim.cia import CIA6526  # noqa: E402
from c64sim.via import VIA6522  # noqa: E402

SLOW = os.environ.get("MULTITINI_SLOW_TESTS") == "1"
ROMS = ROOT / "third_party" / "c64" / "ROMs"


def hello_program() -> bytes:
    """A 2 KiB BASIC program of REM lines that survives relinking."""
    lines = [(10 * (i + 1), b"\x8f" + (b"LINE %d " % i).ljust(60, b"X")) for i in range(30)]
    return basic_program(lines)


def make_test_disk() -> D64:
    image = D64.blank("TESTDISK", "TD")
    image.add_file("HELLO", hello_program())
    image.add_file("NOTES", b"HELLO SEQ", "SEQ")
    return image


class GcrTests(unittest.TestCase):
    def test_encode_decode_round_trip(self):
        data = bytes(range(256))
        self.assertEqual(gcr.decode(gcr.encode(data), strict=True), data)
        self.assertEqual(len(gcr.encode(data)), 320)

    def test_header_and_data_block_lengths(self):
        self.assertEqual(len(gcr.encode(gcr.header_block(18, 0, 0x54, 0x44))), 10)
        self.assertEqual(len(gcr.encode(gcr.data_block(bytes(256)))), 325)

    def test_zone_layout(self):
        self.assertEqual([gcr.sectors_per_track(t) for t in (1, 17, 18, 24, 25, 30, 31, 35)],
                         [21, 21, 19, 19, 18, 18, 17, 17])
        self.assertEqual([gcr.track_capacity(t) for t in (1, 18, 25, 31)], [7692, 7142, 6666, 6250])
        self.assertEqual(gcr.ZONE_BYTE_PERIOD_US, (32, 30, 28, 26))

    def test_track_round_trip_includes_sector_zero(self):
        sectors = [bytes([(s * 3 + i) & 0xFF for i in range(256)]) for s in range(21)]
        raw = gcr.build_track(5, sectors, 0x41, 0x42)
        self.assertEqual(len(raw), 7692)
        self.assertEqual(raw[:5], b"\xff" * 5)
        decoded, errors = gcr.decode_track(raw)
        self.assertEqual(errors, [])
        self.assertEqual(sorted(decoded), list(range(21)))
        for sector in range(21):
            self.assertEqual(decoded[sector], sectors[sector])

    def test_disk_round_trip(self):
        image = make_test_disk()
        disk = GCRDisk.from_d64(image)
        self.assertEqual((disk.id1, disk.id2), (ord("T"), ord("D")))
        self.assertIsNone(disk.tracks[1])
        back = disk.to_d64()
        self.assertEqual(back.decode_errors, [])
        self.assertEqual(back.compare(image), [])

    def test_checksum_error_is_reported(self):
        sectors = [bytes(256) for _ in range(17)]
        raw = gcr.build_track(35, sectors, 1, 2)
        raw[5 + 10 + 9 + 5 + 20] ^= 0x01
        decoded, errors = gcr.decode_track(raw)
        self.assertNotIn(0, decoded)
        self.assertEqual(len(errors), 1)


class D64Tests(unittest.TestCase):
    def test_blank_disk_bam(self):
        image = D64.blank("MY DISK", "ID")
        bam = image.read_sector(18, 0)
        self.assertEqual(bam[:4], b"\x12\x01\x41\x00")
        self.assertEqual(bam[0xA5:0xA7], b"2A")
        self.assertEqual(image.name, "MY DISK")
        self.assertEqual(image.disk_id, "ID")
        self.assertEqual(image.blocks_free(), 664)
        self.assertFalse(image.is_free(18, 0))
        self.assertFalse(image.is_free(18, 1))
        self.assertTrue(image.is_free(17, 0))

    def test_dos_allocation_order(self):
        image = D64.blank("ALLOC", "AL")
        payload = bytes(254 * 4)
        track, sector, blocks = image.add_file("FOUR", payload)
        self.assertEqual((track, sector, blocks), (17, 0, 4))
        chain = []
        t, s = track, sector
        while t:
            chain.append((t, s))
            block = image.read_sector(t, s)
            t, s = block[0], block[1]
        self.assertEqual(chain, [(17, 0), (17, 10), (17, 20), (17, 8)])
        self.assertEqual(image.blocks_free(), 660)
        entry = image.directory()[0]
        self.assertEqual((entry.name, entry.ftype, entry.blocks), ("FOUR", "PRG", 4))
        self.assertEqual(image.read_file("FOUR"), payload)

    def test_files_move_to_the_next_track_when_full(self):
        image = D64.blank("FULL", "FL")
        image.add_file("BIG", bytes(254 * 25))
        block = image.read_sector(17, 0)
        tracks = {17}
        t, s = block[0], block[1]
        while t:
            tracks.add(t)
            block = image.read_sector(t, s)
            t, s = block[0], block[1]
        self.assertEqual(tracks, {17, 16})
        self.assertEqual(image.free_on_track(17), 0)

    def test_directory_entries_and_last_sector_length(self):
        image = make_test_disk()
        names = [(e.name, e.ftype, e.blocks) for e in image.directory()]
        self.assertEqual(names, [("HELLO", "PRG", 8), ("NOTES", "SEQ", 1)])
        self.assertEqual(image.read_file("NOTES"), b"HELLO SEQ")
        self.assertEqual(image.read_file("HELLO"), hello_program())
        self.assertIsNone(image.find_entry("MISSING"))
        with self.assertRaises(ValueError):
            image.add_file("HELLO", b"dup")

    def test_directory_spills_into_a_second_sector(self):
        image = D64.blank("MANY", "MN")
        for index in range(9):
            image.add_file("FILE%d" % index, b"x")
        self.assertEqual(len(image.directory()), 9)
        first = image.read_sector(18, 1)
        self.assertEqual((first[0], first[1]), (18, 4))

    def test_save_load_and_compare(self):
        image = make_test_disk()
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "test.d64"
            image.save(path)
            loaded = D64.load(path)
        self.assertEqual(loaded.compare(image), [])
        loaded.write_sector(1, 0, bytes([1]) * 256)
        self.assertEqual(loaded.compare(image), [(1, 0)])


class ChipTests(unittest.TestCase):
    def test_via_timer1_one_shot_flags_after_count(self):
        via = VIA6522("T", lambda: 0xFF, lambda: 0xFF)
        via.write(4, 0x10, 0)
        via.write(5, 0x00, 100)
        self.assertEqual(via.read(4, 105) | (via.read(5, 105) << 8), 0x000B)
        via.run_events(110)
        self.assertFalse(via.ifr & 0x40)
        via.run_events(117)
        self.assertTrue(via.ifr & 0x40)
        self.assertFalse(via.irq)
        via.write(14, 0xC0, 117)
        self.assertTrue(via.irq)
        self.assertEqual(via.read(13, 117) & 0xC0, 0xC0)
        via.read(4, 118)
        self.assertFalse(via.irq)

    def test_via_timer1_free_run_period(self):
        via = VIA6522("T", lambda: 0xFF, lambda: 0xFF)
        via.write(11, 0x40, 0)
        via.write(4, 0x64, 0)
        via.write(5, 0x00, 0)
        first = via.t1_next
        via.run_events(first)
        self.assertTrue(via.ifr & 0x40)
        self.assertEqual(via.t1_next - first, 0x64 + 2)

    def test_via_ca1_edge_select_and_port_read_clears(self):
        via = VIA6522("T", lambda: 0xFF, lambda: 0xFF)
        via.write(12, 0x01, 0)
        via.set_ca1(0)
        self.assertFalse(via.ifr & 0x02)
        via.set_ca1(1)
        self.assertTrue(via.ifr & 0x02)
        via.read(1, 0)
        self.assertFalse(via.ifr & 0x02)
        via.write(12, 0x00, 0)
        via.set_ca1(0)
        self.assertTrue(via.ifr & 0x02)

    def test_cia_timer_b_one_shot_and_icr_read_clear(self):
        cia = CIA6526("C", lambda: 0xFF, lambda: 0xFF)
        cia.write(13, 0x7F, 0)
        cia.write(6, 0x00, 0)
        cia.write(7, 0x01, 0)
        cia.write(15, 0x19, 1000)
        self.assertEqual(cia.read(7, 1001) << 8 | cia.read(6, 1001), 0x00FF)
        cia.run_events(1200)
        self.assertFalse(cia.icr & 0x02)
        cia.run_events(1000 + 0x100 + 1)
        self.assertTrue(cia.icr & 0x02)
        self.assertFalse(cia.irq)
        self.assertEqual(cia.read(13, 1300), 0x02)
        self.assertEqual(cia.read(13, 1300), 0x00)
        self.assertEqual(cia.read(15, 1300) & 0x01, 0)

    def test_cia_timer_a_continuous_irq(self):
        seen = []
        cia = CIA6526("C", lambda: 0xFF, lambda: 0xFF, on_irq=seen.append)
        cia.write(13, 0x81, 0)
        cia.write(4, 0x25, 0)
        cia.write(5, 0x40, 0)
        cia.write(14, 0x01, 0)
        cia.run_events(0x4026)
        self.assertEqual(seen, [True])
        self.assertEqual(cia.read(13, 0x4026), 0x81)
        self.assertEqual(seen, [True, False])
        self.assertEqual(cia.ta.next, 0x4026 * 2)

    def test_cia_flag_negative_edge(self):
        cia = CIA6526("C", lambda: 0xFF, lambda: 0xFF)
        cia.set_flag(1)
        cia.set_flag(0)
        self.assertEqual(cia.icr, 0x10)
        cia.set_flag(1)
        self.assertEqual(cia.icr, 0x10)


class ParallelCableTests(unittest.TestCase):
    def test_cable_wiring(self):
        system = System(parallel_cable=True, extram=True)
        c64 = system.c64
        drive = system.drive
        c64.io_write(0xDD03, 0xFF)
        c64.io_write(0xDD01, 0x5A)
        drive.via1.write(3, 0x00, 0)
        self.assertEqual(drive.via1.read(1, 0), 0x5A)
        drive.via1.write(3, 0xFF, 0)
        drive.via1.write(1, 0x0F, 0)
        self.assertEqual(system.bus.par, 0x0A)
        c64.io_write(0xDD03, 0x00)
        self.assertEqual(c64.io_read(0xDD01), 0x0F)
        drive.via1.write(12, 0x0A, 0)
        self.assertEqual(drive.via1.ifr & 0x10, 0x10)
        drive.via1.read(0, 0)
        c64.io_read(0xDD01)
        self.assertEqual(drive.via1.ifr & 0x10, 0x10)
        c64.io_read(0xDD0D)
        drive.via1.read(1, 0)
        self.assertEqual(c64.io_read(0xDD0D) & 0x10, 0x10)

    def test_pa2_level_and_manual_ca2_modes(self):
        system = System(parallel_cable=True)
        c64 = system.c64
        drive = system.drive
        c64.io_write(0xDD02, 0x3B)
        c64.io_write(0xDD00, 0x07)
        c64.io_read(0xDD0D)
        # VIA1 PCR $0F: CA1 positive edge, CA2 manual high.
        drive.via1.write(12, 0x0F, 0)
        self.assertEqual(c64.io_read(0xDD00) & 0x04, 0x04)
        # PCR $0D: CA2 manual low; the falling edge also hits FLAG2.
        drive.via1.write(12, 0x0D, 0)
        self.assertEqual(c64.io_read(0xDD00) & 0x04, 0x00)
        self.assertEqual(c64.io_read(0xDD0D) & 0x10, 0x10)
        # ORA accesses do not pulse CA2 in the manual modes.
        drive.via1.read(1, 0)
        drive.via1.write(1, 0x00, 0)
        self.assertEqual(c64.io_read(0xDD00) & 0x04, 0x00)
        self.assertEqual(c64.io_read(0xDD0D) & 0x10, 0x00)
        drive.via1.write(12, 0x0F, 0)
        drive.via1.read(1, 0)
        self.assertEqual(c64.io_read(0xDD00) & 0x04, 0x04)
        self.assertEqual(c64.io_read(0xDD0D) & 0x10, 0x00)
        # Output direction: the pin reads the C64's own level, wired low
        # if the drive pulls it low.
        c64.io_write(0xDD02, 0x3F)
        c64.io_write(0xDD00, 0x07)
        self.assertEqual(c64.io_read(0xDD00) & 0x04, 0x04)
        c64.io_write(0xDD00, 0x03)
        self.assertEqual(c64.io_read(0xDD00) & 0x04, 0x00)
        c64.io_write(0xDD00, 0x07)
        drive.via1.write(12, 0x0D, 0)
        self.assertEqual(c64.io_read(0xDD00) & 0x04, 0x00)
        # Pulse mode still strobes: a low then high edge on FLAG2.
        drive.via1.write(12, 0x0B, 0)
        c64.io_read(0xDD0D)
        drive.via1.read(1, 0)
        self.assertEqual(c64.io_read(0xDD0D) & 0x10, 0x10)
        self.assertEqual(c64.io_read(0xDD00) & 0x04, 0x04)
        # Without the cable PA2 floats high whatever the drive does.
        plain = System()
        plain.c64.io_write(0xDD02, 0x3B)
        plain.drive.via1.write(12, 0x0D, 0)
        self.assertEqual(plain.c64.io_read(0xDD00) & 0x04, 0x04)

    def test_cia2_shift_register_holds_a_byte(self):
        system = System()
        system.c64.io_write(0xDD0C, 0xA5)
        self.assertEqual(system.c64.io_read(0xDD0C), 0xA5)
        self.assertEqual(system.c64.io_read(0xDD0E) & 0x40, 0)

    def test_bus_trace_shows_both_handshake_lines(self):
        system = System(parallel_cable=True)
        system.enable_bus_trace()
        system.c64.io_write(0xDD02, 0x3B)
        system.c64.io_read(0xDD01)
        system.drive.via1.write(12, 0x0D, 0)
        system.drive.via1.write(12, 0x0F, 0)
        names = [name for _, name, _ in system.bus.trace]
        self.assertEqual(names[-4:], ["PC2", "PC2", "FLAG2/PA2", "FLAG2/PA2"])
        self.assertIn("FLAG2/PA2 0", system.bus_trace())

    def test_extram_maps_ram_at_8000(self):
        system = System(extram=True)
        drive = system.drive
        drive.slow_write(0x8123, 0x77)
        self.assertEqual(drive.cpu.ByteAt(0x8123), 0x77)
        self.assertEqual(drive.cpu.ByteAt(0xA123), drive.rom[0x2123])
        self.assertEqual(len(drive.extram_bytes), 8192)
        self.assertEqual(system.drive_extram()[0x123], 0x77)
        plain = System()
        self.assertEqual(plain.drive_extram(), b"")
        plain.drive.slow_write(0x8123, 0x77)
        self.assertEqual(plain.drive.cpu.ByteAt(0x8123), plain.drive.rom[0x0123])

    def test_drive_memory_decoding(self):
        drive = System().drive
        drive.slow_write(0x2345, 0x12)
        self.assertEqual(drive.cpu.ByteAt(0x0345), 0x12)
        self.assertEqual(drive.cpu.ByteAt(0x6345), 0x12)
        self.assertEqual(drive.cpu.ByteAt(0x0C00), 0xFF)
        drive.slow_write(0x1802, 0x1A)
        self.assertEqual(drive.cpu.ByteAt(0x3802), 0x1A)
        self.assertEqual(drive.cpu.ByteAt(0xC000), drive.cpu.ByteAt(0x8000))


class StockRomAcceptanceTests(unittest.TestCase):
    """Stock KERNAL and 1541 DOS talking over the simulated IEC bus."""

    booted = None
    boot_seconds = 0.0

    @classmethod
    def setUpClass(cls):
        cls.image = make_test_disk()
        system = System(d64=cls.image)
        cls.boot_seconds = system.boot_to_ready(5.0)
        cls.boot_stats = system.stats()
        cls.booted = system

    def setUp(self):
        self.system = self.booted.snapshot()

    def run_line(self, line, budget=30.0):
        self.system.type_line(line)
        return self.system.run_until_ready(budget)

    def test_boot_to_ready(self):
        self.assertLess(self.boot_seconds, 3.0)
        screen = self.system.screen_text()
        self.assertTrue(any("READY." in line for line in screen))
        self.assertTrue(any("38911 BASIC BYTES FREE" in line for line in screen))
        self.assertEqual(self.system.c64.cpu.pc, 0xE5CD)
        self.assertEqual(self.system.peek_ram(0x02A6), 1)

    def test_performance(self):
        rate = self.boot_stats["c64_cycles_per_wall_second"]
        print("\n  boot: %.3f simulated s, %.2f wall s, %.0f C64 cycles per wall second" % (
            self.boot_seconds, self.boot_stats["wall_seconds"], rate))
        self.assertGreater(rate, 100000)

    def test_directory_listing(self):
        seconds = self.run_line('LOAD"$",8')
        self.run_line("LIST")
        screen = self.system.screen_text()
        self.assertTrue(any('"TESTDISK        " TD 2A' in line for line in screen))
        self.assertTrue(any('"HELLO"' in line and "PRG" in line for line in screen))
        self.assertTrue(any('"NOTES"' in line and "SEQ" in line for line in screen))
        self.assertTrue(any("655 BLOCKS FREE." in line for line in screen))
        self.assertFalse(any("ERROR" in line for line in screen))
        self.assertLess(seconds, 4.0)

    def test_load_prg(self):
        program = hello_program()
        seconds = self.run_line('LOAD"HELLO",8')
        print("\n  LOAD of %d bytes: %.3f simulated s" % (len(program), seconds))
        screen = [line.rstrip() for line in self.system.screen_text() if line.strip()]
        self.assertFalse(any("ERROR" in line for line in screen))
        self.assertEqual(screen[-3:], ["SEARCHING FOR HELLO", "LOADING", "READY."])
        end = 0x0801 + len(program) - 2
        self.assertEqual(self.system.peek_word(0xAE), end)
        self.assertEqual(self.system.peek_word(0x2D), end)
        self.assertEqual(self.system.ram_bytes(0x0801, end), program[2:])
        self.assertEqual(self.system.peek_ram(0x90), 0x40)
        self.assertEqual(self.system.drive.track, 17.0)

    def test_load_missing_file(self):
        self.run_line('LOAD"NOPE",8')
        screen = self.system.screen_text()
        self.assertTrue(any("?FILE NOT FOUND  ERROR" in line for line in screen))
        self.assertTrue(self.system.drive.led)

    def stage_disk_launcher(self, name, target_size=0):
        # Original one-line launcher, in the same PRG format checked by the
        # C builder tests. Mirror the HDL's RAM install/pointer repair here;
        # the ROMs and emulated drive perform the actual LOAD and restart.
        program = basic_program([(10, b'\x93"' + name.encode('ascii') + b'",8,1')])
        program += bytes(max(0, target_size - len(program)))
        end = 0x0801 + len(program) - 2
        for address, value in enumerate(program[2:], 0x0801):
            self.system.poke(address, value)
        for address, value in ((0x2B, 0x0801), (0xAC, 0),
                               (0x2D, end), (0x2F, end), (0x31, end), (0xAE, end)):
            self.system.poke(address, value & 0xFF)
            self.system.poke(address + 1, value >> 8)

    def test_disk_launcher_runs_exact_program_and_its_next_disk_load(self):
        image = D64.blank('LAUNCH TEST', 'LT')
        image.add_file('OTHER', basic_program([(10, b'\x97' + b'49152,99')]))
        image.add_file('STAGE ONE', basic_program([
            (10, b'\x97' + b'49152,\xc2(186)'),
            # LOAD initializes its default device before parsing arguments;
            # use the device saved by line 10, not a live PEEK of $BA here.
            (20, b'\x93"STAGE TWO",\xc2(49152),1'),
        ]))
        image.add_file('STAGE TWO', basic_program([(10, b'\x97' + b'49153,42')]))
        self.system.insert_disk(image)
        self.assertEqual(self.system.peek_ram(0xBA), 0)
        self.stage_disk_launcher('STAGE ONE', len(image.read_file('STAGE ONE')))
        self.run_line('RUN')
        self.assertEqual(self.system.peek_ram(0xC000), 8)
        self.assertEqual(self.system.peek_ram(0xC001), 42)
        self.assertFalse(self.system.screen_contains('ERROR'))
        self.assertEqual(self.system.drive.bytes_written, 0)

    def test_disk_launcher_reserves_variables_beyond_large_target(self):
        image = D64.blank('VARIABLE TEST', 'VT')
        program = basic_program([
            (10, b'A\xb21'),
            (20, b'\x99"VARIABLE TEST OK"'),
            (30, b'A\xb2A\xaa1'),
            (40, b'\x97' + b'49152,A'),
            (50, b'\x8f' + b'X' * 100),
        ])
        image.add_file('VARIABLES', program)
        self.system.insert_disk(image)
        self.stage_disk_launcher('VARIABLES', len(program))
        self.run_line('RUN')
        self.assertTrue(self.system.screen_contains('VARIABLE TEST OK'))
        self.assertFalse(self.system.screen_contains('ERROR'))
        self.assertEqual(self.system.peek_ram(0xC000), 2)
        end = 0x0801 + len(program) - 2
        self.assertEqual(self.system.peek_word(0x2D), end)
        self.assertEqual(self.system.ram_bytes(0x0801, end), program[2:])

    def test_disk_launcher_keeps_short_target_variables_above_launcher(self):
        image = D64.blank('SHORT TEST', 'ST')
        program = basic_program([(10, b'A\xb21')])
        name = 'SIXTEENCHARACTER'
        image.add_file(name, program)
        self.system.insert_disk(image)
        self.stage_disk_launcher(name, len(program))
        reserved_end = self.system.peek_word(0x2D)
        self.assertGreater(reserved_end, 0x0801 + len(program) - 2)
        self.run_line('RUN')
        self.assertFalse(self.system.screen_contains('ERROR'))
        self.assertEqual(self.system.peek_word(0x2D), reserved_end)
        self.assertGreater(self.system.peek_word(0x2F), reserved_end)
        self.assertEqual(self.system.ram_bytes(0x0801, 0x0801 + len(program) - 2),
                         program[2:])

    def test_disk_launcher_load_failure_returns_basic_without_retrying(self):
        self.stage_disk_launcher('MISSING')
        self.run_line('RUN')
        self.assertTrue(self.system.screen_contains('FILE NOT FOUND'))
        self.assertTrue(self.system.screen_contains('ERROR IN 10'))
        self.assertEqual(self.system.peek_ram(0xC000), 0)
        self.assertEqual(self.system.drive.bytes_written, 0)
        screen = self.system.screen_text()
        self.system.run_for(0.5)
        self.assertEqual(self.system.screen_text(), screen)

    def test_save_program(self):
        self.run_line('10 PRINT"HELLO WORLD"')
        self.run_line("20 GOTO 10")
        seconds = self.run_line('SAVE"NEW",8')
        screen = self.system.screen_text()
        self.assertTrue(any("SAVING NEW" in line for line in screen))
        self.assertFalse(any("ERROR" in line for line in screen))
        image = self.system.disk_image()
        self.assertEqual(image.decode_errors, [])
        entry = image.find_entry("NEW")
        self.assertIsNotNone(entry)
        self.assertEqual((entry.ftype, entry.blocks, entry.closed), ("PRG", 1, True))
        expected = b"\x01\x08" + self.system.ram_bytes(0x0801, self.system.peek_word(0x2D))
        self.assertEqual(image.read_file("NEW"), expected)
        self.assertEqual(image.read_file("HELLO"), hello_program())
        self.assertEqual(image.blocks_free(), 654)
        self.assertGreater(self.system.drive.bytes_written, 0)
        self.assertLess(seconds, 6.0)

    def test_run_stop_breaks_a_program(self):
        self.run_line("10 GOTO 10")
        self.system.type_line("RUN")
        self.system.run_for(0.3)
        self.system.press_stop()
        self.system.run_for(0.2)
        self.system.release_keys()
        self.system.run_until_ready(2.0)
        self.assertTrue(any("BREAK IN 10" in line for line in self.system.screen_text()))

    def test_bus_trace_records_atn(self):
        self.system.enable_bus_trace()
        self.run_line('LOAD"NOPE",8')
        names = {name for _, name, _ in self.system.bus.trace}
        self.assertEqual(names, {"ATN", "CLK", "DATA"})
        self.assertIn("ATN", self.system.bus_trace())
        self.assertEqual(len(self.system.bus_trace(5).splitlines()), 5)

    def test_instruction_trace_and_breakpoint(self):
        self.system.enable_instruction_trace("drive", 16)
        hits = []
        self.system.add_breakpoint("c64", 0xED0C, lambda system: hits.append(system.c64.cpu.a) or True)
        self.system.type_line('LOAD"NOPE",8')
        self.assertEqual(self.system.run_for(5.0), "breakpoint")
        self.assertEqual(hits, [8])
        self.assertIn("A=", self.system.format_instruction_trace("drive", 4))

    def test_run_until_pc_and_run_until(self):
        self.system.type_line('LOAD"NOPE",8')
        seconds = self.system.run_until_pc("c64", 0xED0C, 2.0)
        self.assertEqual(self.system.c64.cpu.pc, 0xED0C)
        self.assertGreater(seconds, 0.0)
        self.assertNotIn(0xED0C, self.system.c64.breakpoints)
        self.system.run_until_pc("drive", 0xE85B, 2.0)
        self.assertEqual(self.system.drive.cpu.pc, 0xE85B)
        self.system.run_until(lambda system: system.screen_contains("FILE NOT FOUND"), 10.0)
        with self.assertRaises(SimTimeout):
            self.system.run_until(lambda system: False, 0.01)

    def test_rom_bytes_overrides(self):
        basic = bytearray((ROMS / "basic.901226-01.bin").read_bytes())
        basic[0x378:0x37E] = b"READX."
        drive_rom = bytearray((ROMS / "1541-II.251968-03.bin").read_bytes())
        # The DOS checksums its ROM at reset, so the patch keeps the byte
        # sum of the page: "FRDF." sums like "FREE.".
        drive_rom[0x817:0x823] = b"BLOCKS FRDF."
        kernal = (ROMS / "kernal.901227-03.bin").read_bytes()
        system = System(d64=self.image, basic_bytes=bytes(basic), kernal_bytes=kernal,
                        drive_rom_bytes=bytes(drive_rom))
        system.boot_to_ready(5.0)
        self.assertTrue(system.screen_contains("READX."))
        system.type_line('LOAD"$",8')
        system.type_line("LIST")
        system.run_until_ready(30.0)
        self.assertTrue(system.screen_contains("655 BLOCKS FRDF."))

    def test_no_disk_reports_file_not_found(self):
        system = System()
        system.boot_to_ready(5.0)
        system.type_line('LOAD"$",8')
        system.run_until_ready(30.0)
        self.assertTrue(any("?FILE NOT FOUND  ERROR" in line for line in system.screen_text()))

    @unittest.skipUnless(SLOW, "set MULTITINI_SLOW_TESTS=1 to run the 202 block LOAD")
    def test_slow_big_load(self):
        image = D64.blank("BIGDISK", "BG")
        data = bytearray((i * 7 + 3) & 0xFF for i in range(202 * 254 - 2))
        data[0x400:0x404] = b"\x00" * 4
        _, _, blocks = image.add_file("BIG", b"\x00\x04" + bytes(data))
        self.assertEqual(blocks, 202)
        system = System(d64=image)
        system.boot_to_ready(5.0)
        system.type_line('LOAD"BIG",8,1')
        seconds = system.run_until_ready(400.0)
        stats = system.stats()
        print("\n  202 block LOAD: %.3f simulated s, %.1f wall s, %.0f C64 cycles per wall second" % (
            seconds, stats["wall_seconds"], stats["c64_cycles_per_wall_second"]))
        end = 0x0400 + len(data)
        self.assertEqual(system.peek_word(0xAE), end)
        self.assertEqual(system.peek_ram(0x90), 0x40)
        self.assertEqual(system.ram_bytes(0x0800, end), bytes(data[0x400:]))


class CliTests(unittest.TestCase):
    def test_cli_lists_directory(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "cli.d64"
            make_test_disk().save(path)
            out = Path(tmp) / "out.d64"
            status = cli_main(["--d64", str(path), "--type", 'LOAD"$",8', "--type", "LIST",
                               "--quiet", "--save-d64", str(out)])
            self.assertEqual(status, 0)
            self.assertEqual(D64.load(out).compare(make_test_disk()), [])


if __name__ == "__main__":
    unittest.main()
