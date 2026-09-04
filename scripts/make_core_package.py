#!/usr/bin/env python3
"""Create a runtime-loadable Multitini core package from Vivado bitstreams."""

from __future__ import annotations

import argparse
import binascii
import hashlib
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


BITSTREAM_MIN = 1024 * 1024
BITSTREAM_MAX = 8 * 1024 * 1024


@dataclass(frozen=True)
class Variant:
    variant_id: str
    label: str
    bitstream: Path


def slug(value: str, label: str) -> str:
    if not value or any(ch not in "abcdefghijklmnopqrstuvwxyz0123456789-_"
                        for ch in value):
        raise ValueError(f"{label} must use lowercase a-z, 0-9, '-' or '_'")
    return value


def parse_variant(value: str) -> Variant:
    fields = value.split("|", 2)
    if len(fields) != 3 or not fields[1].strip():
        raise argparse.ArgumentTypeError(
            "variant must be ID|DISPLAY LABEL|PATH.bit")
    try:
        variant_id = slug(fields[0].strip(), "variant ID")
    except ValueError as error:
        raise argparse.ArgumentTypeError(str(error)) from error
    return Variant(variant_id, fields[1].strip(), Path(fields[2].strip()))


def locate_bootgen(explicit: str | None) -> str:
    if explicit:
        candidate = explicit
    elif sys.platform == "win32":
        candidate = shutil.which("bootgen.bat")
    else:
        candidate = shutil.which("bootgen")
    if not candidate:
        raise FileNotFoundError("bootgen was not found; use --bootgen")
    return candidate


def process_bitstream(bootgen: str, source: Path, destination: Path) -> None:
    if not source.is_file():
        raise FileNotFoundError(source)
    if source.suffix.lower() != ".bit":
        raise ValueError(f"expected a Vivado .bit file: {source}")
    source = source.resolve()
    destination = destination.resolve()
    with tempfile.TemporaryDirectory(prefix="multitini-core-") as temp_name:
        temp = Path(temp_name)
        temp_source = temp / "core.bit"
        shutil.copyfile(source, temp_source)
        bif = temp / "core.bif"
        bif.write_text(
            "all:\n{\n    core.bit\n}\n",
            encoding="utf-8",
        )
        command = [bootgen, "-arch", "zynq", "-image", str(bif),
                   "-process_bitstream", "bin", "-w", "on"]
        if sys.platform == "win32" and Path(bootgen).suffix.lower() in {
                ".bat", ".cmd"}:
            command = ["cmd.exe", "/d", "/s", "/c",
                       subprocess.list2cmdline(command)]
        subprocess.run(command, check=True, cwd=temp)
        generated = temp / "core.bit.bin"
        if generated.is_file():
            shutil.copyfile(generated, destination)
    if not destination.is_file():
        raise RuntimeError(f"bootgen did not create {destination}")


def validate_content_root(value: str) -> str:
    if (not value.startswith("0:/multitini/cores/") or ".." in value or
            "\\" in value or value.endswith("/")):
        raise ValueError("content root must be below 0:/multitini/cores")
    return value


def create_package(args: argparse.Namespace) -> Path:
    core_id = slug(args.id, "core ID")
    if not args.name.strip() or "\n" in args.name or "=" in args.name:
        raise ValueError("core name is invalid")
    if not 0 < args.core_id <= 0xFFFFFFFF:
        raise ValueError("core ID word must be a nonzero 32-bit value")
    validate_content_root(args.content_root)
    if not args.variants or len(args.variants) > 8:
        raise ValueError("one through eight variants are required")
    variant_ids = [item.variant_id for item in args.variants]
    if len(set(variant_ids)) != len(variant_ids):
        raise ValueError("variant IDs must be unique")

    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    bootgen = locate_bootgen(args.bootgen)
    records: list[tuple[Variant, str, int, int, str]] = []
    for variant in args.variants:
        filename = f"{core_id}-{variant.variant_id}.bin"
        destination = output / filename
        process_bitstream(bootgen, variant.bitstream, destination)
        payload = destination.read_bytes()
        size = len(payload)
        if size < BITSTREAM_MIN or size > BITSTREAM_MAX or size % 4:
            raise ValueError(
                f"processed bitstream size is invalid: {destination} ({size})")
        crc32 = binascii.crc32(payload) & 0xFFFFFFFF
        sha256 = hashlib.sha256(payload).hexdigest()
        records.append((variant, filename, size, crc32, sha256))

    lines = [
        "# Multitini runtime core package",
        "# Generated; do not hand-edit sizes or hashes.",
        "schema=1",
        f"id={core_id}",
        f"name={args.name.strip()}",
        f"core_id=0x{args.core_id:08X}",
        f"abi={args.abi_major}.{args.abi_minor}",
        f"content_root={args.content_root}",
    ]
    for variant, filename, size, crc32, sha256 in records:
        lines.append(f"# {variant.variant_id}.sha256={sha256}")
        lines.append(
            f"variant={variant.variant_id}|{variant.label}|{filename}|"
            f"{size}|0x{crc32:08X}")
    manifest = output / "core.cfg"
    manifest.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"Created {manifest}")
    for _, filename, size, crc32, sha256 in records:
        print(f"  {filename}: {size} bytes CRC32={crc32:08X} SHA256={sha256}")
    return manifest


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--id", required=True, help="lowercase package ID")
    result.add_argument("--name", required=True, help="display name")
    result.add_argument("--core-id", required=True, type=lambda value: int(value, 0))
    result.add_argument("--content-root", required=True)
    result.add_argument("--abi-major", type=int, default=0)
    result.add_argument("--abi-minor", type=int, default=7)
    result.add_argument("--variant", dest="variants", action="append",
                        type=parse_variant,
                        help="ID|DISPLAY LABEL|PATH.bit (repeatable)")
    result.add_argument("--output", required=True, type=Path)
    result.add_argument("--bootgen", help="path to bootgen executable")
    return result


def main() -> int:
    args = parser().parse_args()
    try:
        create_package(args)
    except (OSError, RuntimeError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
