"""Host checks for original code/presets/OFS output; stdlib except optional disassembly.
SPDX-License-Identifier: GPL-3.0-only
"""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from adf import data_disk, name_hash
from helper import build, FLAGS, job, MODES, prefs, screenmode
from m68k import Assembler, hunk


def chunks(data):
    assert data[:4] == b"FORM" and struct.unpack_from(">I", data, 4)[0] == len(data) - 8
    assert data[8:12] == b"PREF"
    result = {}
    at = 12
    while at < len(data):
        name, size = data[at:at + 4], struct.unpack_from(">I", data, at + 4)[0]
        result[name] = data[at + 8:at + 8 + size]
        at += 8 + size + (size & 1)
    assert at == len(data)
    return result


def read_ofs(image):
    """Independent chain/checksum/bitmap reader for the produced data disk."""
    assert len(image) == 901120 and image[:4] == b"DOS\0"
    used, files = {0, 1, 880, 881}, {}
    def block(n):
        data = image[n * 512:(n + 1) * 512]
        words = struct.unpack(">128I", data)
        assert sum(words) & 0xffffffff == 0
        return data, words
    def directory(n, prefix):
        _, words = block(n)
        assert words[0] == 2
        for head in words[6:78]:
            while head:
                assert head not in used
                used.add(head)
                raw, entry = block(head)
                name = raw[433:433 + raw[432]].decode("ascii")
                path = prefix + name
                assert entry[1] == head and entry[125] == n
                if entry[127] == 2:
                    directory(head, path + "/")
                else:
                    assert entry[127] == 0xfffffffd
                    payload, cursor, sequence = bytearray(), entry[4], 1
                    while cursor:
                        assert cursor not in used
                        used.add(cursor)
                        data, part = block(cursor)
                        assert part[0] == 8 and part[1] == head and part[2] == sequence
                        assert 0 < part[3] <= 488
                        assert entry[78 - sequence] == cursor
                        payload.extend(data[24:24 + part[3]])
                        cursor, sequence = part[4], sequence + 1
                    assert sequence - 1 == entry[2] and len(payload) == entry[81]
                    files[path] = bytes(payload)
                head = entry[124]
    root, words = block(880)
    assert words[3] == 72 and words[78:80] == (0xffffffff, 881) and words[127] == 1
    directory(880, "")
    bitmap = block(881)[1]
    for n in range(2, 1760):
        word, bit = divmod(n - 2, 32)
        assert bool(bitmap[word + 1] & 1 << bit) == (n not in used)
    return files


class AssemblerTests(unittest.TestCase):
    def test_known_motorola_encodings(self):
        a = Assembler()
        a.ins("move.l", "4.w", "a6")
        a.ins("jsr", "-552(a6)")
        a.ins("moveq", "#-1", "d0")
        a.ins("rts")
        self.assertEqual(a.finish().hex(), "2c7800044eae fdd8 70ff4e75".replace(" ", ""))

    def test_two_memory_extensions_keep_operand_order(self):
        a = Assembler(); a.ins("move.w", "0x1000.l", "0x2000.l")
        self.assertEqual(a.finish().hex(), "33f90000100000002000")

    def test_pc_relative_base_is_extension_word(self):
        a = Assembler(); a.ins("bsr", "later"); a.ins("moveq", "#0", "d0")
        a.label("later"); a.ins("rts")
        self.assertEqual(a.finish().hex(), "6100000470004e75")

    def test_rejects_unresolved_and_overflowing_branches(self):
        a = Assembler(); a.ins("bra", "missing")
        with self.assertRaises(ValueError): a.finish()
        a = Assembler(); a.ins("bra", "far"); a.data(bytes(33000)); a.label("far")
        with self.assertRaises(ValueError): a.finish()

    def test_register_save_restore_and_invalid_quick(self):
        a = Assembler(); a.ins("save"); a.ins("restore")
        self.assertEqual(a.finish().hex(), "48e73f3e4cdf7cfc")
        with self.assertRaises(ValueError): Assembler().ins("addq.w", "#9", "d0")
        with self.assertRaises(ValueError): Assembler().ins("moveq", "#128", "d0")

    def test_signed_word_shift_and_range_branch_encodings(self):
        a = Assembler(); a.ins("asr.w", "#1", "d0"); a.ins("asr.w", "#8", "d7")
        self.assertEqual(a.finish().hex(), "e240e047")
        for op, count, reg in (("asr.b", "#1", "d0"), ("asr.l", "#1", "d0"),
                               ("asr.w", "#0", "d0"), ("asr.w", "#9", "d0"),
                               ("asr.w", "d1", "d0"), ("asr.w", "#1", "a0"),
                               ("asr.w", "#1", "d8")):
            with self.subTest(op=op, count=count, reg=reg), self.assertRaises(ValueError):
                Assembler().ins(op, count, reg)
        a = Assembler()
        for n, op in enumerate(("bvs", "blt", "bgt")):
            a.ins(op, f"next{n}"); a.label(f"next{n}")
        self.assertEqual(a.finish().hex(), "690000026d0000026e000002")

    def test_hunk_is_single_code_without_relocation(self):
        r = build(); data = r["hunk"]
        header = struct.unpack_from(">8I", data)
        self.assertEqual(header[:5], (0x3f3, 0, 1, 0, 0))
        self.assertEqual(header[6], 0x3e9)
        self.assertEqual(header[5], header[7])
        self.assertEqual(len(data), 36 + header[5] * 4)
        self.assertEqual(data[32:32 + len(r["code"])], r["code"])
        self.assertEqual(data[-4:], bytes.fromhex("000003f2"))

    def test_all_instructions_disassemble_as_original_68000(self):
        try:
            from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
        except ImportError:
            self.skipTest("Optional Capstone unavailable; native CPU tests remain independent")
        r = build(); instructions = list(Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000).disasm(
            r["code"][:r["instruction_end"]], 0))
        self.assertEqual(sum(i.size for i in instructions), r["instruction_end"])
        self.assertGreater(len(instructions), 300)
        self.assertFalse(any(i.mnemonic == "dc.w" for i in instructions))


class PreferencesTests(unittest.TestCase):
    def test_four_actual_screen_modes_without_autoscroll(self):
        for width, height, mode, depth in MODES:
            payload = chunks(screenmode(width, height, mode, depth))
            self.assertEqual(payload[b"PRHD"], bytes(6))
            self.assertEqual(payload[b"SCRM"][:16], bytes(16))
            self.assertEqual(struct.unpack(">IHHHH", payload[b"SCRM"][16:]),
                             (mode, width, height, depth, 0))
        self.assertEqual(MODES, ((640, 256, 0x29000, 4), (640, 512, 0x29004, 4),
                                (724, 566, 0x29004, 4), (1280, 512, 0x29024, 2)))

    def test_old_presets_and_mailbox_flags_remain_compatible(self):
        for width, height, mode, depth in MODES[:3]:
            self.assertEqual(screenmode(width, height, mode, depth),
                             prefs(b"SCRM", bytes(16) + struct.pack(">IHHHH", mode, width, height, 4, 0)))
        self.assertEqual(FLAGS, dict(alive=1, screen=2, maximum=4, monitor=8,
                                    busy=16, viewport_match=32, superhires=64))

    def test_overscan_template_contains_no_guessed_origin(self):
        self.assertEqual(chunks(prefs(b"OSCN", bytes(36)))[b"OSCN"], bytes(36))

    def test_jobs_save_and_use_are_separate_and_report_after_return(self):
        for mode in range(len(MODES)):
            for save in (False, True):
                text = job(mode, save).decode()
                self.assertIn(" SAVE" if save else " USE", text)
                self.assertNotIn(" USE" if save else " SAVE", text)
                self.assertLess(text.index("SYS:Prefs/ScreenMode"), text.rindex('Echo "0"'))
                self.assertIn('Echo "3"', text)
                self.assertLess(text.index("SYS:Prefs/Overscan"), text.index("SYS:Prefs/ScreenMode"))



class DiskTests(unittest.TestCase):
    def test_ofs_multiblock_files_empty_files_nested_names(self):
        source = {"MultitiniScreen": build()["hunk"], "Presets/2.prefs": screenmode(*MODES[2]),
                  "Zero": b"", "Source/large": bytes(range(256)) * 70}
        disk = data_disk(source)
        self.assertEqual(read_ofs(disk), source)
        self.assertEqual(disk[4:1024], bytes(1020))  # Data disk has no OS/boot code.

    def test_ofs_hash_collisions_preserve_both_files(self):
        names = {}
        for n in range(1000):
            name = "item" + str(n); slot = name_hash(name)
            if slot in names:
                first = names[slot]; break
            names[slot] = name
        files = {first: b"first", name: b"second"}
        self.assertEqual(read_ofs(data_disk(files)), files)

    def test_rejects_bad_names_case_duplicates_and_large_files(self):
        for files in ({"../bad": b""}, {"a" * 31: b""}, {"Foo": b"1", "foo": b"2"},
                      {"large": bytes(488 * 72 + 1)}):
            with self.assertRaises(ValueError): data_disk(files)

    def test_disk_is_reproducible_and_corruption_is_detected(self):
        files = {"one": b"test", "nested/two": b"another"}
        a = data_disk(files)
        self.assertEqual(a, data_disk(dict(reversed(list(files.items())))))
        broken = bytearray(a); broken[880 * 512 + 7] ^= 1
        with self.assertRaises(AssertionError): read_ofs(broken)


if __name__ == "__main__":
    unittest.main()
