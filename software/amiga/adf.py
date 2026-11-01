"""Deterministic, original OFS data-floppy writer for the small helper package.

No boot code or OS files are included. It supports short ASCII names and files
up to 72 OFS data blocks; other uses fail explicitly.
SPDX-License-Identifier: GPL-3.0-only
"""
from pathlib import PurePosixPath
import struct

BLOCKS, BYTES, ROOT, BITMAP = 1760, 512, 880, 881


def name_hash(name):
    value = len(name)
    for char in name.upper():
        value = (value * 13 + ord(char)) & 0x7ff
    return value % 72


def data_disk(files, volume="MultitiniScreen"):
    image = bytearray(BLOCKS * BYTES)
    allocated = {0, 1, ROOT, BITMAP}
    entries, parents, blocks = {}, {"": ROOT}, {}
    folded = set()

    def allocate():
        for n in range(2, BLOCKS):
            if n not in allocated:
                allocated.add(n)
                return n
        raise ValueError("Helper data floppy full")

    def valid_name(name):
        if not 1 <= len(name) <= 30 or any(ord(c) < 32 or ord(c) > 126 or c in "/:" for c in name):
            raise ValueError("Unsupported OFS name: " + name)

    def word(block, index, value):
        struct.pack_into(">I", blocks[block], index * 4, value & 0xffffffff)

    def bstr(block, name):
        valid_name(name)
        value = name.encode("ascii")
        blocks[block][432:433 + len(value)] = bytes([len(value)]) + value

    def checksum(block, index=5):
        word(block, index, 0)
        word(block, index, -sum(struct.unpack(">128I", blocks[block])))

    for path, data in sorted(files.items()):
        p = PurePosixPath(path)
        if path.upper() in folded:
            raise ValueError("Case-insensitive duplicate helper path")
        folded.add(path.upper())
        if p.is_absolute() or ".." in p.parts or "\\" in path:
            raise ValueError("Unsafe helper path")
        for part in p.parts:
            valid_name(part)
        parent = ""
        for part in p.parts[:-1]:
            directory = (parent + "/" + part).lstrip("/")
            if directory not in parents:
                parents[directory] = allocate()
                entries[directory] = (parent, part, None, parents[directory])
            parent = directory
        if path in entries:
            raise ValueError("Duplicate helper entry")
        entries[path] = (parent, p.name, bytes(data), allocate())

    blocks[ROOT] = bytearray(BYTES)
    word(ROOT, 0, 2)
    word(ROOT, 3, 72)
    word(ROOT, 78, -1)
    word(ROOT, 79, BITMAP)
    word(ROOT, 127, 1)
    bstr(ROOT, volume)
    for path, (parent, name, data, header) in sorted(entries.items()):
        blocks[header] = bytearray(BYTES)
        word(header, 0, 2)
        word(header, 1, header)
        word(header, 125, parents[parent])
        bstr(header, name)
        if data is None:
            word(header, 127, 2)
            continue
        chunks = [data[n:n + 488] for n in range(0, len(data), 488)]
        if len(chunks) > 72:
            raise ValueError("Helper file exceeds single OFS header")
        numbers = [allocate() for _ in chunks]
        word(header, 2, len(numbers))
        word(header, 4, numbers[0] if numbers else 0)
        word(header, 81, len(data))
        word(header, 127, -3)
        if name == "Install" or name.startswith(("apply", "save")):
            word(header, 80, 64)  # S bit; RWED remain allowed.
        for index, (number, payload) in enumerate(zip(numbers, chunks)):
            word(header, 77 - index, number)
            blocks[number] = bytearray(BYTES)
            word(number, 0, 8)
            word(number, 1, header)
            word(number, 2, index + 1)
            word(number, 3, len(payload))
            word(number, 4, numbers[index + 1] if index + 1 < len(numbers) else 0)
            blocks[number][24:24 + len(payload)] = payload
    # Link each name into its parent's hash table, including collisions.
    for path, (parent, name, data, header) in sorted(entries.items()):
        parent_number = parents[parent]
        slot = 6 + name_hash(name)
        previous = struct.unpack_from(">I", blocks[parent_number], slot * 4)[0]
        word(header, 124, previous)
        word(parent_number, slot, header)
    blocks[BITMAP] = bytearray(BYTES)
    for number in range(2, BLOCKS):
        if number not in allocated:
            index, bit = divmod(number - 2, 32)
            value = struct.unpack_from(">I", blocks[BITMAP], (index + 1) * 4)[0]
            word(BITMAP, index + 1, value | 1 << bit)
    for number in blocks:
        checksum(number, 0 if number == BITMAP else 5)
        image[number * BYTES:(number + 1) * BYTES] = blocks[number]
    image[:4] = b"DOS\0"
    # An uninstalled data disk deliberately has no valid boot checksum/code.
    return bytes(image)
