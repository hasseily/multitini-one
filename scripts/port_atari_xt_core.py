#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Import the pinned Atari-XT standalone HDL subset and adapt its machine top."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "third_party/atari-xt/upstream"
MANIFEST = ROOT / "provenance/atari-xt.import.json"
PORT = ROOT / "hdl/core/atari_xt/ports/a8_core.sv"
URL = "https://github.com/ThrudTheBarbarian/fpga-xt"
PIN = "a4340eb034503c6189197480bccc55564e7708a5"
LICENSE = "LicenseRef-CERN-OHL-S-2.0-with-attribution"

HDL = (
    "antic_gtia.sv", "antic_scanline.sv", "antic_beam.sv", "antic_dl.sv", "antic_pf_geom.sv",
    "antic_pf_fetch.sv", "antic_line_render.sv", "antic_mode_tbl.sv", "antic_pixel_shift.sv",
    "antic_pf_source.sv", "antic_color_sel.sv", "antic_char_ctl.sv", "antic_pm_fetch.sv",
    "antic_nmi.sv", "antic_dma_sched.sv", "antic_reg_file.sv", "gtia_reg_file.sv", "gtia_stage.sv",
    "gtia_obj_walk.sv", "gtia_priority.sv", "gtia_collide.sv", "gtia_special.sv", "antic2.sv",
    "antic2_seq.sv", "antic2_regs.sv", "antic2_line.sv", "antic2_dl.sv", "antic_pf_stream.sv",
    "antic_pm_dma.sv", "antic_emit_win.sv", "a2_video.sv", "pia_regs.sv", "pokey.sv",
    "pokey_regs.sv", "pokey_audio.sv", "pokey_pot.sv", "pokey_serial.sv", "xt6502f/xt6502f.sv",
    "palette_lut.sv", "palette/atari_ntsc.hex",
)
FILES = ("LICENSE", "README.md", "hdl/a8_core.sv", *(f"hdl/{file}" for file in HDL))


def read_pinned(checkout: Path, path: str) -> bytes:
    entry = subprocess.check_output(
        ["git", "-C", str(checkout), "ls-tree", PIN, "--", path], text=True
    ).split()
    if len(entry) != 4 or entry[1] != "blob":
        raise SystemExit(f"Pinned Atari-XT source is not a blob: {path}")
    data = subprocess.check_output(["git", "-C", str(checkout), "cat-file", "blob", entry[2]])
    blob = hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest()
    if entry[2] != blob:
        raise SystemExit(f"Pinned Atari-XT source mismatch for {path}")
    return data


def port_machine(data: bytes) -> bytes:
    source = data.decode().replace("\r\n", "\n")
    substitutions = (
        ("module a8_core #(", "module multitini_atari_xt_machine #("),
        ("    input  wire        cold,", "    input  wire        cold,\n    input  wire        hold_cpu,"),
        ("    input  wire [7:0]  antic_rdata,", """    input  wire [7:0]  antic_rdata,
    output wire        antic_rom_hit,
    output wire [14:0] antic_rom_addr,"""),
        ("    input  wire [7:0]  consol_keys,", """    input  wire [7:0]  consol_keys,
    input  wire [7:0]  joy_porta,
    input  wire        kbd_event_valid, kbd_release, break_key_pulse,
    input  wire [7:0]  kbd_event_code,
    output wire [3:0]  audio_ch1, audio_ch2, audio_ch3, audio_ch4,"""),
        ("    xt6502f u_cpu (", """    xt6502f #(.CLK_SALLY_HZ(CLK_HZ), .PHI2_HZ(PHI2_HZ)) u_cpu ("""),
        (".dbg_load(1'b0), .dbg_pc_in(16'h0000)", """.dbg_load(1'b0), .dbg_pc_in(16'h0000),
        .dbg_a_in(8'h00), .dbg_x_in(8'h00), .dbg_y_in(8'h00), .dbg_s_in(8'h00), .dbg_p_in(8'h00),
        .dbg_cyc_addr(), .dbg_cyc_val(), .dbg_cyc_rw(), .dbg_cyc_valid()"""),
        (".joy_porta_in(8'hFF),", ".joy_porta_in(joy_porta),"),
        (".kbd_event_valid(1'b0), .kbd_event_code(8'h00), .kbd_release(1'b0),",
         ".kbd_event_valid(kbd_event_valid), .kbd_event_code(kbd_event_code), .kbd_release(kbd_release),"),
        (".ch1_out(), .ch2_out(), .ch3_out(), .ch4_out(),",
         ".ch1_out(audio_ch1), .ch2_out(audio_ch2), .ch3_out(audio_ch3), .ch4_out(audio_ch4),"),
        (".break_key_pulse(1'b0),", ".break_key_pulse(break_key_pulse),"),
        ("    wire pokey_re = c_rw && cs_pokey", "    wire pokey_re = c_rdy && c_rw && cs_pokey"),
        ("assign c_rdy = halt_n && wsync_ok;", "assign c_rdy = !hold_cpu && halt_n && wsync_ok;"),
    )
    for original, replacement in substitutions:
        if source.count(original) != 1:
            raise SystemExit(f"Could not apply Atari-XT port change for {original!r}")
        source = source.replace(original, replacement)
    rom_start = source.index("    // The harness boots OS-LESS:")
    rom_end = source.index("    wire [7:0] reg_rdata;", rom_start)
    source = source[:rom_start] + """    // External, preloaded XL ROMs are visible to both masters from reset.
    // ANTIC needs the OS character generator when CHBASE points into ROM.
    wire [7:0] portb_bank;
    function automatic [15:0] xl_rom(input [15:0] addr);
        if (portb_bank[0] && addr[15:14] == 2'b11 && addr[15:11] != 5'b11010)
            xl_rom = {1'b1, 1'b0, addr[13:0]};
        else if (!portb_bank[1] && addr[15:13] == 3'b101)
            xl_rom = {1'b1, 2'b10, addr[12:0]};
        else if (portb_bank[0] && !portb_bank[7] && addr[15:11] == 5'b01010)
            xl_rom = {1'b1, 4'b0010, addr[10:0]};
        else xl_rom = 16'd0;
    endfunction
    assign {rom_hit, rom_addr} = xl_rom(c_addr);
    assign {antic_rom_hit, antic_rom_addr} = xl_rom(antic_addr);
""" + source[rom_end:]
    source = source.replace(
        "    // Everything POKEY needs from outside this core -- keyboard, pots, serial --\n"
        "    // is tied off. The ACID tests exercise the timers, IRQs, RANDOM and SKSTAT,\n"
        "    // none of which depend on those.",
        "    // The wrapper supplies keyboard events and samples the four audio channels."
    ).replace(
        "    // open bus $FF -- which is exactly what pia_basic (wants $00) and pia_irq\n"
        "    // (wants $3F) were reporting.  The joystick pins are unwired here and read\n"
        "    // as all-released, per the module's own default.",
        "    // open bus $FF -- which is exactly what pia_basic (wants $00) and pia_irq\n"
        "    // (wants $3F) were reporting.  PORTA also sees the controller directions."
    )
    header = (f"// SPDX-License-Identifier: {LICENSE}\n"
              "// Adapted from Atari-XT by ThrudTheBarbarian.\n"
              f"// Source Location: {URL}/blob/{PIN}/hdl/a8_core.sv\n"
              "// See third_party/atari-xt/upstream/LICENSE.\n"
              "// Generated by scripts/port_atari_xt_core.py.\n")
    return (header + source).encode()


def write_or_check(path: Path, data: bytes, check: bool) -> None:
    if check:
        if not path.is_file() or path.read_bytes() != data:
            raise SystemExit(f"Atari-XT port mismatch: {path.relative_to(ROOT)}")
    else:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--import-checkout", type=Path)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    records = []
    for path in FILES:
        target = UPSTREAM / path
        if args.import_checkout:
            data = read_pinned(args.import_checkout, path)
            write_or_check(target, data, args.check)
        else:
            data = target.read_bytes()
        records.append({"path": path, "sha256": hashlib.sha256(data).hexdigest(), "license": LICENSE})
    manifest = {
        "schema_version": 1, "source": URL, "commit": PIN, "license": LICENSE,
        "attribution": "This private prototype incorporates and adapts Atari-XT by ThrudTheBarbarian.",
        "purpose": "SALLY fidelity CPU, ANTIC2/GTIA, PIA, POKEY and NTSC palette for 64K XL mode.",
        "exclusions": ["Atari-XT platform and OS", "system ROMs", "media", "binary FPGA images"],
        "modifications": ["Wire the same subcycle parameters to CPU and chipset.",
                          "Map externally loaded XL ROMs at reset and on ANTIC reads.",
                          "Expose PORTA directions, POKEY keyboard and PCM channel levels."],
        "files": records,
    }
    write_or_check(MANIFEST, (json.dumps(manifest, indent=2) + "\n").encode(),
                   args.check or args.import_checkout is None)
    write_or_check(PORT, port_machine((UPSTREAM / "hdl/a8_core.sv").read_bytes()), args.check)


if __name__ == "__main__":
    main()
