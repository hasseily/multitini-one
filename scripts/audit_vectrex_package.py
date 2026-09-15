#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit explicit, unencrypted Zynq FSBL + supervisor BIT + application images.

Promoted from the local 20260920 calibrated firmware-payload audit.
Only audit scratch and the explicitly requested JSON report are written.
Header layout is checked against local Bootgen 2025.2 -read output. ELF32
PT_LOAD data is compared by physical load address, never by searching bytes.
Only initialized file bytes are shipped; internal holes and final word padding
must be zero. The BIT payload is regenerated with the explicit Bootgen tool,
so its header removal and configuration-word byte order are independently
checked rather than guessed. No input artifact is changed.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def describe(path, data):
    return {"path": str(path.resolve()), "bytes": len(data), "sha256": digest(data)}


def words(data, offset, count):
    require(offset >= 0 and offset + count * 4 <= len(data),
            f"Header outside image at 0x{offset:x}")
    return struct.unpack_from(f"<{count}I", data, offset)


def bootgen(tool, arguments, cwd):
    command = [str(tool), "-arch", "zynq", *map(str, arguments)]
    if sys.platform == "win32" and tool.suffix.lower() in {".bat", ".cmd"}:
        command = ["cmd.exe", "/d", "/s", "/c", subprocess.list2cmdline(command)]
    launcher = Path(__file__).resolve().with_name("run_native_tool.py")
    command = [sys.executable, "-u", str(launcher), "--", *command]
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    output = result.stdout + result.stderr
    require(result.returncode == 0, f"Bootgen failed:\n{output}")
    require("[ERROR]" not in output and "ERROR:" not in output,
            f"Bootgen reported an error:\n{output}")
    return output


def elf_segments(path):
    data = path.read_bytes()
    require(len(data) >= 52 and data[:7] == b"\x7fELF\x01\x01\x01",
            f"Not ELF32 little-endian: {path}")
    header = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    require(header[1] == 40 and header[0] == 2, f"Not an ARM executable: {path}")
    require(header[8] >= 32, f"Invalid program header size: {path}")
    segments = []
    for index in range(header[9]):
        fields = words(data, header[4] + index * header[8], 8)
        kind, offset, vaddr, paddr, filesz, memsz, flags, alignment = fields
        if kind != 1:
            continue
        require(filesz <= memsz and offset + filesz <= len(data),
                f"Invalid PT_LOAD {index} in {path}")
        require(paddr + memsz <= 0x100000000, f"PT_LOAD address overflow: {path}")
        segments.append({"index": index, "load_address": paddr,
                         "virtual_address": vaddr, "file_bytes": filesz,
                         "memory_bytes": memsz, "flags": flags,
                         "payload": data[offset:offset + filesz]})
    require(any(s["file_bytes"] for s in segments), f"ELF has no initialized PT_LOAD: {path}")
    return data, header[3], segments


def parse_firmware(data):
    require(words(data, 0x20, 2) == (0xAA995566, 0x584C4E58), "Not a Zynq boot image")
    require(sum(words(data, 0x20, 11)) & 0xFFFFFFFF == 0xFFFFFFFF,
            "Boot-header checksum mismatch")
    require(words(data, 0x28, 2) == (0, 0x01010000),
            "Encrypted or unsupported boot image")
    iht_offset, pht_offset = words(data, 0x98, 2)
    require(iht_offset >= 0xa0 and iht_offset % 4 == pht_offset % 4 == 0,
            "Invalid image/partition-header table placement")
    iht = words(data, iht_offset, 5)
    require(iht[0] == 0x01020000 and iht[4] == 0, "Unsupported image-header table")
    require(iht[2] * 4 == pht_offset, "Inconsistent partition-header offsets")
    require(3 <= iht[1] <= 64, "Unexpected partition count")
    image_offset = iht[3] * 4
    images = []
    seen = set()
    occupied = []
    total_partitions = 0
    while image_offset:
        require(image_offset not in seen and len(images) < 3, "Extra or cyclic image header")
        seen.add(image_offset)
        ih = words(data, image_offset, 4)
        require(1 <= ih[3] <= 32, "Invalid image partition count")
        partitions = []
        for index in range(ih[3]):
            ph_offset = ih[1] * 4 + index * 64
            require(ph_offset == pht_offset + total_partitions * 64,
                    "Partition-header order does not match the three-input BIF")
            ph = words(data, ph_offset, 16)
            require(sum(ph) & 0xFFFFFFFF == 0xFFFFFFFF,
                    f"Partition-header checksum mismatch at 0x{ph_offset:x}")
            require(ph[0] == ph[1] == ph[2] and ph[1] != 0,
                    "Encrypted/compressed/empty partition is unsupported")
            require(ph[8] == ph[10] == 0 and ph[9] * 4 == image_offset,
                    "Authenticated/checksummed/mislinked partition is unsupported")
            require(ph[7] == (ih[3] if index == 0 else 0) and not any(ph[11:15]),
                    "Unexpected partition count or reserved header fields")
            length, offset = ph[1] * 4, ph[5] * 4
            require(offset >= pht_offset + iht[1] * 64 and offset + length <= len(data),
                    "Partition payload outside firmware")
            require(all(offset + length <= start or offset >= end for start, end in occupied),
                    "Overlapping partition payloads")
            occupied.append((offset, offset + length))
            payload = data[offset:offset + length]
            partitions.append({"offset": offset, "bytes": length,
                               "load_address": ph[3], "entry_address": ph[4],
                               "attributes": ph[6], "sha256": digest(payload),
                               "payload": payload})
            total_partitions += 1
        images.append(partitions)
        image_offset = ih[0] * 4
    require(len(images) == 3 and total_partitions == iht[1],
            "Expected exactly FSBL, supervisor BIT, and application images")
    require(max(end for _, end in occupied) == len(data), "Unexpected bytes after final partition")
    require(len(images[0]) == len(images[1]) == 1, "FSBL and BIT must each have one partition")
    fsbl = images[0][0]
    bh = words(data, 0x30, 5)
    require(tuple(bh) == (fsbl["offset"], fsbl["bytes"], fsbl["load_address"],
                          fsbl["entry_address"], fsbl["bytes"]),
            "FSBL boot-header fields do not match its partition")
    return images


def verify_elf(partitions, path):
    data, entry, segments = elf_segments(path)
    used = set()
    records = []
    for pindex, partition in enumerate(partitions):
        require(partition["attributes"] == 0x10, f"Unexpected ARM partition attributes: {path}")
        require(partition["entry_address"] == (entry if pindex == 0 else 0),
                f"Unexpected ARM entry address: {path}")
        base = partition["load_address"]
        limit = base + partition["bytes"]
        members = [s for s in segments if s["file_bytes"] and
                   base <= s["load_address"] and
                   s["load_address"] + s["file_bytes"] <= limit]
        require(members and min(s["load_address"] for s in members) == base,
                f"Unmatched partition at load address 0x{base:x}: {path}")
        last_file_address = max(s["load_address"] + s["file_bytes"] for s in members)
        require((last_file_address - base + 3) & ~3 == partition["bytes"],
                f"Unexpected partition extent/padding at 0x{base:x}: {path}")
        expected = bytearray(partition["bytes"])
        ranges = []
        for segment in members:
            require(segment["index"] not in used, f"ELF segment packaged twice: {path}")
            start = segment["load_address"] - base
            end = start + segment["file_bytes"]
            require(all(end <= a or start >= b for a, b in ranges), f"Overlapping ELF segments: {path}")
            ranges.append((start, end))
            expected[start:end] = segment["payload"]
            used.add(segment["index"])
        actual = partition["payload"]
        if actual != expected:
            mismatch = next(i for i, (a, b) in enumerate(zip(actual, expected)) if a != b)
            raise ValueError(f"ELF payload mismatch at physical address 0x{base + mismatch:x}: {path}")
        records.append({**{k: v for k, v in partition.items() if k != "payload"},
                        "elf_segments": [s["index"] for s in members],
                        "zero_gap_and_padding_bytes": len(expected) - sum(s["file_bytes"] for s in members)})
    require(used == {s["index"] for s in segments if s["file_bytes"]},
            f"ELF initialized PT_LOAD segment missing from image: {path}")
    return {**describe(path, data), "entry_address": entry, "partitions": records,
            "pt_load": [{**{k: v for k, v in s.items() if k != "payload"},
                         "initialized_bytes_verified": bool(s["file_bytes"]),
                         "bss_bytes_not_shipped": s["memory_bytes"] - s["file_bytes"]}
                        for s in segments]}


def process_bit(tool, source, temporary, basename):
    copied = temporary / (basename + ".bit")
    shutil.copyfile(source, copied)
    bif = temporary / (basename + ".bif")
    bif.write_text(f"all:\n{{\n    {copied.name}\n}}\n", encoding="ascii")
    log = bootgen(tool, ["-image", bif, "-process_bitstream", "bin", "-w", "on"], temporary)
    return Path(str(copied) + ".bin").read_bytes(), log


def verify_core_manifest(manifest, binary, data, abi):
    fields, variants, hashes = {}, {}, {}
    for raw in manifest.read_text(encoding="ascii").splitlines():
        line = raw.strip()
        if line.startswith("# ") and ".sha256=" in line:
            key, value = line[2:].split(".sha256=", 1)
            require(key not in hashes, "Duplicate core SHA256 comment")
            hashes[key] = value
        elif line and not line.startswith("#"):
            require("=" in line, "Invalid core manifest line")
            key, value = line.split("=", 1)
            if key == "variant":
                parts = value.split("|")
                require(len(parts) == 5 and parts[0] not in variants,
                        "Invalid or duplicate core variant")
                variants[parts[0]] = parts
            else:
                require(key not in fields, "Duplicate core manifest field")
                fields[key] = value
    require(set(fields) == {"schema", "id", "name", "core_id", "abi", "content_root"},
            "Unexpected core manifest fields")
    require(fields["schema"] == "1" and fields["id"] == "vectrex" and
            fields["name"] and int(fields["core_id"], 0) == 0x56454358 and
            fields["abi"] == abi and fields["content_root"] == "0:/multitini/cores/vectrex/media",
            "Wrong Vectrex core identity, ABI or content root")
    require(set(variants) == set(hashes) == {"native"}, "Expected exactly the native Vectrex variant")
    _, label, filename, size, crc = variants["native"]
    require(label and filename == "vectrex-native.bin" and
            (manifest.parent / filename).resolve() == binary.resolve(),
            "Core manifest does not address the explicit core binary")
    require(int(size) == len(data) and int(crc, 0) == binascii.crc32(data) & 0xffffffff and
            hashes["native"] == digest(data), "Core manifest size/CRC32/SHA256 mismatch")
    return {**describe(manifest, manifest.read_bytes()), "identity": fields,
            "variant": variants["native"], "crc32": f"{binascii.crc32(data) & 0xffffffff:08X}"}


def audit(args):
    firmware, fsbl, bit, app, tool = [p.resolve() for p in
                                    (args.firmware, args.fsbl, args.bit, args.app, args.bootgen)]
    core_bit, core_bin, core_manifest = [p.resolve() for p in
                                        (args.core_bit, args.core_bin, args.core_manifest)]
    for path in (firmware, fsbl, bit, app, tool, core_bit, core_bin, core_manifest):
        require(path.is_file(), f"Missing explicit input: {path}")
    marker = Path(str(bit) + ".kind")
    require(marker.is_file() and marker.read_text().strip() == "SUPERVISOR",
            f"Missing or incorrect supervisor kind marker: {marker}")
    core_marker = Path(str(core_bit) + ".kind")
    require(core_marker.is_file() and core_marker.read_text().strip() == "VECTREX",
            f"Missing or incorrect Vectrex kind marker: {core_marker}")
    input_paths = (firmware, fsbl, bit, app, core_bit, core_bin, core_manifest, marker, core_marker)
    before = {str(path): digest(path.read_bytes()) for path in input_paths}
    data = firmware.read_bytes()
    images = parse_firmware(data)
    fsbl_report = verify_elf(images[0], fsbl)
    app_report = verify_elf(images[2], app)
    scratch = Path(__file__).resolve().parents[1] / "build/vectrex-package-audit"
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="payload-audit-", dir=scratch) as directory:
        temp = Path(directory)
        processed, bit_log = process_bit(tool, bit, temp, "supervisor")
        core_processed, core_log = process_bit(tool, core_bit, temp, "core")
        readback = bootgen(tool, ["-read", firmware], temp)
    partition = images[1][0]
    require(partition["attributes"] == 0x20 and partition["load_address"] == 0 and
            partition["entry_address"] == 0, "Unexpected PL partition attributes/address")
    require(partition["payload"] == processed,
            "Supervisor partition differs from Bootgen-processed explicit BIT")
    core_data = core_bin.read_bytes()
    require(core_data == core_processed,
            "Native core binary differs from Bootgen-processed explicit Vectrex BIT")
    core_manifest_report = verify_core_manifest(core_manifest, core_bin, core_data, args.abi)
    require(all(digest(path.read_bytes()) == before[str(path)] for path in input_paths),
            "An input changed during the audit; rerun against stable final artifacts")
    report = {"status": "PASS", "calibration_only": args.calibration_only,
              "scope": "unencrypted Zynq three-input Bootgen image; initialized ELF PT_LOAD bytes, zero gaps/padding, processed BIT, header checksums",
              "firmware": describe(firmware, data), "fsbl": fsbl_report,
              "supervisor_bit": {**describe(bit, bit.read_bytes()),
                                 "processed_bytes": len(processed), "processed_sha256": digest(processed),
                                 "partition": {k: v for k, v in partition.items() if k != "payload"}},
              "application": app_report,
              "native_core": {"bit": describe(core_bit, core_bit.read_bytes()),
                              "binary": describe(core_bin, core_data),
                              "manifest": core_manifest_report,
                              "bootgen_process_output": core_log},
              "bootgen": str(tool),
              "bootgen_process_output": bit_log, "bootgen_readback": readback,
              "limitations": ["BSS-only memory is initialized by software; it has no file bytes to package",
                              "This audit establishes artifact identity, not hardware execution or timing"]}
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("firmware", "fsbl", "bit", "app"):
        parser.add_argument(name, type=Path)
    parser.add_argument("--bootgen", type=Path, required=True)
    parser.add_argument("--core-bit", type=Path, required=True)
    parser.add_argument("--core-bin", type=Path, required=True)
    parser.add_argument("--core-manifest", type=Path, required=True)
    parser.add_argument("--abi", default="0.11")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--calibration-only", action="store_true")
    args = parser.parse_args()
    try:
        if args.report:
            require(args.report.resolve() not in
                    {p.resolve() for p in (args.firmware, args.fsbl, args.bit, args.app,
                     args.bootgen, args.core_bit, args.core_bin, args.core_manifest,
                     Path(str(args.bit) + ".kind"), Path(str(args.core_bit) + ".kind"))},
                    "Report must not overwrite an input")
        report = audit(args)
        output = json.dumps(report, indent=2) + "\n"
        if args.report:
            args.report.write_text(output, encoding="utf-8")
            print(f"PASS: {args.report}")
            print(f"  Firmware: {report['firmware']['bytes']} bytes SHA256={report['firmware']['sha256']}")
            print(f"  FSBL partitions: {len(report['fsbl']['partitions'])}; application partitions: {len(report['application']['partitions'])}")
            print(f"  Native core: {report['native_core']['binary']['bytes']} bytes SHA256={report['native_core']['binary']['sha256']}")
        else:
            print(output, end="")
    except (ValueError, OSError, struct.error, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
