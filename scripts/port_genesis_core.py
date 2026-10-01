#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce narrow Genesis strict-tool and clock-ratio helper ports."""

from __future__ import annotations
import argparse
import difflib
import hashlib
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/genesis/upstream/rtl/pad_io.sv"
TARGET = ROOT / "hdl/core/genesis/ports/pad_io.sv"


def generate_pad() -> str:
    text = SOURCE.read_text()
    changes = (
        ("module pad_io\n", "// Multitini port, 2026-09-30: strict variable type, deterministic reset and doubled-clock pad timers.\n"
                              "module pad_io #(parameter integer CLOCK_MULTIPLIER=2)\n"),
        ("wire [5:0] pdata;", ""),
        ("reg   [4:0] mdata;", ""),
        ("assign port_out =", "reg [5:0] pdata;\nreg [4:0] mdata;\nwire [6:0] gdata;\nassign port_out ="),
        ("wire [6:0] gdata =", "assign gdata ="),
        ("reg [10:0] FLTMR;", "reg [$clog2(2048*CLOCK_MULTIPLIER)-1:0] FLTMR;"),
        ("\t\tJCNT <= 3;", "\t\tJCNT <= 3;\n\t\tJTMR <= 0;\n\t\tFLTMR <= 0;\n\t\tTHd <= 1;"),
        ("(210*7)", "(210*7*CLOCK_MULTIPLIER)"),
        ("(11600*7)", "(11600*7*CLOCK_MULTIPLIER)"),
    )
    for before, after in changes:
        if text.count(before) != 1:
            raise ValueError(f"Pinned Genesis pad source changed at {before!r}")
        text = text.replace(before, after)
    return "\n".join(line.rstrip() for line in text.splitlines()) + "\n"


def generate_audio() -> str:
    text = (ROOT / "third_party/genesis/upstream/rtl/audio_cond.sv").read_text()
    text = text.replace("wire [15:0] psg;", "")
    text = text.replace("wire [15:0] psg_amp", "// Multitini port, 2026-09-30: declare the filter result before its instance.\nwire [15:0] psg;\nwire [15:0] psg_amp")
    for instance in ("lpf_left", "lpf_right"):
        before = f"genesis_lpf {instance}"
        if text.count(before) != 1:
            raise ValueError(f"Pinned Genesis audio instance changed: {instance}")
        text = text.replace(before, f"genesis_lpf_late_mux {instance}")
    return "\n".join(line.rstrip() for line in text.splitlines()) + "\n"


def generate_lpf() -> str:
    """Factor mode selection after fixed arithmetic without changing IIR state."""
    pinned = {
        "genesis_lpf.v": "958d16499e7fee982d3b6a286f6cf0bb590e3385159885f743a0092477ea1dd8",
        "audio_iir_filter.v": "29ba71a0f7ec9e77087b7f3eb110b791ca88814f9cda7f8f59ba4bbe84714083",
    }
    for name, expected in pinned.items():
        source = ROOT / "third_party/genesis/upstream/rtl" / name
        if hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            raise ValueError(f"Pinned MIT Genesis audio source changed: {name}")
    original = (ROOT / "third_party/genesis/upstream/rtl/genesis_lpf.v").read_text()
    notice = original.split("*/", 1)[0] + "*/\n"
    return "// SPDX-License-Identifier: MIT\n" + notice + """

// Derived from genesis_lpf.v and audio_iir_filter.v at MiSTer Mega Drive commit
// 083e1b1f4db8ffb32d6e8f83cf822fedc6184a61; reproduced by port_genesis_core.py.
// Shared state, the 504-clock sample interval, reset, truncation and bypass
// match the originals. Keep constant arithmetic before the mode selection
// so a parent-clock mode change does not launch through the DSP cascade.
module genesis_lpf_late_mux (
    input wire clk,
    input wire reset,
    input wire [1:0] lpf_mode,
    input wire signed [15:0] in,
    output wire signed [15:0] out
);
    reg signed [15:0] x0,x1,y0;
    reg [9:0] count;
    (* keep="true", use_dsp="yes" *) wire signed [33:0] acc_model1 =
        (18'sd10528*x0 - 18'sd5264*x1) + 18'sd27504*y0;
    (* keep="true", use_dsp="yes" *) wire signed [33:0] acc_model2 =
        (18'sd12888*x0 - 18'sd6440*x1) + 18'sd26328*y0;
    (* keep="true", use_dsp="yes" *) wire signed [33:0] acc_minimal =
        (18'sd6840*x0 + 18'sd6840*x1) + 18'sd19088*y0;
    (* keep="true" *) wire signed [33:0] acc_bypass = 18'sd32768*y0;
    reg signed [33:0] out32;
    always @* begin
        case (lpf_mode)
            2'b00: out32=acc_model1;
            2'b01: out32=acc_model2;
            2'b10: out32=acc_minimal;
            default: out32=acc_bypass;
        endcase
    end
    assign out=(lpf_mode==2'b11) ? in : y0;
    always @(posedge clk) begin
        if (reset) begin
            count<=0;
            x0<=0;
            x1<=0;
            y0<=0;
        end else begin
            count<=count+1'd1;
            if (count==10'd503) begin
                count<=0;
                y0<={out32[33],out32[29:15]};
                x1<=x0;
                x0<=in;
            end
        end
    end
endmodule
"""


def generate_iir() -> str:
    text = (ROOT / "third_party/genesis/upstream/sys/iir_filter.v").read_text()
    declarations = ("wire [39:0] tap0;", "wire [39:0] tap1;", "wire [39:0] tap2;",
                    "reg        ch = 0;", "reg [15:0] inp, inp_m;")
    for declaration in declarations:
        if text.count(declaration) != 1:
            raise ValueError(f"Pinned IIR declaration changed: {declaration}")
        text = text.replace(declaration, "")
    text = text.replace("wire [59:0] inp_mul", "// Multitini port, 2026-09-30: declarations precede all expressions and instances.\n" +
                        "\n".join(declarations) + "\nwire [59:0] inp_mul")
    text = text.replace("reg  [39:0] x1, y;", "")
    text = text.replace("wire [39:0] x  =", "reg [39:0] x1, y;\nwire [39:0] x  =")
    return "\n".join(line.rstrip() for line in text.splitlines()) + "\n"


def generate_ym_io() -> str:
    text = (ROOT / "third_party/genesis/upstream/rtl/nuked-md/ym3438_io.v").read_text()
    before = "\twire [7:0] debug_data;"
    if text.count(before) != 1:
        raise ValueError("Pinned YM3438 debug declaration changed")
    text = text.replace(before, "")
    text = "// SPDX-License-Identifier: GPL-2.0-or-later\n// Derived from Nuked-MD; exact source and enclosed LICENSE are in third_party/genesis/upstream/rtl/nuked-md.\n" + text
    text = text.replace("\treg [7:0] data_o_r;", "\t// Multitini port, 2026-09-30: declare the debug result before its sequential consumer.\n" +
                        before + "\n\treg [7:0] data_o_r;")
    return "\n".join(line.rstrip() for line in text.splitlines()) + "\n"


def generate_vdp() -> str:
    text = (ROOT / "third_party/genesis/upstream/rtl/nuked-md/ym7101.v").read_text()
    original12 = "\tym7101_dff dff12(.MCLK(MCLK), .clk(w37), .inp(1'h1), .rst(w10), .outp(dff12_l2));"
    original13 = "\tym7101_dff dff13(.MCLK(MCLK), .clk(w34), .inp(w44), .rst(w38), .outp(dff13_l2));"
    original15 = "\tym7101_dff dff15(.MCLK(MCLK), .clk(dff14_l2), .inp(w44), .rst(w31), .outp(dff15_l2));"
    if any(text.count(instance) != 1 for instance in (original12, original13, original15)):
        raise ValueError("Pinned VDP bus handshake latch cluster changed")
    text = text.replace(original12, "").replace(original13, "")
    replacement = """	// Multitini port, 2026-09-30: remove three-cell combinational feedback.
	// Preserve stable latch transitions; resolve upstream pulse races with
	// explicit clear priority. See the exhaustive VDP cluster regression.
	multitini_genesis_vdp_bus_pair bus_pair(
		.MCLK(MCLK), .reset_comb(reset_comb), .dff21(dff21_l2),
		.cpu_as(cpu_as), .w2(w2), .dff14(dff14_l2),
		.w44(w44), .out12(dff12_l2), .out13(dff13_l2), .out15(dff15_l2));"""
    text = text.replace(original15, replacement)
    return "\n".join(line.rstrip() for line in text.splitlines()) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    failed = False
    for target, generate in ((TARGET, generate_pad),
            (TARGET.with_name("audio_cond.sv"), generate_audio),
            (TARGET.with_name("genesis_lpf_late_mux.sv"), generate_lpf),
            (TARGET.with_name("iir_filter.sv"), generate_iir),
            (TARGET.with_name("ym3438_io.v"), generate_ym_io),
            (TARGET.with_name("ym7101.v"), generate_vdp)):
        expected = generate()
        current = target.read_text() if target.exists() else ""
        if current == expected:
            continue
        if args.check:
            sys.stderr.writelines(difflib.unified_diff(current.splitlines(True), expected.splitlines(True),
                                  fromfile=str(target), tofile="generated Genesis helper port"))
            failed = True
        else:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(expected)
            print(f"generated {target.relative_to(ROOT)}")
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
