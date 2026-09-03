#!/usr/bin/env python3
"""Upload the bundled or explicitly selected C64 ROMs to Multitini firmware."""

from __future__ import annotations

import argparse
import hashlib
import sys
import time
import zlib
from pathlib import Path

try:
    import serial
except ImportError:  # pragma: no cover - handled at runtime.
    serial = None


BASIC_SIZE = 8192
KERNAL_SIZE = 8192
CHARACTER_SIZE = 4096
BUNDLE_SIZE = BASIC_SIZE + KERNAL_SIZE + CHARACTER_SIZE

REPO_ROOT = Path(__file__).resolve().parents[1]
BUNDLED_ROM_DIRECTORY = REPO_ROOT / "third_party" / "c64" / "ROMs"
DEFAULT_BASIC_ROM = BUNDLED_ROM_DIRECTORY / "basic.901226-01.bin"
DEFAULT_KERNAL_ROM = BUNDLED_ROM_DIRECTORY / "kernal.901227-03.bin"
DEFAULT_CHARACTER_ROM = BUNDLED_ROM_DIRECTORY / "characters.901225-01.bin"

XMODEM_STX = 0x02
XMODEM_EOT = 0x04
XMODEM_ACK = 0x06
XMODEM_NAK = 0x15
XMODEM_CAN = 0x18
XMODEM_CRC_REQUEST = ord("C")
XMODEM_BLOCK_SIZE = 1024

READY_MARKER = b"[C64] XMODEM-CRC ready"
SUCCESS_MARKER = b"[C64] boot ok"
ERROR_MARKER = b"[C64] error:"


def crc16_ccitt(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def validate_rom_bytes(label: str, data: bytes, expected_size: int) -> None:
    if len(data) != expected_size:
        raise ValueError(
            f"{label} ROM must be exactly {expected_size} bytes; got {len(data)}"
        )
    if len(set(data)) == 1:
        raise ValueError(f"{label} ROM is uniform data and was rejected")


def build_bundle(basic: bytes, kernal: bytes, character: bytes) -> bytes:
    validate_rom_bytes("BASIC", basic, BASIC_SIZE)
    validate_rom_bytes("KERNAL", kernal, KERNAL_SIZE)
    validate_rom_bytes("character", character, CHARACTER_SIZE)
    bundle = basic + kernal + character
    if len(bundle) != BUNDLE_SIZE:
        raise AssertionError("internal C64 ROM bundle size mismatch")
    return bundle


def load_rom(label: str, path: Path, expected_size: int) -> bytes:
    data = path.read_bytes()
    validate_rom_bytes(label, data, expected_size)
    digest = hashlib.sha256(data).hexdigest()
    print(f"[host] {label}={path} size={len(data)} sha256={digest}")
    return data


def command_for_bundle(bundle: bytes) -> bytes:
    if len(bundle) != BUNDLE_SIZE:
        raise ValueError(f"bundle must be exactly {BUNDLE_SIZE} bytes")
    crc32 = zlib.crc32(bundle) & 0xFFFFFFFF
    return f":c64rx {BUNDLE_SIZE} 0x{crc32:08X}\r".encode("ascii")


def xmodem_packet(block_number: int, payload: bytes) -> bytes:
    if not 1 <= block_number <= 255:
        raise ValueError("XMODEM block number is out of range")
    if len(payload) != XMODEM_BLOCK_SIZE:
        raise ValueError("XMODEM-1K packet payload must be exactly 1024 bytes")
    crc = crc16_ccitt(payload)
    return bytes(
        [XMODEM_STX, block_number, 0xFF - block_number]
    ) + payload + bytes([(crc >> 8) & 0xFF, crc & 0xFF])


def print_target(data: bytes, quiet: bool) -> None:
    if quiet or not data:
        return
    text = data.decode("utf-8", errors="replace")
    encoding = sys.stdout.encoding or "utf-8"
    safe = text.encode(encoding, errors="replace").decode(encoding, errors="replace")
    sys.stdout.write(safe)
    sys.stdout.flush()


def read_some(port: "serial.Serial") -> bytes:
    waiting = port.in_waiting
    return port.read(waiting if waiting else 1)


def write_all(port: "serial.Serial", data: bytes) -> None:
    offset = 0
    while offset < len(data):
        written = port.write(data[offset:])
        if written is None or written <= 0:
            raise RuntimeError("serial write made no progress")
        offset += written
    port.flush()


def wait_for_ready(port: "serial.Serial", timeout_s: float, quiet: bool) -> None:
    deadline = time.monotonic() + timeout_s
    received = bytearray()
    marker_end: int | None = None

    while time.monotonic() < deadline:
        data = read_some(port)
        if not data:
            continue
        print_target(data, quiet)
        received += data
        if len(received) > 8192:
            removed = len(received) - 8192
            del received[:removed]
            if marker_end is not None:
                marker_end = max(0, marker_end - removed)
        if ERROR_MARKER in received:
            raise RuntimeError("target rejected the C64 upload command")
        if marker_end is None:
            marker = bytes(received).find(READY_MARKER)
            if marker >= 0:
                marker_end = marker + len(READY_MARKER)
        if marker_end is not None and XMODEM_CRC_REQUEST in received[marker_end:]:
            return
    raise TimeoutError("target did not enter C64 XMODEM receive mode")


def wait_for_reply(port: "serial.Serial", timeout_s: float) -> int:
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        data = port.read(1)
        if data and data[0] in (XMODEM_ACK, XMODEM_NAK, XMODEM_CAN):
            return data[0]
    raise TimeoutError("timeout waiting for XMODEM reply")


def send_xmodem_1k(
    port: "serial.Serial", bundle: bytes, retries: int, quiet: bool
) -> None:
    if len(bundle) != BUNDLE_SIZE or len(bundle) % XMODEM_BLOCK_SIZE != 0:
        raise ValueError("C64 bundle is not an exact XMODEM-1K transfer")

    block_count = len(bundle) // XMODEM_BLOCK_SIZE
    for block_index in range(block_count):
        block_number = block_index + 1
        offset = block_index * XMODEM_BLOCK_SIZE
        packet = xmodem_packet(
            block_number, bundle[offset : offset + XMODEM_BLOCK_SIZE]
        )
        for attempt in range(retries + 1):
            write_all(port, packet)
            reply = wait_for_reply(port, 10.0)
            if reply == XMODEM_ACK:
                break
            if reply == XMODEM_CAN:
                raise RuntimeError("target cancelled the XMODEM transfer")
            if attempt == retries:
                raise RuntimeError(f"target did not accept block {block_number}")
        if not quiet and (block_number % 4 == 0 or block_number == block_count):
            print(f"[host] sent {offset + XMODEM_BLOCK_SIZE}/{len(bundle)} bytes")

    for attempt in range(retries + 1):
        write_all(port, bytes([XMODEM_EOT]))
        reply = wait_for_reply(port, 10.0)
        if reply == XMODEM_ACK:
            return
        if reply == XMODEM_CAN:
            raise RuntimeError("target cancelled at XMODEM end-of-transfer")
        if attempt == retries:
            break
    raise RuntimeError("target did not acknowledge XMODEM end-of-transfer")


def wait_for_boot_result(port: "serial.Serial", timeout_s: float, quiet: bool) -> None:
    deadline = time.monotonic() + timeout_s
    received = bytearray()

    while time.monotonic() < deadline:
        data = read_some(port)
        if not data:
            continue
        print_target(data, quiet)
        received += data
        if len(received) > 8192:
            del received[: len(received) - 8192]
        if SUCCESS_MARKER in received:
            return
        if ERROR_MARKER in received:
            line = bytes(received).split(ERROR_MARKER, 1)[1].splitlines()[0]
            detail = line.decode("utf-8", errors="replace").strip()
            raise RuntimeError(f"target C64 load failed: {detail}")
    raise TimeoutError("target did not report an explicit C64 boot result")


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Upload the bundled or explicitly selected C64 ROMs over UART0"
    )
    parser.add_argument(
        "--basic",
        type=Path,
        default=DEFAULT_BASIC_ROM,
        help="8192-byte BASIC ROM (defaults to the repository copy)",
    )
    parser.add_argument(
        "--kernal",
        type=Path,
        default=DEFAULT_KERNAL_ROM,
        help="8192-byte KERNAL ROM (defaults to the repository copy)",
    )
    parser.add_argument(
        "--character",
        type=Path,
        default=DEFAULT_CHARACTER_ROM,
        help="4096-byte character ROM (defaults to the repository copy)",
    )
    parser.add_argument("--port", default="COM3", help="diagnostic UART0 port")
    parser.add_argument("--baud", type=int, default=921600, help="UART baud rate")
    parser.add_argument("--command-timeout", type=float, default=10.0)
    parser.add_argument("--result-timeout", type=float, default=30.0)
    parser.add_argument("--xmodem-retries", type=int, default=10)
    parser.add_argument("--quiet", action="store_true")
    return parser.parse_args(argv)


def main() -> int:
    args = parse_args()
    if serial is None:
        print("pyserial is required: python -m pip install pyserial", file=sys.stderr)
        return 2
    if args.xmodem_retries < 0:
        raise ValueError("--xmodem-retries cannot be negative")

    basic = load_rom("BASIC", args.basic, BASIC_SIZE)
    kernal = load_rom("KERNAL", args.kernal, KERNAL_SIZE)
    character = load_rom("character", args.character, CHARACTER_SIZE)
    bundle = build_bundle(basic, kernal, character)
    crc32 = zlib.crc32(bundle) & 0xFFFFFFFF
    sha256 = hashlib.sha256(bundle).hexdigest()
    command = command_for_bundle(bundle)
    print(f"[host] bundle size={len(bundle)} crc32=0x{crc32:08X} sha256={sha256}")
    print(f"[host] opening {args.port} @ {args.baud} 8N1")

    with serial.Serial(
        args.port,
        args.baud,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.05,
        write_timeout=2.0,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    ) as port:
        port.reset_input_buffer()
        write_all(port, command)
        wait_for_ready(port, args.command_timeout, args.quiet)
        send_xmodem_1k(port, bundle, args.xmodem_retries, args.quiet)
        wait_for_boot_result(port, args.result_timeout, args.quiet)

    print("[host] target reported C64 boot ok")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("\n[host] interrupted", file=sys.stderr)
        raise SystemExit(130)
    except (OSError, RuntimeError, TimeoutError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
