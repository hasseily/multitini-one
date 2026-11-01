#!/usr/bin/env python3
"""Execute the produced HUNK on real FX68K with original library stubs.

This is an offline ABI/control-flow test, not an AmigaOS preferences test.
SPDX-License-Identifier: GPL-3.0-only
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
from helper import build, S, MODES
from m68k import Assembler

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
CODE = 0x10000
SCREEN, BITMAP, MODE = 0x6000, 0x6200, 0x6300
VPE, COLORMAP = 0x6600, 0x6700
PUBLIC_LIST, PUBLIC_OTHER, PUBLIC_NULL, PUBLIC_WB = 0x6400, 0x6420, 0x6440, 0x6460
LIST_LOCKED, SCREEN_LOCKS, LIST_LOCKS, LIST_UNLOCKS = 0x5820, 0x5822, 0x5824, 0x5826
CHILD_BUSY, VERIFY_QUERIES, PUBLISH_AT = 0x5828, 0x582A, 0x582C
SAVE_REQUEST = 0x5830
OUTPUT_OPENS, OWNED_HANDLE, OUTPUT_LIVE, OUTPUT_CLOSES = 0x5832, 0x5834, 0x5836, 0x5838
STREAM_ERROR, PARENT_CLOSED, LEGACY_CALLS, OUTPUT_ATTEMPTS = 0x583A, 0x583C, 0x583E, 0x5840
CLIP_CALLS, CLIP_ERROR, VIEW_X, VIEW_Y = 0x584A, 0x584C, 0x5850, 0x5852
POLLS, DEADLINE, LAUNCHES, RETURNED, CONFIG = 0x5802, 0x5806, 0x5808, 0x580A, 0x580C


def memory_image(legacy_probe=False):
    r = build()
    memory = bytearray(0x100000)
    memory[CODE:CODE + len(r["code"])] = r["code"]
    a = Assembler()
    i, lab = a.ins, a.label
    vectors = []
    absolute = lambda value: f"0x{value:x}.l"
    state = lambda name: absolute(CODE + r["assembler"].labels["state"] + S[name])
    vector = lambda base, bias, name: vectors.append((base - bias, name))
    for bias in (132, 138, 360):
        vector(0x1000, bias, "zero")
    vector(0x1000, 390, "find_port")
    vector(0x1000, 354, "add_port")
    vector(0x1000, 414, "close_library")
    vector(0x1000, 306, "zero")
    vector(0x1000, 552, "open_library")
    for bias, name in ((30, "open_file"), (36, "close_file"), (42, "read"),
                       (48, "write"), (72, "delete"), (132, "ioerr"), (198, "delay"),
                       (222, "execute"), (606, "legacy_system")):
        vector(0x2000, bias, name)
    for bias, name in ((474, "overscan"), (510, "screen"), (516, "zero"),
                       (522, "public_list"), (528, "public_unlock")):
        vector(0x3000, bias, name)
    for bias, name in ((708, "video_control"), (756, "monitor"), (792, "get_mode"), (798, "available")):
        vector(0x4000, bias, name)
    lab("zero"); i("moveq", "#0", "d0"); i("rts")
    lab("one"); i("moveq", "#1", "d0"); i("rts")
    lab("find_port")
    i("cmpi.w", "#9", absolute(CONFIG)); i("beq", "one")
    i("bra", "zero")
    lab("add_port"); i("move.l", "a1", "0x5818.l"); i("bra", "zero")
    lab("close_library"); i("addq.w", "#1", "0x581c.l"); i("bra", "zero")
    lab("open_library")
    i("cmpi.w", "#5", absolute(CONFIG)); i("beq", "zero")
    i("cmpi.w", "#11", absolute(CONFIG)); i("bne", "library_checked")
    i("cmpi.b", "#103", "(a1)"); i("beq", "zero")
    lab("library_checked")
    i("cmpi.b", "#100", "(a1)"); i("beq", "dos")
    i("cmpi.b", "#105", "(a1)"); i("beq", "intuition")
    i("move.l", "#0x4000", "d0"); i("rts")
    lab("dos"); i("move.l", "#0x2000", "d0"); i("rts")
    lab("intuition"); i("move.l", "#0x3000", "d0"); i("rts")
    lab("delete")
    i("cmpi.w", "#14", absolute(CONFIG)); i("beq", "zero")
    i("cmpi.w", "#15", absolute(CONFIG)); i("beq", "zero")
    i("bra", "one")
    lab("ioerr")
    i("move.l", "#205", "d0")
    i("cmpi.w", "#14", absolute(CONFIG)); i("bne", "ioerr_done")
    i("move.l", "#202", "d0")
    lab("ioerr_done"); i("rts")
    lab("open_file")
    i("move.l", "d1", "a0"); i("cmpi.b", "#78", "(a0)"); i("bne", "one")
    for offset, value in enumerate(b"NIL:\0"):
        i("cmpi.b", f"#{value}", f"{offset}(a0)"); i("bne", "stream_error")
    i("cmpi.l", "#1006", "d2"); i("bne", "stream_error")
    i("addq.w", "#1", absolute(OUTPUT_ATTEMPTS))
    i("cmpi.w", "#31", absolute(CONFIG)); i("beq", "zero")
    i("cmpi.w", "#33", absolute(CONFIG)); i("bne", "nil_open_allowed")
    i("cmpi.w", "#2", absolute(OUTPUT_ATTEMPTS)); i("beq", "zero")
    lab("nil_open_allowed")
    i("tst.w", absolute(OUTPUT_LIVE)); i("bne", "stream_error")
    i("addq.w", "#1", absolute(OUTPUT_OPENS))
    i("moveq", "#0", "d0"); i("move.w", absolute(OUTPUT_OPENS), "d0")
    i("addi.w", "#0x100", "d0"); i("move.w", "d0", absolute(OWNED_HANDLE))
    i("move.w", "#1", absolute(OUTPUT_LIVE)); i("rts")
    lab("close_file")
    i("cmpi.l", "#1", "d1"); i("beq", "one")  # Receipt/OSCN handle, not a launch output.
    i("moveq", "#0", "d0"); i("move.w", absolute(OWNED_HANDLE), "d0")
    i("cmp.l", "d1", "d0"); i("bne", "stream_error")
    i("tst.w", absolute(OUTPUT_LIVE)); i("beq", "stream_error")
    i("clr.w", absolute(OUTPUT_LIVE)); i("addq.w", "#1", absolute(OUTPUT_CLOSES))
    i("cmpi.w", "#32", absolute(CONFIG)); i("beq", "zero")
    i("bra", "one")
    lab("stream_error")
    i("move.w", "#0xdead", absolute(STREAM_ERROR)); i("bra", "zero")
    lab("public_list")
    i("tst.w", absolute(PUBLISH_AT)); i("beq", "public_publish_done")
    i("move.w", absolute(POLLS), "d0"); i("cmp.w", absolute(PUBLISH_AT), "d0")
    i("bcs", "public_publish_done")
    i("bsr", "publish_screen"); i("clr.w", absolute(PUBLISH_AT))
    lab("public_publish_done")
    i("cmpi.w", "#3", state("phase")); i("bcs", "public_not_verifying")
    i("addq.w", "#1", absolute(VERIFY_QUERIES))
    lab("public_not_verifying")
    i("move.w", "#1", absolute(LIST_LOCKED))
    i("addq.w", "#1", absolute(LIST_LOCKS))
    i("move.l", f"#{PUBLIC_WB}", absolute(PUBLIC_NULL))
    i("move.w", "#0", absolute(PUBLIC_WB + 18))
    i("cmpi.w", "#24", absolute(CONFIG)); i("beq", "public_absent")
    i("cmpi.w", "#26", absolute(CONFIG)); i("bne", "public_private_check")
    i("tst.w", absolute(POLLS)); i("beq", "public_ready")
    i("cmpi.w", "#4", absolute(POLLS)); i("bcs", "public_absent")
    lab("public_private_check")
    i("cmpi.w", "#25", absolute(CONFIG)); i("bne", "public_ready")
    i("cmpi.w", "#3", absolute(POLLS)); i("bcc", "public_ready")
    i("move.w", "#1", absolute(PUBLIC_WB + 18))
    i("bra", "public_ready")
    lab("public_absent")
    i("move.l", f"#{PUBLIC_LIST + 4}", absolute(PUBLIC_NULL))
    lab("public_ready")
    i("move.l", f"#{PUBLIC_LIST}", "d0"); i("rts")
    lab("public_unlock")
    i("move.w", "#0", absolute(LIST_LOCKED))
    i("addq.w", "#1", absolute(LIST_UNLOCKS)); i("rts")
    lab("screen")
    i("addq.w", "#1", absolute(SCREEN_LOCKS))
    i("cmpi.w", "#19", absolute(CONFIG)); i("bne", "screen_ready")
    i("cmpi.w", "#3", absolute(POLLS)); i("bcs", "zero")
    lab("screen_ready"); i("move.l", f"#{SCREEN}", "d0"); i("rts")
    lab("get_mode"); i("move.l", absolute(MODE), "d0"); i("rts")
    lab("available")
    i("cmpi.w", "#4", absolute(CONFIG)); i("beq", "one")
    i("cmpi.w", "#8", absolute(CONFIG)); i("beq", "one")
    i("moveq", "#0", "d0"); i("rts")
    lab("monitor")
    i("move.w", absolute(VIEW_X), "20(a1)"); i("move.w", absolute(VIEW_Y), "22(a1)")
    for offset, value in ((28, 0), (30, 0), (32, 511), (34, 311), (80, 129), (82, 44)):
        i("move.w", f"#{value}", f"{offset}(a1)")
    i("cmpi.w", "#39", absolute(CONFIG)); i("bne", "monitor_range_ready")
    i("move.w", "#100", "28(a1)")  # Proposed maximum ViewX93 is outside the permitted range.
    lab("monitor_range_ready")
    i("cmpi.w", "#34", absolute(CONFIG)); i("bne", "monitor_complete")
    i("moveq", "#80", "d0"); i("rts")
    lab("monitor_complete")
    i("moveq", "#96", "d0"); i("rts")
    lab("overscan")
    # Query coordinates move with the current monitor origin. At stock129/44
    # MAX starts-72/-15; normalized maximum93/29 makes that same range0/0.
    i("move.w", "#129", "d0"); i("sub.w", absolute(VIEW_X), "d0")
    i("add.w", "d0", "d0"); i("addi.w", "#-72", "d0")
    i("move.w", "d0", "(a1)"); i("addi.w", "#723", "d0"); i("move.w", "d0", "4(a1)")
    i("cmpi.w", "#35", absolute(CONFIG)); i("bne", "overscan_x_ready")
    i("addq.w", "#1", "(a1)"); i("addq.w", "#1", "4(a1)")
    lab("overscan_x_ready")
    i("move.w", "#29", "d0"); i("sub.w", absolute(VIEW_Y), "d0")
    i("move.w", "d0", "2(a1)"); i("addi.w", "#282", "d0"); i("move.w", "d0", "6(a1)")
    i("move.l", "a0", "d0"); i("cmpi.l", "#0x29000", "d0")
    i("beq", "nonlace")
    i("move.w", "2(a1)", "d0"); i("add.w", "d0", "d0"); i("move.w", "d0", "2(a1)")
    i("addi.w", "#565", "d0"); i("move.w", "d0", "6(a1)")
    lab("nonlace")
    i("moveq", "#1", "d0"); i("rts")
    lab("video_control")
    i("addq.w", "#1", absolute(CLIP_CALLS))
    i("move.l", "a0", "d0"); i("cmpi.l", f"#{COLORMAP}", "d0"); i("bne", "clip_error")
    for offset, value in ((0, 0x80000013), (4, 0), (8, 0), (12, 0)):
        i("cmpi.l", f"#{value}", f"{offset}(a1)"); i("bne", "clip_error")
    i("move.l", "#0x80000014", "(a1)")  # GET is consumed; the next call must rearm it.
    i("move.l", f"#{VPE}", "4(a1)")
    i("cmpi.w", "#42", absolute(CONFIG)); i("beq", "one")
    i("cmpi.w", "#43", absolute(CONFIG)); i("bne", "zero")
    i("clr.l", "4(a1)"); i("bra", "zero")
    lab("clip_error")
    i("move.w", "#0xdead", absolute(CLIP_ERROR)); i("bra", "one")
    lab("delay")
    i("addq.w", "#1", absolute(POLLS))
    lab("delay_done"); i("moveq", "#0", "d0"); i("rts")
    lab("execute")
    i("tst.l", "d1"); i("beq", "stream_error")
    i("move.l", "d1", "a0"); i("cmpi.b", "#69", "(a0)"); i("bne", "stream_error")
    i("tst.l", "d2"); i("bne", "stream_error")
    i("moveq", "#0", "d0"); i("move.w", absolute(OWNED_HANDLE), "d0")
    i("cmp.l", "d3", "d0"); i("bne", "stream_error")
    i("tst.w", absolute(OUTPUT_LIVE)); i("beq", "stream_error")
    i("cmpi.w", "#1", "0xde0f08.l"); i("bne", "stream_error")
    i("move.w", "0xde0f1e.l", "d0"); i("andi.w", "#16", "d0"); i("beq", "stream_error")
    i("bra", "shell_command")
    lab("legacy_system")
    # Documented old SYS_Asynch TRUE closes inherited parent handles.
    i("addq.w", "#1", absolute(LEGACY_CALLS))
    i("tst.w", absolute(PARENT_CLOSED)); i("bne", "legacy_closed")
    i("move.l", "d2", "a0")
    i("cmpi.l", "#0x80000023", "(a0)"); i("bne", "legacy_closed")
    i("cmpi.l", "#1", "4(a0)"); i("bne", "legacy_closed")
    i("move.w", "#1", absolute(PARENT_CLOSED))
    i("bsr", "shell_command"); i("moveq", "#0", "d0"); i("rts")
    lab("legacy_closed")
    i("move.w", "#0xdead", absolute(STREAM_ERROR)); i("bra", "minus_one")
    lab("shell_command")
    i("cmpi.w", "#19", absolute(CONFIG)); i("bne", "system_ready")
    i("cmpi.w", "#3", absolute(POLLS)); i("bcs", "zero")
    lab("system_ready")
    i("addq.w", "#1", absolute(LAUNCHES))
    i("cmpi.w", "#2", state("phase")); i("bne", "system_save_checked")
    i("move.w", state("request"), absolute(SAVE_REQUEST))
    lab("system_save_checked")
    i("cmpi.w", "#12", absolute(CONFIG)); i("beq", "zero")
    i("move.l", "0x5810.l", "a0")
    i("move.w", "54(a0)", absolute(VIEW_X)); i("move.w", "56(a0)", absolute(VIEW_Y))
    i("move.w", "#1", absolute(CHILD_BUSY))
    i("clr.w", absolute(VERIFY_QUERIES))
    i("move.w", absolute(POLLS), "d0"); i("addq.w", "#3", "d0")
    i("move.w", "d0", absolute(DEADLINE))
    for scenario in (27, 28, 29):
        i("cmpi.w", f"#{scenario}", absolute(CONFIG)); i("beq", "system_deferred")
    i("bsr", "publish_screen")
    i("bra", "system_done")
    lab("system_deferred")
    # Force a separate settling interval after SAVE too in scenario29.
    i("cmpi.w", "#29", absolute(CONFIG)); i("bne", "system_done")
    i("cmpi.w", "#2", state("phase")); i("bne", "system_done")
    i("move.w", "#320", absolute(SCREEN + 68))
    i("move.w", "#200", absolute(SCREEN + 70))
    lab("system_done"); i("moveq", "#-1", "d0"); i("rts")
    lab("publish_screen")
    i("clr.w", absolute(SCREEN + 8)); i("clr.w", absolute(SCREEN + 10))
    i("clr.w", absolute(VPE + 28)); i("clr.w", absolute(VPE + 30))
    for name, offset in (("want_width", 12), ("want_height", 14)):
        i("move.w", state(name), absolute(SCREEN + offset))
    i("move.w", state("want_width"), absolute(SCREEN + 68))
    i("move.w", state("want_height"), absolute(SCREEN + 70))
    for name, offset in (("want_width", 32), ("want_height", 34)):
        i("move.w", state(name), "d0"); i("subq.w", "#1", "d0")
        i("move.w", "d0", absolute(VPE + offset))
    for scenario in (36, 41):
        i("cmpi.w", f"#{scenario}", absolute(CONFIG)); i("beq", "publish_negative_clip")
    i("bra", "publish_clip_ready")
    lab("publish_negative_clip")
    for offset, delta in ((28, -72), (30, -30), (32, -72), (34, -30)):
        i("addi.w", f"#{delta}", absolute(VPE + offset))
    i("cmpi.w", "#41", absolute(CONFIG)); i("bne", "publish_clip_ready")
    i("move.w", "#-72", absolute(SCREEN + 8)); i("move.w", "#-30", absolute(SCREEN + 10))
    lab("publish_clip_ready")
    i("cmpi.w", "#40", absolute(CONFIG)); i("bne", "publish_width_ready")
    i("move.w", "#639", absolute(VPE + 32))
    lab("publish_width_ready")
    i("move.l", state("want_mode"), absolute(MODE))
    i("move.w", state("want_depth"), "d0")
    i("move.b", "d0", absolute(BITMAP + 5))
    i("cmpi.w", "#21", absolute(CONFIG)); i("bne", "depth_checked")
    i("move.b", "#4", absolute(BITMAP + 5))
    lab("depth_checked")
    i("cmpi.w", "#22", absolute(CONFIG)); i("bne", "mode_checked")
    i("move.l", "#0x29004", absolute(MODE))
    lab("mode_checked")
    # Virtual bitmap matches, visible ViewPort deliberately does not.
    i("cmpi.w", "#2", absolute(CONFIG)); i("bne", "publish_done")
    i("move.w", "#640", absolute(SCREEN + 68))
    i("move.w", "#256", absolute(SCREEN + 70))
    lab("publish_done"); i("rts")
    lab("read")
    i("move.w", absolute(POLLS), "d0"); i("cmp.w", absolute(DEADLINE), "d0")
    i("bcs", "zero")
    i("move.l", "d2", "a0"); i("move.b", "#48", "(a0)")
    i("cmpi.w", "#16", absolute(CONFIG)); i("bne", "receipt_complete")
    i("tst.w", "0x581e.l"); i("bne", "receipt_complete")
    i("move.w", "#1", "0x581e.l"); i("moveq", "#1", "d0"); i("rts")
    lab("receipt_complete"); i("move.b", "#10", "1(a0)")
    i("clr.w", absolute(CHILD_BUSY))
    i("cmpi.w", "#27", absolute(CONFIG)); i("bne", "receipt_publish_later")
    # Publication occurs inside Read, after the old helper's per-poll query.
    i("bsr", "publish_screen")
    i("bra", "receipt_publish_done")
    lab("receipt_publish_later")
    i("cmpi.w", "#28", absolute(CONFIG)); i("beq", "receipt_defer")
    i("cmpi.w", "#29", absolute(CONFIG)); i("bne", "receipt_publish_done")
    lab("receipt_defer")
    i("move.w", absolute(POLLS), "d0"); i("addq.w", "#3", "d0")
    i("move.w", "d0", absolute(PUBLISH_AT))
    lab("receipt_publish_done")
    i("cmpi.w", "#18", absolute(CONFIG)); i("beq", "receipt_bad")
    i("cmpi.w", "#1", absolute(CONFIG)); i("bne", "read_done")
    lab("receipt_bad")
    i("move.b", "#51", "(a0)")
    lab("read_done"); i("moveq", "#2", "d0"); i("rts")
    lab("minus_one"); i("moveq", "#-1", "d0"); i("rts")
    lab("write")
    i("move.l", "d2", "0x5810.l"); i("move.l", "d3", "0x5814.l")
    i("cmpi.w", "#7", absolute(CONFIG)); i("beq", "zero")
    i("move.l", "d3", "d0"); i("rts")
    stubs = a.finish()
    memory[0x8000:0x8000 + len(stubs)] = stubs
    for address, name in vectors:
        memory[address:address + 6] = struct.pack(">HI", 0x4ef9, 0x8000 + a.labels[name])
    boot = Assembler()
    boot.ins("jsr", f"0x{CODE:x}.l")
    boot.ins("move.w", "#0xabcd", absolute(RETURNED))
    boot.label("finished"); boot.ins("bra", "finished")
    # Reset PC and ExecBase may share the stub library's positive header.
    memory[0x1000:0x1000 + boot.pc] = boot.finish()
    struct.pack_into(">II", memory, 0, 0x0ff000, 0x1000)
    struct.pack_into(">I", memory, SCREEN + 88, BITMAP)
    struct.pack_into(">I", memory, SCREEN + 48, COLORMAP)
    struct.pack_into(">4h", memory, VPE + 28, 0, 0, 639, 255)
    struct.pack_into(">HH", memory, VIEW_X, 129, 44)
    memory[BITMAP + 5] = 4
    struct.pack_into(">I", memory, MODE, 0x29000)
    struct.pack_into(">HH", memory, SCREEN + 12, 640, 256)
    struct.pack_into(">HH", memory, SCREEN + 68, 640, 256)
    # Exercise a real Exec list: unrelated named and null-name nodes precede
    # Workbench. The odd string address requires byte-safe name comparisons.
    struct.pack_into(">III", memory, PUBLIC_LIST, PUBLIC_OTHER, 0, PUBLIC_WB)
    for node, successor, name in ((PUBLIC_OTHER, PUBLIC_NULL, 0x6501),
                                  (PUBLIC_NULL, PUBLIC_WB, 0),
                                  (PUBLIC_WB, PUBLIC_LIST + 4, 0x6521)):
        struct.pack_into(">I", memory, node, successor)
        struct.pack_into(">I", memory, node + 10, name)
        struct.pack_into(">I", memory, node + 14, 0xdeadbeef)  # Never dereference psn_Screen.
    memory[0x6501:0x6501 + 12] = b"Workbencher\0"
    memory[0x6521:0x6521 + 10] = b"Workbench\0"
    if legacy_probe:
        # Self-contained historical API pattern, without importing an ignored
        # build artifact: two launches inheriting the same parent streams.
        probe = Assembler()
        for slot in (0x5842, 0x5846):
            probe.ins("move.l", "#0x2000", "a6")
            probe.ins("lea", "command(pc)", "a0"); probe.ins("move.l", "a0", "d1")
            probe.ins("lea", "tags(pc)", "a0"); probe.ins("move.l", "a0", "d2")
            probe.ins("jsr", "-606(a6)"); probe.ins("move.l", "d0", absolute(slot))
        probe.ins("rts"); probe.label("command"); probe.data(b"Execute old-pattern\0"); probe.align()
        probe.label("tags"); probe.data(struct.pack(">4I", 0x80000023, 1, 0, 0))
        data = probe.finish(); memory[CODE:CODE + len(data)] = data
        r["legacy_probe_sha256"] = hashlib.sha256(data).hexdigest()
    return memory, r


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--vivado", type=Path, default=Path("C:/Xilinx/2025.2/Vivado"))
    args = p.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / "build") or out.exists():
        raise SystemExit("Select a fresh repository-local build directory")
    out.mkdir(parents=True)
    memory, helper = memory_image()
    (out / "memory.hex").write_text("".join(f"{v:04x}\n" for v in struct.unpack(">524288H", memory)))
    legacy_memory, legacy = memory_image(legacy_probe=True)
    (out / "legacy-memory.hex").write_text("".join(f"{v:04x}\n" for v in struct.unpack(">524288H", legacy_memory)))
    upstream = ROOT / "third_party/amiga/upstream/rtl/fx68k"
    for name in ("microrom.mem", "nanorom.mem"):
        shutil.copyfile(upstream / name, out / name)
    tools = args.vivado / "bin"
    env = dict(os.environ)
    env["PATH"] = str(tools) + os.pathsep + env.get("PATH", "")
    guard = [sys.executable, str(ROOT / "scripts/run_native_tool.py"), "--"]
    def run(name, command):
        with (out / (name + ".log")).open("w") as log:
            subprocess.run(guard + ["cmd.exe", "/d", "/c"] + [str(x) for x in command], cwd=out, env=env,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    cases = [(mode, 0) for mode in range(len(MODES))]
    cases += [(1, n) for n in (1, 2, 3, 4, 5, 6, 9, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20)]
    cases += [(2, 7), (3, 8)]
    cases += [(3, n) for n in (1, 2, 3, 13, 17, 18, 19, 20, 21, 22, 23)]
    cases += [(1, n) for n in (24, 25, 26)]
    cases += [(1, n) for n in (27, 28, 29, 30, 31, 32, 33)]
    cases += [(1, 34), (2, 35), (2, 36), (2, 37)]
    cases += [(m, 38) for m in (0, 1, 3)]
    cases += [(2, n) for n in (39, 40, 41, 42, 43)]
    wrappers = out / "tops.sv"
    wrappers.write_text("\n".join(f"module tb_helper_m{m}s{s}; tb_helper #(.MODESEL({m}),.SCENARIO({s})) t(); endmodule" for m,s in cases) +
                        "\nmodule tb_helper_legacy; tb_helper #(.NEGATIVE(1)) t(); endmodule\n")
    sources = [ROOT / "hdl/core/amiga/ports/fx68k.sv", upstream / "fx68kAlu.sv",
               upstream / "uaddrPla.sv", ROOT / "hdl/core/amiga/multitini_amiga_display_mailbox.sv",
               HERE / "tests/tb_helper.sv", wrappers]
    pinned = sources + [HERE / n for n in ("helper.py", "m68k.py", "test_native.py")]
    pinned += [upstream / n for n in ("microrom.mem", "nanorom.mem")]
    snapshot = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in pinned}
    run("compile", [tools / "xvlog.bat", "--sv", *sources])
    passed = []
    for mode, scenario in cases:
        name = f"m{mode}s{scenario}"
        run(name + "-elaborate", [tools / "xelab.bat", "tb_helper_" + name, "-s", name])
        run(name, [tools / "xsim.bat", name, "-runall"])
        assert "GUEST_HELPER_PASS" in (out / (name + ".log")).read_text()
        passed.append(name)
    run("legacy-elaborate", [tools / "xelab.bat", "tb_helper_legacy", "-s", "legacy"])
    run("legacy", [tools / "xsim.bat", "legacy", "-runall"])
    assert "LEGACY_SHARED_STREAM_FAILURE_REPRODUCED" in (out / "legacy.log").read_text()
    passed.append("legacy-shared-streams")
    assert snapshot == {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in pinned}, "Test source changed during execution"
    record = {"status": "PASS", "cases": passed, "cpu": "real FX68K",
        "libraries": "original Exec/DOS/Intuition/Graphics stubs; no AmigaOS",
        "hunk_sha256": hashlib.sha256(helper["hunk"]).hexdigest(), "board_evidence": False,
        "source_sha256": snapshot, "memory_hex_sha256": hashlib.sha256((out / "memory.hex").read_bytes()).hexdigest(),
        "stream_ownership": {"api": "DOS Execute(-222)", "synchronous": True, "input": 0,
            "fresh_owned_nil_output": True, "closes_output_on_success_and_failure": True,
            "original_use_and_save_retained": True, "caller_streams_never_transferred": True},
        "overscan_geometry": {"all_modes_apply_before_screenmode": True, "display_id": "00029000",
            "maximum_standard": [0, 0, 723, 282], "maximum_text": [724, 283],
            "maximum_view": [93, 29], "standard_view_from_immutable_default": [129, 44],
            "standard_text": [640, 256], "signed_even_x_conversion_and_range_checks": True,
            "roundtrip_cases": ["m0s38", "m1s38", "m3s38"]},
        "physical_clip": {"api": "graphics VideoControl(-708)", "get_tag_rearmed_each_query": True,
            "os_owned_pointer_copied_before_unlock": True, "matches_screen_origin_and_dimensions": True,
            "negative_cases": ["m2s36", "m2s40", "m2s42", "m2s43"],
            "matching_negative_origin_case": "m2s41"},
        "negative_control": {"status": "EXPECTED_FAILURE_REPRODUCED", "case": "legacy-shared-streams",
            "contract": "SYS_Asynch TRUE closes inherited parent streams; second launch rejects closed handles",
            "legacy_probe_sha256": legacy["legacy_probe_sha256"],
            "memory_hex_sha256": hashlib.sha256((out / "legacy-memory.hex").read_bytes()).hexdigest()}}
    (out / "validation.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))


if __name__ == "__main__":
    main()
