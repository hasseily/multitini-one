# SPDX-License-Identifier: GPL-3.0-only
"""Build a native GS/OS S16 application and an OS-free ProDOS delivery disk."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zipfile
from protocol import *

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1] if (HERE.parents[1]/"MULTITINI_ONE_PLAN.md").is_file() else HERE

def asm_bytes(label, data):
    return label + ":\n" + "".join("    .byte " + ",".join(f"${v:02x}" for v in data[i:i+24]) + "\n"
                                      for i in range(0, len(data), 24))

def descriptors():
    output = bytearray()
    for p in profiles():
        kind = [s[0] for s in SECTIONS].index(p["section"])
        bank, address, a, b = p.get("bank", 0), p.get("address", 0), 0, 0
        if kind == 0:
            a, b = 8 | (p["ramwrt"] << 4), 0x80 | (p["all_banks"] << 4)
        elif kind == 2:
            bank, address, a = p["alias"], 0xd800, p["first"]
        elif kind == 3:
            address, a, b = 0xd800, p["altzp"] << 7, p["inhibited"] << 6
        elif kind == 4:
            bank, address, a = 0xe0, p["register"], p["half"] << 7
        output.extend((kind, p["planes"], p["cases"], bank, address & 255, address >> 8, a, b))
    return output

# Original 3x5 bitmap glyphs; no Apple character ROM or external font is shipped.
GLYPHS = {
    " ": [0]*5, ":":[0,2,0,2,0], "-": [0,0,7,0,0], ".":[0,0,0,0,2],
    "A":[2,5,7,5,5], "B":[6,5,6,5,6], "C":[3,4,4,4,3], "D":[6,5,5,5,6],
    "E":[7,4,6,4,7], "F":[7,4,6,4,4], "G":[3,4,5,5,3], "H":[5,5,7,5,5],
    "I":[7,2,2,2,7], "J":[1,1,1,5,2], "K":[5,5,6,5,5], "L":[4,4,4,4,7],
    "M":[5,7,7,5,5], "N":[5,7,7,7,5], "O":[2,5,5,5,2], "P":[6,5,6,4,4],
    "Q":[2,5,5,3,1], "R":[6,5,6,5,5], "S":[3,4,2,1,6], "T":[7,2,2,2,2],
    "U":[5,5,5,5,7], "V":[5,5,5,5,2], "W":[5,5,7,7,5], "X":[5,5,2,5,5],
    "Y":[5,5,2,2,2], "Z":[7,1,2,4,7], "1":[2,6,2,2,7],
}

def banner(message):
    image = [0]*1280
    for col, character in enumerate(message):
        for y, row in enumerate(GLYPHS[character]):
            for x in range(3):
                if row & (4 >> x):
                    for dx in range(2):
                        pixel = 16 + col * 10 + x * 2 + dx
                        image[(y + 1)*160 + pixel//4] |= 3 << (6 - 2*(pixel % 4))
    return bytes(image)

def make_setup():
    out = []
    def store(labels, suffix=""):
        out.extend(f"    sta {label}{suffix}" for label in labels)
    out += ["    lda descriptor+3"]
    store(("source_write", "source_read", "shadow_store", "aux_read", "aux_write"), "+3")
    out += ["    and #$fe", "    cmp #$e0", "    bne setup_not_slow", "    lda #0", "setup_not_slow:"]
    store(("seed_even", "read_even"), "+3")
    out += ["    ora #1"]
    store(("seed_odd", "read_odd"), "+3")
    out += ["    rep #$20", "    lda descriptor+4"]
    store(("seed_even", "seed_odd", "seed_slow0", "seed_slow1", "read_even", "read_odd",
           "read_slow0", "read_slow1", "source_write", "source_read", "shadow_store", "aux_read", "aux_write"), "+1")
    store(("register_store", "register_read"), "+1")
    out += ["    sep #$20", "    lda descriptor+3"]
    store(("lc_first", "lc_second"), "+3")
    out += ["    lda descriptor", "    cmp #2", "    beq setup_lc_zero", "    lda descriptor+3", "    bra setup_lc_bank", "setup_lc_zero:", "    lda #0", "setup_lc_bank:"]
    store(("lc_seed_bank2", "lc_seed_bank1", "lc_seed_e", "lc_window_d_read", "lc_window_e_read",
           "lc_window_d_write", "lc_window_e_write", "lc_window_d_after", "lc_window_e_after"), "+3")
    return "\n".join(out) + "\n"

def prepare_case():
    return """
prepare_case:
    lda descriptor
    cmp #2
    bne prepare_window
    lda descriptor+6
    and #$0f
    ora #$80
    sta lc_first+1
    lda descriptor+6
    lsr
    lsr
    lsr
    lsr
    tax8
    lda switch_opcodes,x
    sta lc_first
    lda case_number
    and #$0f
    ora #$80
    sta lc_second+1
    lda case_number
    lsr
    lsr
    lsr
    lsr
    tax8
    lda switch_opcodes,x
    sta lc_second
    rts
prepare_window:
    cmp #3
    bne prepare_done
    lda case_number
    and #$0f
    ora #$80
    sta lc_window_switch+1
    sta lc_window_switch2+1
    lda case_number
    lsr
    lsr
    lsr
    lsr
    tax8
    lda switch_opcodes,x
    sta lc_window_switch
    sta lc_window_switch2
prepare_done:
    rts
"""

def generate(directory):
    data = descriptors()
    (directory / "generated.inc").write_text(f"PROFILE_COUNT = {len(data)//8}\nINDEX_COUNT = {INDEX_COUNT}\n")
    (directory / "setup.inc").write_text(make_setup())
    tables = asm_bytes("descriptors", data)
    tables += asm_bytes("bit_masks", [1 << i for i in range(8)])
    tables += asm_bytes("odd_bit_masks", [(1 << (i+1)) & 255 for i in range(8)])
    tables += asm_bytes("sentinels", [0x19, 0x2b, 0x3d, 0x4f])
    tables += asm_bytes("markers", [0x96, 0x69])
    tables += asm_bytes("switch_opcodes", [0xaf, 0x8f])
    tables += asm_bytes("signature", b"MMU1\x01")
    tables += asm_bytes("flat_banks", [0, 0, 1, 1])
    tables += asm_bytes("flat_alts", [0, 128, 0, 128])
    tables += asm_bytes("extended_io_banks", [b for b in (0,1,2,3,14,15,126,127) for _ in range(2)])
    tables += asm_bytes("extended_io_speed", [0x80,0x90]*8)
    tables += asm_bytes("aux_state", [8 | ((i & 1) << 5) | ((i & 2) << 3) |
                                     ((i & 4) << 5) | ((i & 8) << 3) for i in range(128)])
    tables += asm_bytes("aux_video", [(i >> 6) & 1 for i in range(128)])
    tables += asm_bytes("aux_store80", [(i >> 4) & 1 for i in range(128)])
    tables += asm_bytes("aux_hires", [(i >> 5) & 1 for i in range(128)])
    tables += asm_bytes("rom_sample_addresses", b"".join((bank << 16 | offset).to_bytes(3, "little")
                          for bank in (0xfc, 0xfd, 0xfe, 0xff) for offset in (0xc071, 0xd800, 0xe800, 0xfbbf)))
    tables += asm_bytes("absent_addresses", b"".join((bank << 16 | offset).to_bytes(3, "little")
                          for bank in (0x80, 0x81, 0x82, 0xdf) for offset in range(8)))
    tables += "slot_triggers: .word slot_none,slot_read3,slot_write3,slot_read1,slot_read7,slot_sticky,slot_release_read,slot_release_write\n"
    tables += asm_bytes("banner", banner("MMU1: DONE - PHOTO WHOLE SCREEN - REBOOT TO EXIT"))
    tables += asm_bytes("warning_banner", banner("MMU1: CHECK STATUS - PHOTO WHOLE SCREEN - REBOOT"))
    tables += prepare_case()
    (directory / "tables.inc").write_text(tables)

def omf(payload):
    """One OMF 2.0 code segment: private, no special memory, bank aligned.

    All internal 16-bit operands are offsets within an aligned bank. The entry
    stub self-copies to bank 05 before any probe, so there are no relocations.
    KIND bits follow Apple's OMF definition (NoSpecial=1000, Private=4000).
    """
    if len(payload) != 65536:
        raise ValueError("payload must occupy exactly one bank")
    names = b"          " + b"\x08MMUPROBE"
    header = bytearray(44)
    body = b"\xf2" + struct.pack("<I", len(payload)) + payload + b"\0"
    struct.pack_into("<III", header, 0, len(header)+len(names)+len(body), 0, len(payload))
    header[14:16] = b"\x04\x02"
    struct.pack_into("<IHHII", header, 16, 65536, 0x5000, 0, 0, 65536)
    struct.pack_into("<HIHH", header, 34, 1, 0, 44, 44+len(names))
    return bytes(header) + names + body

def prodos_volume(files):
    """Original, non-bootable 800 KiB ProDOS data volume; no OS bytes."""
    blocks = 1600
    image = bytearray(blocks * 512)
    for block in range(2, 6):
        struct.pack_into("<HH", image, block*512, block-1 if block>2 else 0, block+1 if block<5 else 0)
    root = memoryview(image)[2*512+4:2*512+43]
    root[0] = 0xf8
    root[1:9] = b"MMUPROBE"
    root[30:33] = bytes((0xc3, 39, 13))
    struct.pack_into("<HHH", root, 33, len(files), 6, blocks)
    allocated = set(range(7))
    next_block = 7
    for n, (name, filetype, data) in enumerate(files):
        count = (len(data)+511)//512
        if count > 256:
            raise ValueError("delivery files must fit a sapling")
        index = next_block
        next_block += 1
        allocated.add(index)
        for i in range(count):
            block = next_block
            next_block += 1
            allocated.add(block)
            image[index*512+i] = block & 255
            image[index*512+256+i] = block >> 8
            chunk = data[i*512:(i+1)*512]
            image[block*512:block*512+len(chunk)] = chunk
        entry = memoryview(image)[2*512+4+(n+1)*39:2*512+4+(n+2)*39]
        entry[0] = 0x20 | len(name)
        entry[1:1+len(name)] = name.encode("ascii")
        entry[16] = filetype
        struct.pack_into("<HH", entry, 17, index, count+1)
        entry[21:24] = len(data).to_bytes(3, "little")
        entry[30] = 0xc3
        struct.pack_into("<H", entry, 37, 2)
    bitmap = memoryview(image)[6*512:7*512]
    bitmap[:] = b"\xff" * 512
    for block in allocated:
        bitmap[block//8] &= ~(0x80 >> (block % 8))
    return bytes(image)

def build(directory, cc65):
    directory.mkdir(parents=True, exist_ok=True)
    generate(directory)
    for name in ("probe.s", "kernels.s", "transport.s", "probe.cfg"):
        shutil.copyfile(HERE/name, directory/name)
    env = {k: v for k, v in os.environ.items() if k.upper() != "PATH"}
    env["PATH"] = os.environ.get("PATH", os.environ.get("Path", ""))
    def run(command):
        result = subprocess.run(list(map(str, command)), cwd=directory, env=env, capture_output=True, text=True)
        (directory / (Path(command[0]).stem + ".log")).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
    suffix = ".exe" if os.name == "nt" else ""
    run([cc65 / ("ca65"+suffix), "-g", "-l", "probe.lst", "-o", "probe.o", "probe.s"])
    run([cc65 / ("ld65"+suffix), "-C", "probe.cfg", "-m", "probe.map", "-Ln", "probe.labels", "-o", "probe.bin", "probe.o"])
    payload = (directory/"probe.bin").read_bytes()
    executable = omf(payload)
    (directory/"MMUPROBE#B30000").write_bytes(executable)
    tester = (HERE/"TESTERS.txt").read_bytes()
    disk = prodos_volume((("MMUPROBE", 0xb3, executable), ("READ.ME", 4, tester.replace(b"\r\n",b"\n").replace(b"\n", b"\r"))))
    (directory/"MMUPROBE.po").write_bytes(disk)
    inputs = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in HERE.iterdir() if p.is_file()}
    manifest = dict(format="MMU1", version=1, profiles=len(list(profiles())), index_count=INDEX_COUNT,
                    source_sha256=inputs, native_gsos_launch_verified=False, original_hardware_verified=False,
                    artifacts={name: hashlib.sha256((directory/name).read_bytes()).hexdigest()
                               for name in ("MMUPROBE#B30000", "MMUPROBE.po", "probe.bin")})
    validation = directory/"validation.json"
    validated = validation.is_file() and json.loads(validation.read_text()).get("probe_sha256") == hashlib.sha256(payload).hexdigest()
    manifest["software_validation"] = "validation.json" if validated else None
    (directory/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
    archive = directory/"IIGS-MMU-PROBE-v1.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        z.write(HERE/"LICENSE", "LICENSE")
        if validated:
            z.write(validation,"validation.json")
            example = directory/"EXAMPLE.png"
            if example.is_file() and json.loads(validation.read_text()).get("example_sha256") == hashlib.sha256(example.read_bytes()).hexdigest():
                z.write(example,"EXAMPLE.png")
        for name in ("MMUPROBE.po", "MMUPROBE#B30000", "manifest.json"):
            z.write(directory/name, name)
        for path in HERE.iterdir():
            if path.is_file():
                z.write(path, "source/"+path.name)
        z.write(HERE/"TESTERS.txt", "TESTERS.txt")
    return archive

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT/"build/iigs-mmu-probe")
    parser.add_argument("--cc65-bin", type=Path, default=Path("D:/Repos/cc65/bin") if os.name=="nt" else Path("/usr/bin"))
    args = parser.parse_args()
    print(build(args.output.resolve(), args.cc65_bin.resolve()))
