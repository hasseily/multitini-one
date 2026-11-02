# SPDX-License-Identifier: GPL-3.0-only
"""Versioned observation schedule and lossless screenshot transport. No MMU oracle."""
from __future__ import annotations
import binascii
import struct
import zlib

VERSION = 1
CAPACITY = 34 * 96
HEADER = 64
ADDRESSES = (0x03ff, 0x0400, 0x07ff, 0x0800, 0x0bff, 0x0c00,
             0x1fff, 0x2000, 0x3fff, 0x4000, 0x5fff, 0x6000, 0x9fff, 0xa000)
SOURCES = ((0, 0), (1, 0), (2, 0), (3, 0), (14, 0), (15, 0),
           (126, 0), (127, 0), (0, 1), (2, 1))
AUX_ADDRESSES = (0x0100, 0x01ff, 0x0200, 0x03ff, 0x0400, 0x07ff, 0x2000, 0x4000)
ALIASES = (0, 1, 0xe0, 0xe1)
REGISTERS = (0xc029, 0xc035, 0xc036, 0xc068, 0xc02d)
SECTIONS = (("shadow", 280, 4, 128), ("aux", 32, 6, 128),
            ("lc_pairs", 128, 3, 32), ("lc_windows", 16, 6, 32),
            ("registers", 10, 8, 128))
INDEX_COUNT = sum(profiles * planes for _, profiles, planes, _ in SECTIONS)

def profiles():
    for bank, ramwrt in SOURCES:
        for address in ADDRESSES:
            for all_banks in (0, 1):
                yield dict(section="shadow", bank=bank, ramwrt=ramwrt, address=address,
                           all_banks=all_banks, planes=4, cases=128)
    for bank in ALIASES[:1] + (2,) + ALIASES[2:]:
        for address in AUX_ADDRESSES:
            yield dict(section="aux", bank=bank, address=address, planes=6, cases=128)
    for alias in ALIASES:
        for first in range(32):
            yield dict(section="lc_pairs", alias=alias, first=first, planes=3, cases=32)
    for bank in ALIASES:
        for altzp in (0, 1):
            for inhibited in (0, 1):
                yield dict(section="lc_windows", bank=bank, altzp=altzp,
                           inhibited=inhibited, planes=6, cases=32)
    for register in REGISTERS:
        for half in (0, 1):
            yield dict(section="registers", register=register, half=half, planes=8, cases=128)

def pack_profiles(observations):
    """Host counterpart of the 65816 dictionary encoder, used for transport tests."""
    dictionary, indices = [], bytearray()
    for profile, values in zip(profiles(), observations, strict=True):
        if len(values) != profile["cases"]:
            raise ValueError("wrong case count")
        for plane in range(profile["planes"]):
            bitmap = bytes(sum(((values[j * 8 + k] >> plane) & 1) << k
                               for k in range(8) if j * 8 + k < len(values)) for j in range(16))
            if bitmap not in dictionary:
                dictionary.append(bitmap)
            indices.append(dictionary.index(bitmap))
    if len(dictionary) > 128:
        raise ValueError("dictionary overflow")
    return bytes(indices), b"".join(dictionary)

def decode_packet(data):
    if len(data) < HEADER or data[:4] != b"MMU1" or data[4] != VERSION:
        raise ValueError("not an MMU1 version-1 packet")
    length, index_count, dictionaries, extra_length = struct.unpack_from("<HHHH", data, 8)
    if length > CAPACITY or length < HEADER + 4 or len(data) < length:
        raise ValueError("invalid packet length")
    data = data[:length]
    if zlib.crc32(data[:-4]) != struct.unpack_from("<I", data, length - 4)[0]:
        raise ValueError("packet CRC32 mismatch")
    if index_count != INDEX_COUNT or HEADER + index_count + 16 * dictionaries + extra_length + 4 != length:
        raise ValueError("inconsistent packet sections")
    dictionary = data[HEADER + index_count:HEADER + index_count + dictionaries * 16]
    cursor, decoded = HEADER, []
    for profile in profiles():
        values = [0] * profile["cases"]
        for plane in range(profile["planes"]):
            index = data[cursor]
            cursor += 1
            if index >= dictionaries:
                raise ValueError("invalid dictionary index")
            bitmap = dictionary[index * 16:(index + 1) * 16]
            for case in range(len(values)):
                values[case] |= ((bitmap[case >> 3] >> (case & 7)) & 1) << plane
        decoded.append(dict(profile, observations=values))
    extras = data[HEADER + index_count + dictionaries * 16:-4]
    flags = data[5]
    return dict(format="MMU1", version=data[4], flags=flags, rom_revision=data[6],
                launch_bank=data[7], length=length, dictionary_count=dictionaries,
                scheduled_cases=struct.unpack_from("<I", data, 16)[0],
                measured_cases=struct.unpack_from("<I", data, 16)[0]-struct.unpack_from("<H", data, 48)[0],
                vbl_edges=struct.unpack_from("<H", data, 20)[0],
                anomalies=struct.unpack_from("<H", data, 22)[0],
                entry_registers=dict(zip(("C029", "C035", "C036", "C068", "C02D"), data[24:29])),
                present_banks=[bank for bank in range(128) if data[32+bank//8] & (1 << (bank % 8))],
                skipped_shadow_cases=struct.unpack_from("<H", data, 48)[0],
                supplemental=dict(rom_samples=list(extras[:16]),
                                  absent_samples=[list(extras[16+i*8:24+i*8]) for i in range(4)],
                                  lc_flat=[list(extras[48+i*4:52+i*4]) for i in range(4)],
                                  extended_io=[list(extras[64+i*2:66+i*2]) for i in range(16)],
                                  text2_cya6=[list(extras[96+i*2:98+i*2]) for i in range(16)],
                                  c800=[list(extras[128+i*5:133+i*5]) for i in range(64)],
                                  anomaly_records=[list(extras[448+i*6:454+i*6]) for i in range(min(16,struct.unpack_from("<H", data, 22)[0]))]),
                profiles=decoded, extras_hex=extras.hex(), crc32=f"{zlib.crc32(data[:-4]):08x}",
                complete=(flags == 0))

def screen_rows(packet):
    if len(packet) > CAPACITY:
        raise ValueError("packet does not fit one screen")
    padded = packet.ljust(CAPACITY, b"\0")
    for row in range(96):
        body = bytes([row]) + padded[row * 34:(row + 1) * 34]
        yield b"\xa5\x5a" + body + struct.pack("<H", binascii.crc_hqx(body, 0xffff)) + b"\xa5"

def packet_from_rows(rows, repair=True):
    output, corrections = bytearray(), []
    if len(rows) != 96:
        raise ValueError("expected 96 rows")
    for number, source in enumerate(rows):
        row = bytearray(source)
        if len(row) != 40 or row[:2] != b"\xa5\x5a" or row[-1] != 0xa5:
            raise ValueError(f"row {number}: border not found; check crop/orientation")
        def valid(candidate):
            return candidate[0] == number and binascii.crc_hqx(candidate[:35], 0xffff) == int.from_bytes(candidate[35:37], "little")
        body = row[2:39]
        if not valid(body):
            candidates = []
            if repair:
                for bit in range(296):
                    candidate = bytearray(body)
                    candidate[bit >> 3] ^= 1 << (bit & 7)
                    if valid(candidate):
                        candidates.append((bit, candidate))
            if len(candidates) != 1:
                raise ValueError(f"row {number}: CRC16 failure; take a sharper image")
            bit, body = candidates[0]
            corrections.append(dict(row=number, bit=bit))
        output.extend(body[1:35])
    result = decode_packet(output)
    result["image_corrections"] = corrections
    return bytes(output[:result["length"]]), result
