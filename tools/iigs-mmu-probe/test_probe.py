# SPDX-License-Identifier: GPL-3.0-only
"""Transport, file-format, and deliberately damaged screenshot checks."""
from __future__ import annotations
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib
from protocol import *
from build import omf, prodos_volume
from decode import image_rows, preview_packet, homography, write_outputs

def fixture():
    observations = [[((case >> (profile % 7)) ^ profile) & ((1 << p["planes"])-1)
                     for case in range(p["cases"])] for profile,p in enumerate(profiles())]
    indices, dictionary = pack_profiles(observations)
    extras = bytes(544)
    data = bytearray(HEADER) + indices + dictionary + extras
    data[:5] = b"MMU1\x01"
    data[6:8] = bytes([3,6])
    struct.pack_into("<HHHH", data, 8, len(data)+4, len(indices), len(dictionary)//16, len(extras))
    data.extend(struct.pack("<I", zlib.crc32(data)))
    return bytes(data), observations

class ProbeTests(unittest.TestCase):
    def test_lossless_dictionary_and_crc(self):
        packet, observations = fixture()
        decoded = decode_packet(packet)
        self.assertEqual([p["observations"] for p in decoded["profiles"]], observations)
        self.assertLessEqual(len(packet), CAPACITY)

    def test_single_bit_photo_repair_and_double_bit_rejection(self):
        packet, _ = fixture()
        rows = list(screen_rows(packet))
        damaged = bytearray(rows[15]); damaged[9] ^= 0x10; rows[15] = damaged
        decoded_packet, decoded = packet_from_rows(rows)
        self.assertEqual(packet, decoded_packet)
        self.assertEqual(len(decoded["image_corrections"]), 1)
        damaged[17] ^= 0x20
        with self.assertRaises(ValueError):
            packet_from_rows(rows)

    def test_packet_damage_is_not_accepted(self):
        packet, _ = fixture()
        damaged = bytearray(packet); damaged[HEADER+10] ^= 1
        with self.assertRaisesRegex(ValueError, "CRC32"):
            decode_packet(damaged)

    def test_padded_emulator_dump_writes_a_canonical_packet(self):
        packet, _ = fixture()
        padded = packet.ljust(CAPACITY,b"\0")
        with tempfile.TemporaryDirectory() as td:
            output = Path(td)
            write_outputs(padded,decode_packet(padded),output)
            self.assertEqual((output/"measurement.bin").read_bytes(),packet)

    def test_image_roundtrip_scaled_and_inset(self):
        from PIL import Image
        packet, _ = fixture()
        with tempfile.TemporaryDirectory() as td:
            path = Path(td)/"preview.png"
            preview_packet(packet, path)
            image = Image.open(path)
            packet2, _ = packet_from_rows(image_rows(image))
            self.assertEqual(packet2, packet)
            canvas = Image.new("RGB", (1500, 1000), "#444444")
            canvas.paste(image, (40, 70))
            packet2, _ = packet_from_rows(image_rows(canvas, crop=(40,70,1280,800)))
            self.assertEqual(packet2, packet)

    def test_omf_fields_and_independent_load(self):
        payload = bytes(range(256))*256
        data = omf(payload)
        self.assertEqual(struct.unpack_from("<I", data)[0], len(data))
        self.assertEqual(struct.unpack_from("<I", data, 8)[0], 65536)
        self.assertEqual(data[14:16], b"\x04\x02")
        self.assertEqual(struct.unpack_from("<H", data, 20)[0], 0x5000)
        self.assertEqual(struct.unpack_from("<I", data, 28)[0], 65536)
        self.assertEqual(struct.unpack_from("<I", data, 24)[0], 0)
        position = struct.unpack_from("<H", data, 42)[0]
        memory = bytearray()
        while opcode := data[position]:
            position += 1
            if opcode < 0xe0:
                count = opcode
            elif opcode == 0xf2:
                count = struct.unpack_from("<I", data, position)[0]
                position += 4
            else:
                self.fail(f"unexpected record {opcode:02x}")
            memory.extend(data[position:position+count]); position += count
        self.assertEqual(memory, payload)

    def test_perspective_photo_recovers_the_packet(self):
        from PIL import Image
        packet, _ = fixture()
        with tempfile.TemporaryDirectory() as td:
            path = Path(td)/"preview.png"
            preview_packet(packet,path)
            image = Image.open(path)
            rectangle = ((0,0),(1280,0),(1280,800),(0,800))
            corners = ((120,100),(1430,60),(1360,940),(65,840))
            photo = image.transform((1500,1000),Image.Transform.PERSPECTIVE,
                                    homography(corners,rectangle),Image.Resampling.BILINEAR)
            recovered, _ = packet_from_rows(image_rows(photo,corners=[c for p in corners for c in p]))
            self.assertEqual(recovered,packet)

    def test_prodos_bitmap_directory_and_sapling(self):
        payload = omf(bytes(65536))
        volume = prodos_volume((("MMUPROBE",0xb3,payload),))
        root = volume[1028:1067]
        self.assertEqual(root[30:33], b"\xc3\x27\x0d")
        self.assertEqual(struct.unpack_from("<HHH", root, 33), (1,6,1600))
        entry = volume[1067:1106]
        self.assertEqual(entry[0],0x28)
        self.assertEqual(entry[16],0xb3)
        index,used = struct.unpack_from("<HH", entry,17)
        length = int.from_bytes(entry[21:24], "little")
        recovered = bytearray()
        for i in range(used-1):
            block = volume[index*512+i] | volume[index*512+256+i] << 8
            self.assertFalse(volume[6*512+block//8] & (0x80 >> (block%8)))
            recovered.extend(volume[block*512:(block+1)*512])
        self.assertEqual(recovered[:length], payload)

if __name__ == "__main__":
    unittest.main()
