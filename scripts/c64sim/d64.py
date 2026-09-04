# SPDX-License-Identifier: GPL-3.0-only
"""D64 disk images with 1541 DOS style allocation."""

from __future__ import annotations

from .gcr import sectors_per_track

FILE_TYPES = {"DEL": 0x80, "SEQ": 0x81, "PRG": 0x82, "USR": 0x83, "REL": 0x84}
TYPE_NAMES = {v & 0x07: k for k, v in FILE_TYPES.items()}
DIRECTORY_TRACK = 18
INTERLEAVE = 10
DIRECTORY_INTERLEAVE = 3


def petscii_name(name: str) -> bytes:
    raw = name.upper().encode("ascii")
    if len(raw) > 16:
        raise ValueError("file name longer than 16 characters")
    return raw + b"\xa0" * (16 - len(raw))


def name_from_petscii(raw: bytes) -> str:
    return bytes(b for b in raw if b != 0xA0).decode("ascii", "replace")


class DirEntry:
    __slots__ = ("name", "ftype", "track", "sector", "blocks", "closed")

    def __init__(self, name, ftype, track, sector, blocks, closed):
        self.name = name
        self.ftype = ftype
        self.track = track
        self.sector = sector
        self.blocks = blocks
        self.closed = closed

    def __repr__(self):
        return "DirEntry(%r, %s, %d/%d, %d blocks)" % (
            self.name, self.ftype, self.track, self.sector, self.blocks)


class D64:
    """A 35 or 40 track D64 image kept as a bytearray."""

    def __init__(self, data=None, track_count: int = 35):
        if data is not None:
            data = bytearray(data)
            track_count = 35 if len(data) < D64.image_size(36) else 40
            if len(data) < D64.image_size(track_count):
                raise ValueError("unsupported D64 size %d" % len(data))
        else:
            data = bytearray(D64.image_size(track_count))
        self.data = data
        self.track_count = track_count
        self.decode_errors = []

    @staticmethod
    def image_size(track_count: int) -> int:
        return sum(sectors_per_track(t) for t in range(1, track_count + 1)) * 256

    @staticmethod
    def offset(track: int, sector: int) -> int:
        if track < 1 or sector < 0 or sector >= sectors_per_track(track):
            raise ValueError("bad track/sector %d/%d" % (track, sector))
        return (sum(sectors_per_track(t) for t in range(1, track)) + sector) * 256

    @classmethod
    def load(cls, path) -> "D64":
        with open(path, "rb") as stream:
            return cls(stream.read())

    def save(self, path) -> None:
        with open(path, "wb") as stream:
            stream.write(self.data)

    @classmethod
    def blank(cls, name: str = "DISK", disk_id: str = "00", track_count: int = 35) -> "D64":
        image = cls(track_count=track_count)
        bam = bytearray(256)
        bam[0] = DIRECTORY_TRACK
        bam[1] = 1
        bam[2] = 0x41
        for track in range(1, 36):
            count = sectors_per_track(track)
            bam[4 * track] = count
            bits = (1 << count) - 1
            bam[4 * track + 1] = bits & 0xFF
            bam[4 * track + 2] = (bits >> 8) & 0xFF
            bam[4 * track + 3] = (bits >> 16) & 0xFF
        bam[0x90:0xA0] = petscii_name(name)
        bam[0xA0:0xA2] = b"\xa0\xa0"
        ident = disk_id.upper().encode("ascii")[:2].ljust(2, b"0")
        bam[0xA2:0xA4] = ident
        bam[0xA4] = 0xA0
        bam[0xA5:0xA7] = b"2A"
        bam[0xA7:0xAB] = b"\xa0" * 4
        image.write_sector(18, 0, bam)
        image.allocate(18, 0)
        first_dir = bytearray(256)
        first_dir[1] = 0xFF
        image.write_sector(18, 1, first_dir)
        image.allocate(18, 1)
        return image

    # Sector access -------------------------------------------------------

    def read_sector(self, track: int, sector: int) -> bytes:
        off = self.offset(track, sector)
        return bytes(self.data[off:off + 256])

    def write_sector(self, track: int, sector: int, payload) -> None:
        if len(payload) != 256:
            raise ValueError("sector payload must be 256 bytes")
        off = self.offset(track, sector)
        self.data[off:off + 256] = payload

    # BAM -----------------------------------------------------------------

    @property
    def name(self) -> str:
        return name_from_petscii(self.read_sector(18, 0)[0x90:0xA0])

    @property
    def disk_id(self) -> str:
        return self.read_sector(18, 0)[0xA2:0xA4].decode("ascii", "replace")

    def _bam_bit(self, track, sector):
        off = self.offset(18, 0) + 4 * track + 1 + (sector >> 3)
        return off, 1 << (sector & 7)

    def is_free(self, track: int, sector: int) -> bool:
        off, mask = self._bam_bit(track, sector)
        return bool(self.data[off] & mask)

    def allocate(self, track: int, sector: int) -> None:
        off, mask = self._bam_bit(track, sector)
        if self.data[off] & mask:
            self.data[off] &= ~mask & 0xFF
            self.data[self.offset(18, 0) + 4 * track] -= 1

    def free(self, track: int, sector: int) -> None:
        off, mask = self._bam_bit(track, sector)
        if not self.data[off] & mask:
            self.data[off] |= mask
            self.data[self.offset(18, 0) + 4 * track] += 1

    def free_on_track(self, track: int) -> int:
        return self.data[self.offset(18, 0) + 4 * track]

    def blocks_free(self) -> int:
        return sum(self.free_on_track(t) for t in range(1, 36) if t != DIRECTORY_TRACK)

    # DOS allocation -------------------------------------------------------

    def _first_track(self) -> int:
        for distance in range(1, 18):
            for track in (DIRECTORY_TRACK - distance, DIRECTORY_TRACK + distance):
                if 1 <= track <= 35 and self.free_on_track(track):
                    return track
        raise ValueError("disk full")

    def _next_track(self, track: int) -> int:
        if track < DIRECTORY_TRACK:
            candidates = list(range(track - 1, 0, -1)) + list(range(DIRECTORY_TRACK + 1, 36))
        else:
            candidates = list(range(track + 1, 36)) + list(range(DIRECTORY_TRACK - 1, 0, -1))
        for candidate in candidates:
            if self.free_on_track(candidate):
                return candidate
        raise ValueError("disk full")

    def _first_free_sector(self, track: int, start: int) -> int:
        count = sectors_per_track(track)
        for i in range(count):
            sector = (start + i) % count
            if self.is_free(track, sector):
                return sector
        raise ValueError("track full")

    def _allocate_first(self):
        track = self._first_track()
        sector = self._first_free_sector(track, 0)
        self.allocate(track, sector)
        return track, sector

    def _allocate_next(self, track: int, sector: int):
        if self.free_on_track(track):
            count = sectors_per_track(track)
            sector += INTERLEAVE
            if sector >= count:
                sector -= count
                if sector:
                    sector -= 1
            sector = self._first_free_sector(track, sector)
        else:
            track = self._next_track(track)
            sector = self._first_free_sector(track, 0)
        self.allocate(track, sector)
        return track, sector

    def _allocate_directory_sector(self, sector: int) -> int:
        start = sector + DIRECTORY_INTERLEAVE
        count = sectors_per_track(DIRECTORY_TRACK)
        if start >= count:
            start -= count
        new = self._first_free_sector(DIRECTORY_TRACK, start)
        self.allocate(DIRECTORY_TRACK, new)
        return new

    # Files ---------------------------------------------------------------

    def add_file(self, name: str, payload: bytes, ftype: str = "PRG"):
        """Write a file with DOS style allocation; returns (track, sector, blocks)."""
        if self.find_entry(name) is not None:
            raise ValueError("file exists: %s" % name)
        payload = bytes(payload)
        chunks = [payload[i:i + 254] for i in range(0, len(payload), 254)] or [b""]
        track, sector = self._allocate_first()
        first = (track, sector)
        blocks = 0
        for index, chunk in enumerate(chunks):
            block = bytearray(256)
            if index + 1 < len(chunks):
                nxt = self._allocate_next(track, sector)
                block[0], block[1] = nxt
            else:
                block[0] = 0
                block[1] = len(chunk) + 1
            block[2:2 + len(chunk)] = chunk
            self.write_sector(track, sector, block)
            blocks += 1
            if index + 1 < len(chunks):
                track, sector = nxt
        self._add_directory_entry(name, ftype, first[0], first[1], blocks)
        return first[0], first[1], blocks

    def _add_directory_entry(self, name, ftype, track, sector, blocks) -> None:
        dir_track, dir_sector = DIRECTORY_TRACK, 1
        while True:
            block = bytearray(self.read_sector(dir_track, dir_sector))
            for slot in range(8):
                base = slot * 32
                if block[base + 2] == 0:
                    entry = bytearray(32)
                    entry[2] = FILE_TYPES[ftype]
                    entry[3] = track
                    entry[4] = sector
                    entry[5:21] = petscii_name(name)
                    entry[30] = blocks & 0xFF
                    entry[31] = blocks >> 8
                    if slot == 0:
                        entry[0], entry[1] = block[0], block[1]
                    block[base:base + 32] = entry
                    self.write_sector(dir_track, dir_sector, block)
                    return
            if block[0] == 0:
                new_sector = self._allocate_directory_sector(dir_sector)
                block[0], block[1] = DIRECTORY_TRACK, new_sector
                self.write_sector(dir_track, dir_sector, block)
                fresh = bytearray(256)
                fresh[1] = 0xFF
                self.write_sector(DIRECTORY_TRACK, new_sector, fresh)
            dir_track, dir_sector = block[0], block[1]

    def directory(self) -> list:
        entries = []
        track, sector = DIRECTORY_TRACK, 1
        seen = set()
        while track and (track, sector) not in seen:
            seen.add((track, sector))
            block = self.read_sector(track, sector)
            for slot in range(8):
                base = slot * 32
                ftype = block[base + 2]
                if ftype == 0:
                    continue
                entries.append(DirEntry(
                    name_from_petscii(block[base + 5:base + 21]),
                    TYPE_NAMES.get(ftype & 0x07, "???"),
                    block[base + 3], block[base + 4],
                    block[base + 30] | (block[base + 31] << 8),
                    bool(ftype & 0x80)))
            track, sector = block[0], block[1]
        return entries

    def find_entry(self, name: str):
        wanted = name.upper()
        for entry in self.directory():
            if entry.name == wanted:
                return entry
        return None

    def read_chain(self, track: int, sector: int) -> bytes:
        out = bytearray()
        seen = set()
        while track:
            if (track, sector) in seen:
                raise ValueError("sector chain loops at %d/%d" % (track, sector))
            seen.add((track, sector))
            block = self.read_sector(track, sector)
            if block[0] == 0:
                out += block[2:block[1] + 1]
            else:
                out += block[2:]
            track, sector = block[0], block[1]
        return bytes(out)

    def read_file(self, name: str) -> bytes:
        entry = self.find_entry(name)
        if entry is None:
            raise FileNotFoundError(name)
        return self.read_chain(entry.track, entry.sector)

    def compare(self, other: "D64") -> list:
        """Return the (track, sector) pairs whose contents differ."""
        differences = []
        for track in range(1, min(self.track_count, other.track_count) + 1):
            for sector in range(sectors_per_track(track)):
                if self.read_sector(track, sector) != other.read_sector(track, sector):
                    differences.append((track, sector))
        return differences


def basic_program(lines, start: int = 0x0801) -> bytes:
    """Build a PRG (with load address) from (line number, raw token bytes)."""
    out = bytearray(start.to_bytes(2, "little"))
    address = start
    for number, body in lines:
        body = bytes(body)
        next_address = address + 4 + len(body) + 1
        out += next_address.to_bytes(2, "little")
        out += number.to_bytes(2, "little")
        out += body + b"\x00"
        address = next_address
    out += b"\x00\x00"
    return bytes(out)
