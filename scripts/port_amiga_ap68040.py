#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce AP68040's Xilinx RAM templates and Amiga cache policy."""
import argparse
from pathlib import Path
import hashlib
import json
import re

from port_amiga_core import replace_once

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/amiga/ap68040/rtl"
TARGET = ROOT / "hdl/core/amiga/ap68040"


def ram(text):
    text = text.replace('ramstyle = "M10K, no_rw_check"', 'ram_style = "block"')
    text = text.replace('ramstyle = "MLAB, no_rw_check"', 'ram_style = "distributed"')
    text = replace_once(text, "genvar gs, gb;", "genvar gs;")
    text = replace_once(text, 'logic [SL-1:0][7:0] mem', 'logic [SL*8-1:0] mem')
    text = replace_once(text, """			for (gb = 0; gb < SL; gb++) begin : g_b
				always_ff @(posedge clk)
					if (we && wbe[gs*SL + gb]) mem[waddr][gb] <= wdata[(gs*SL + gb)*8 +: 8];
			end
			always_ff @(posedge clk) qr[gs*SL*8 +: SL*8] <= mem[raddr];""", """			always @(posedge clk) begin
				for (int byte_lane = 0; byte_lane < SL; byte_lane++)
					if (we && wbe[gs*SL + byte_lane])
						mem[waddr][8*byte_lane +: 8] <= wdata[8*(gs*SL + byte_lane) +: 8];
				qr[gs*SL*8 +: SL*8] <= mem[raddr];
			end""")
    text = replace_once(text, """	always_ff @(posedge clk) begin
		if (we_a) mem[addr_a] <= wd_a;
		if (we_b) mem[addr_b] <= wd_b;
		qa_r <= mem[addr_a];
		qb_r <= mem[addr_b];
		ra   <= addr_a;""", """	// Suppress A's physical write on a collision. The registered bypass
	// below still forwards B's value to both ports on that cycle.
	always @(posedge clk) begin
		if (we_a && !(we_b && addr_a == addr_b)) mem[addr_a] <= wd_a;
		qa_r <= mem[addr_a];
	end
	always @(posedge clk) begin
		if (we_b) mem[addr_b] <= wd_b;
		qb_r <= mem[addr_b];
	end
	always_ff @(posedge clk) begin
		ra   <= addr_a;""")
    return text


def amiga_cache_ports(name, text):
    # Chip RAM is shared with DMA. Cache only the configured Zorro II RAM
    # aperture, after address translation and before selecting a bus size.
    text = replace_once(text, "\tinput  logic        clk,",
                        "\tinput  logic [7:0]  amiga_fast_top,\n\tinput  logic        clk,")
    if name == "ap68040.sv":
        for instance in ("dmu", "fetch"):
            text = replace_once(text, f" {instance} (",
                                f" {instance} (\n\t.amiga_fast_top(amiga_fast_top),")
    elif name == "ap68040_dmu.sv":
        text = replace_once(text, "\ttg = x.pa[31:10];", """	if (x.pa[31:24] != 0 || x.pa[23:16] < 8'h20 || x.pa[23:16] >= amiga_fast_top)
		x.cm = 2'b11;
	tg = x.pa[31:10];""")
    else:
        text = replace_once(text, "\tx_cach = x_ok && ic_en && !x_cm[1];",
                            """	if (x_pa[31:24] != 0 || x_pa[23:16] < 8'h20 || x_pa[23:16] >= amiga_fast_top)
		x_cm = 2'b11;
	x_cach = x_ok && ic_en && !x_cm[1];""")
    return text


def declare_before(text, declarations, anchor):
    for declaration in declarations:
        text = replace_once(text, declaration, "")
    return replace_once(text, anchor, "".join(declarations) + anchor)


def vivado_net_binding(name, text):
    # A reference from an instance can create an implicit wire instead of
    # finding the later variable. In a generate scope Vivado then drops the
    # affected cache/ATC ways. Declare the connections before their instances.
    if name == "ap68040.sv":
        instances = re.findall(r"^ap68040_\w+.*?\n\);", text, re.M | re.S)
        if len(instances) != 7:
            raise ValueError("Unexpected AP68040 top-level instance list")
        for instance in instances:
            text = replace_once(text, instance, "")
        return replace_once(text, "\nendmodule", "\n" + "\n".join(instances) + "\nendmodule")
    if name == "ap68040_atc.sv":
        return declare_before(text, ("logic [1:0] wway;\n",), "genvar w;")
    if name == "ap68040_fetch.sv":
        return declare_before(text, ("logic [31:0] m_pa;\n", "logic  [1:0] m_way;\n"), "genvar gw;")
    if name == "ap68040_decode.sv":
        return declare_before(text, ("logic        bw_we;\n", "logic  [7:0] bw_wa;\n",
                                     "logic  [1:0] bw_wd;\n"), "logic  [1:0] bht_q;")
    if name == "ap68040_fpif.sv":
        return declare_before(text, ("logic [2:0] n_srcr, n_dstr, n_class, n_fmt;\n",
                                     "logic [6:0] n_opm;\n"), "ap040_fpu #")
    if name == "ap68040_backend.sv":
        return declare_before(text, ("logic [31:0] ex_latch;\n",),
                              "logic [31:0] ex_a,  ex_b,  ex_ea,  ex_ld;")
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    manifest = json.loads((ROOT / "provenance/ap68040.import.json").read_text(encoding="utf-8"))
    for name, digest in manifest["files"].items():
        if hashlib.sha256((SOURCE.parent / name).read_bytes()).hexdigest() != digest:
            raise SystemExit(f"Pinned AP68040 source changed: {name}")
    if not args.check:
        TARGET.mkdir(parents=True, exist_ok=True)
    for name in ("ap68040.sv", "ap68040_atc.sv", "ap68040_backend.sv",
                 "ap68040_dmu.sv", "ap68040_fetch.sv", "ap68040_fpif.sv",
                 "ap68040_ram.sv", "ap68040_decode.sv", "ap68040_useq.sv"):
        text = (SOURCE / name).read_text(encoding="utf-8")
        if name in ("ap68040.sv", "ap68040_dmu.sv", "ap68040_fetch.sv"):
            text = amiga_cache_ports(name, text)
        if name == "ap68040_ram.sv":
            text = ram(text)
        text = vivado_net_binding(name, text)
        text = text.replace('romstyle = "M10K"', 'rom_style = "block"')
        text = "// Reproduced by scripts/port_amiga_ap68040.py.\n" + text
        output = TARGET / name
        if args.check:
            if output.read_text(encoding="utf-8") != text:
                raise SystemExit(f"AP68040 port differs from its pinned source: {name}")
        else:
            output.write_text(text, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
