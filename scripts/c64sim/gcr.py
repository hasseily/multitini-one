# SPDX-License-Identifier: GPL-3.0-only
"""GCR encoding and 1541 track byte streams.

The track layout mirrors hdl/core/c64/ports/c1541_gcr.sv: per sector a
5 byte sync, a 10 byte GCR header built from
[$08, checksum, sector, track, id2, id1, $0F, $0F], a 9 byte header gap,
another sync, a 325 byte GCR data block built from
[$07, 256 data bytes, checksum, $00, $00] and an inter-sector gap that
pads the track to the zone capacity.
"""

from __future__ import annotations

GCR_ENCODE = (
    0x0A, 0x0B, 0x12, 0x13, 0x0E, 0x0F, 0x16, 0x17,
    0x09, 0x19, 0x1A, 0x1B, 0x0D, 0x1D, 0x1E, 0x15,
)
# Invalid codes decode to $F like the HDL decoder does.
GCR_DECODE = [0x0F] * 32
for _value, _code in enumerate(GCR_ENCODE):
    GCR_DECODE[_code] = _value
GCR_VALID = [False] * 32
for _code in GCR_ENCODE:
    GCR_VALID[_code] = True

# Zone capacity in bytes per revolution and byte period in microseconds,
# indexed by the speed bits (VIA2 PB6:5). Zone 3 is the outer (track 1)
# zone at 26 us per byte, zone 0 the inner zone at 32 us per byte.
ZONE_CAPACITY = (6250, 6666, 7142, 7692)
ZONE_BYTE_PERIOD_US = (32, 30, 28, 26)
MAX_HALF_TRACKS = 84
SYNC_LENGTH = 5
HEADER_GAP = 9
HEADER_GCR_LENGTH = 10
DATA_GCR_LENGTH = 325


def sectors_per_track(track: int) -> int:
    if track < 18:
        return 21
    if track < 25:
        return 19
    if track < 31:
        return 18
    return 17


def speed_zone(track: int) -> int:
    if track < 18:
        return 3
    if track < 25:
        return 2
    if track < 31:
        return 1
    return 0


def track_capacity(track: int) -> int:
    return ZONE_CAPACITY[speed_zone(track)]


def encode(data: bytes) -> bytes:
    """Encode a multiple of 4 bytes into GCR (5 bytes per 4)."""
    if len(data) % 4:
        raise ValueError("GCR input must be a multiple of 4 bytes")
    out = bytearray()
    enc = GCR_ENCODE
    for i in range(0, len(data), 4):
        bits = 0
        for b in data[i:i + 4]:
            bits = (bits << 10) | (enc[b >> 4] << 5) | enc[b & 0x0F]
        out += bits.to_bytes(5, "big")
    return bytes(out)


def decode(gcr: bytes, strict: bool = False) -> bytes:
    """Decode a multiple of 5 GCR bytes (4 data bytes per 5)."""
    if len(gcr) % 5:
        raise ValueError("GCR data must be a multiple of 5 bytes")
    out = bytearray()
    dec = GCR_DECODE
    for i in range(0, len(gcr), 5):
        bits = int.from_bytes(gcr[i:i + 5], "big")
        for k in range(4):
            hi = (bits >> (35 - 10 * k)) & 0x1F
            lo = (bits >> (30 - 10 * k)) & 0x1F
            if strict and not (GCR_VALID[hi] and GCR_VALID[lo]):
                raise ValueError("invalid GCR code")
            out.append((dec[hi] << 4) | dec[lo])
    return bytes(out)


def header_block(track: int, sector: int, id1: int, id2: int) -> bytes:
    checksum = sector ^ track ^ id1 ^ id2
    return bytes([0x08, checksum, sector, track, id2, id1, 0x0F, 0x0F])


def data_block(data: bytes) -> bytes:
    if len(data) != 256:
        raise ValueError("sector data must be 256 bytes")
    checksum = 0
    for b in data:
        checksum ^= b
    return bytes([0x07]) + bytes(data) + bytes([checksum, 0x00, 0x00])


def build_track(track: int, sectors: list, id1: int, id2: int) -> bytearray:
    """Build the raw GCR byte stream of one track from its sector data."""
    count = sectors_per_track(track)
    if len(sectors) != count:
        raise ValueError("wrong sector count for track %d" % track)
    capacity = track_capacity(track)
    per_sector = SYNC_LENGTH + HEADER_GCR_LENGTH + HEADER_GAP + SYNC_LENGTH + DATA_GCR_LENGTH
    gap = capacity // count - per_sector
    if gap < 1:
        raise ValueError("track %d does not fit its sectors" % track)
    out = bytearray()
    for sector in range(count):
        out += b"\xff" * SYNC_LENGTH
        out += encode(header_block(track, sector, id1, id2))
        out += b"\x55" * HEADER_GAP
        out += b"\xff" * SYNC_LENGTH
        out += encode(data_block(sectors[sector]))
        out += b"\x55" * gap
    out += b"\x55" * (capacity - len(out))
    return out


def find_blocks(raw) -> list:
    """Return the start index of every block that follows a sync mark.

    A sync mark is a run of at least two $FF bytes, the byte aligned
    equivalent of ten or more consecutive one bits.
    """
    n = len(raw)
    start = -1
    for i in range(n):
        if raw[i] != 0xFF:
            start = i
            break
    if start < 0:
        return []
    blocks = []
    run = 0
    # One extra step revisits the start byte so a sync that wraps around
    # the end of the track still yields its block.
    for step in range(n + 1):
        idx = (start + step) % n
        if raw[idx] == 0xFF:
            run += 1
        else:
            if run >= 2:
                blocks.append(idx)
            run = 0
    return blocks


def _read_circular(raw, start: int, length: int) -> bytes:
    n = len(raw)
    if start + length <= n:
        return bytes(raw[start:start + length])
    return bytes(raw[start:]) + bytes(raw[:(start + length) % n])


def decode_track(raw):
    """Decode a raw GCR track into ({sector: data}, [errors])."""
    sectors = {}
    errors = []
    if raw is None:
        return sectors, errors
    pending = None
    blocks = find_blocks(raw)
    # The scan may start between a header and its data block, so the
    # first block is visited again after the last one.
    for start in blocks + blocks[:1]:
        head = decode(_read_circular(raw, start, HEADER_GCR_LENGTH))
        if head[0] == 0x08:
            if head[1] != head[2] ^ head[3] ^ head[4] ^ head[5]:
                errors.append("header checksum at %d" % start)
                pending = None
                continue
            pending = (head[3], head[2])
            continue
        if head[0] == 0x07 and pending is not None:
            block = decode(_read_circular(raw, start, DATA_GCR_LENGTH))
            data = block[1:257]
            checksum = 0
            for b in data:
                checksum ^= b
            if checksum != block[257]:
                errors.append("data checksum for sector %d at %d" % (pending[1], start))
            else:
                sectors[pending[1]] = data
            pending = None
    return sectors, errors


class GCRDisk:
    """A disk as 84 half-track raw GCR streams (None = no data)."""

    def __init__(self, tracks=None, id1: int = 0x30, id2: int = 0x30, write_protected: bool = False):
        if tracks is None:
            tracks = [None] * MAX_HALF_TRACKS
        self.tracks = tracks
        self.id1 = id1
        self.id2 = id2
        self.write_protected = write_protected

    @classmethod
    def from_d64(cls, image, write_protected: bool = False) -> "GCRDisk":
        bam = image.read_sector(18, 0)
        id1 = bam[0xA2]
        id2 = bam[0xA3]
        tracks = [None] * MAX_HALF_TRACKS
        for track in range(1, image.track_count + 1):
            sectors = [image.read_sector(track, s) for s in range(sectors_per_track(track))]
            tracks[(track - 1) * 2] = build_track(track, sectors, id1, id2)
        return cls(tracks, id1, id2, write_protected)

    def to_d64(self, track_count: int = 35):
        """Decode every track back into a D64 image (missing sectors stay zero)."""
        from .d64 import D64

        image = D64(track_count=track_count)
        errors = []
        for track in range(1, track_count + 1):
            raw = self.tracks[(track - 1) * 2]
            sectors, track_errors = decode_track(raw)
            errors.extend("track %d: %s" % (track, e) for e in track_errors)
            for sector in range(sectors_per_track(track)):
                if sector in sectors:
                    image.write_sector(track, sector, sectors[sector])
                else:
                    errors.append("track %d sector %d missing" % (track, sector))
        image.decode_errors = errors
        return image
