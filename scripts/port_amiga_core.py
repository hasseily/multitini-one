#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce the narrow ECS-only Minimig ports and portable RAM primitives."""
from __future__ import annotations
import argparse
import csv
import difflib
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/amiga/upstream"
TARGET = ROOT / "hdl/core/amiga/ports"
PIN = "e47c3457b486d09daaf47c8934bda90cf7723045"
NOTICE = "// Multitini ECS port; reproduced by scripts/port_amiga_core.py.\n"


def replace_once(text: str, before: str, after: str) -> str:
    if text.count(before) != 1:
        raise ValueError(f"Pinned Minimig source changed at {before[:90]!r}")
    return text.replace(before, after)


def remove_instance(text: str, kind: str, instance: str, replacement: str) -> str:
    expression = rf"\b{re.escape(kind)}\s*(?:#\s*\([^;]*?\)\s*)?{re.escape(instance)}\s*\(.*?\);"
    result, count = re.subn(expression, replacement, text, count=0, flags=re.S)
    if count != 1:
        raise ValueError(f"Expected one {kind} {instance}, found {count}")
    return result


def minimig() -> str:
    text = (SOURCE / "rtl/minimig.v").read_text()
    text = replace_once(text, "module minimig\n", "module minimig #(parameter AGA_PAL = 1'b0)\n")
    text = replace_once(text, "userio USERIO1", "userio #(.AGA_PAL(AGA_PAL)) USERIO1")
    text = replace_once(text, "input  [15:0] ramdata_in,", """input  [15:0] ramdata2_in,
	output reg [15:0] cpu_data2,
	input  [15:0] ramdata_in,""")
    text = replace_once(text, "//instantiate cpu bridge", """// Latch the second word with the CPU bridge's first word. Subsequent DMA
// and refresh cycles can change the raw RAM response before the CPU uses it.
always @(posedge clk)
	if (!c1 && c3 && cpu_rd && ram_long && !_ram_oe)
		cpu_data2 <= ramdata2_in;

//instantiate cpu bridge""")
    text=replace_once(text,"\toutput  [5:0] ide_req,","\toutput [5:0] ide_monitor,\n\toutput  [5:0] ide_req,")
    text=replace_once(text,"\t.ide_req(ide_req),","\t.monitor(ide_monitor),\n\t.ide_req(ide_req),")
    text = replace_once(text, "\toutput  [1:0] res,", "\toutput  [1:0] res,\n\toutput [8:0] diw_hstart,diw_hstop,\n\toutput [10:0] diw_vstart,diw_vstop,")
    text = replace_once(text, "\t.hde(hde),", "\t.hde(hde),\n\t.diw_hstart(diw_hstart),.diw_hstop(diw_hstop),\n\t.diw_vstart(diw_vstart),.diw_vstop(diw_vstop),")
    text = replace_once(text, ".address(ram_address),", ".address(ram_address[22:1]),")
    text = replace_once(text, "//instantiate sram bridge", "assign ram_address[23]=1'b0;\n\n//instantiate sram bridge")
    text = replace_once(text, ".memory_config(memory_config[3:0]),\n\t.bank(bank)", ".memory_config(memory_config[1:0]),\n\t.bank(bank)")
    text = replace_once(text, ".led(hdd_led)", ".longword(1'b0),\n\t.led(hdd_led)")
    text = remove_instance(text, "toccata", "toccata_board", "assign toccata_out=16'd0;\nassign int6_toccata=1'b0;\nassign toccata_aud_left=16'd0;\nassign toccata_aud_right=16'd0;")
    text = remove_instance(text, "a2065", "a2065_inst", "assign a2065_dout=16'd0;\nassign a2065_nrdy=1'b0;\nassign a2065_int2_sync=1'b0;\nassign a2065_mem_address=29'd0;\nassign a2065_mem_burstcount=8'd0;\nassign a2065_mem_read=1'b0;\nassign a2065_mem_write=1'b0;\nassign a2065_mem_writedata=64'd0;\nassign a2065_mem_byteenable=8'd0;")
    start = text.index("cdtv_bridge cdtv_bridge_inst")
    end = text.index("//-------------------------------------------------------------------------------------", start)
    text = text[:start] + """// CDTV is outside this ECS A600 build and has no emulated card.
assign cdtv_din=16'd0;
assign cdtv_selack=1'b0;
assign cdtv_irq_w=1'b0;
assign cdtv_uio_dout=16'd0;
assign cdtv_req=1'b0;
assign cdtv_dma_req=1'b0;
assign cdtv_dma_we=1'b0;
assign cdtv_dma_baddr=32'd0;
assign cdtv_dma_wbyte=8'd0;
assign cdtv_nvr_dirty=1'b0;
assign cdtv_card_dirty=1'b0;
assign cdtv_cdda_volume=10'd1023;
assign cdtv_cdda_volume_valid=1'b0;
""" + text[end:]
    text = replace_once(text, "reg _rst;", "reg _rst=0;\nreg reset_pipe=0;")
    text = replace_once(text, "\treg r;\n\tr <= ~(cpurst || sys_reset);\n\t_rst <= r;", "\treset_pipe <= ~(cpurst || sys_reset);\n\t_rst <= reset_pipe;")
    text = replace_once(text, "reg [63:0] rtc_reg;", "reg [63:0] rtc_reg=0;")
    text = replace_once(text, """always @(posedge clk) begin
	reg old_flg;
	reg [31:0] cnt;
\t
	old_flg <= rtc[64];
	if(old_flg ^ rtc[64]) begin
		rtc_reg <= {rtc[63:8], 8'd0};
		cnt <= 0;
	end
	else if(cnt < 28375159) cnt <= cnt + 1;
	else begin
		cnt <= 0;
		if(rtc_reg[3:0] < 9) rtc_reg[3:0] <= rtc_reg[3:0] + 1'd1;
		else if(rtc_reg[7:4] < 5) rtc_reg[7:0] <= {rtc_reg[7:4] + 1'd1, 4'b0000};
	end
end""", """// ARM supplies a coherent packed BCD timestamp and toggles bit64. Calendar
// fields stay host-owned; this fallback advances seconds between refreshes.
reg rtc_old_flg=0;
reg [31:0] rtc_count=0;
always @(posedge clk) begin
	rtc_old_flg <= rtc[64];
	if(rtc_old_flg ^ rtc[64]) begin
		rtc_reg <= rtc[63:0];
		rtc_count <= 0;
	end
	else if(rtc_count < 28375159) rtc_count <= rtc_count + 1;
	else begin
		rtc_count <= 0;
		if(rtc_reg[3:0] < 9) rtc_reg[3:0] <= rtc_reg[3:0] + 1'd1;
		else if(rtc_reg[7:4] < 5) rtc_reg[7:0] <= {rtc_reg[7:4] + 1'd1, 4'b0000};
		else rtc_reg[7:0] <= 8'd0; //59 wraps even without a host refresh.
	end
end""")
    return NOTICE + text


def fastchip() -> str:
    """Retain the pinned CPU-side IDE controller without RTG or CD32 devices.

    Both ports use the existing accepted native clock. The upstream IDE
    decode, registered acknowledgement, read wait and Gayle instance are
    copied intact; the board wrapper selects this or the conventional path.
    """
    text = (SOURCE / "rtl/fastchip.v").read_text()
    header = text[:text.index("module fastchip")]
    start = text.index("wire sel_ide   =")
    stop = text.index("wire        sel_rtg =", start)
    interface = """module fastchip (
    input wire clk_sys,reset,sel,
    output wire sel_ack,ready,
    input wire [23:0] addr,input wire [15:0] din,
    output wire [15:0] dout,
    input wire lds,uds,rnw,longword,ide_ena,
    output wire ide_irq,output wire [5:0] ide_req,
    input wire [4:0] ide_address,input wire ide_write,ide_read,
    input wire [15:0] ide_writedata,output wire [15:0] ide_readdata,
    output wire ide_led,output wire [5:0] monitor
);
// IDE-only extraction. No RTG, Akiko, extra CPU clock or memory interface.
assign sel_ack=sel_ide | sel_gayle;
assign ready=ide_ready;
assign dout=ide_dout;
"""
    body=text[start:stop]
    body=replace_once(body,"\t.ide_req(ide_req),","\t.monitor(monitor),\n\t.ide_req(ide_req),")
    return NOTICE + header + interface + body + "\nendmodule\n"


def ide() -> str:
    text=(SOURCE / "rtl/ide.v").read_text()
    text=replace_once(text,"\toutput      [1:0] drive_en,","\toutput [5:0] monitor,\n\toutput      [1:0] drive_en,")
    text=replace_once(text,"endmodule", """// Read-only diagnostics. Retire words on the SAME falling-strobe edge as
// io_cnt, and qualify this pre-edge bundle by the accepted native clock.
reg monitor_old_stb=0,monitor_wide=0,monitor_read=0;
always @(posedge clk) begin
    if(reset)begin monitor_old_stb<=0;monitor_wide<=0;monitor_read<=0;end
    else begin
        monitor_old_stb<=io_stb;
        if(io_stb)begin monitor_wide<=io_32;monitor_read<=read_data_io;end
    end
end
wire monitor_retire=monitor_old_stb && !io_stb && !reset;
assign monitor[0]=!reset && mgmt_write && mgmt_address==5 && mgmt_writedata[11] && (cmd==8'h20 || cmd==8'h21);
assign monitor[2:1]=(monitor_retire && monitor_read) ? (monitor_wide ? 2'd2:2'd1):2'd0;
assign monitor[4:3]=(monitor_retire && !monitor_read) ? (monitor_wide ? 2'd2:2'd1):2'd0;
assign monitor[5]=io_read || io_write;
endmodule""")
    # A 68020 may mix word and longword reads of the FIFO data register.
    # Keep the even bank looking at the next even word when the pointer is
    # odd, so it is already prefetched before a longword transfer begins.
    # The odd bank supplies the current word. No extra CPU acknowledgement
    # delay is needed; ordinary 16-bit reads/writes remain unchanged.
    text=replace_once(text,
        "{buf_q[31:16], (~io_32 & io_cnt[0]) ? buf_q[31:16] : buf_q[15:0]}",
        "((io_32 && io_cnt[0]) ? {buf_q[15:0],buf_q[31:16]} : {buf_q[31:16], (~io_32 & io_cnt[0]) ? buf_q[31:16] : buf_q[15:0]})")
    text=replace_once(text,
        ".address_b(io_cnt[12:1]),\n\t.data_b(io_writedata[15:0]),",
        ".address_b(io_cnt[12:1]+{11'd0,io_cnt[0]}),\n\t.data_b((io_32 && io_cnt[0]) ? io_writedata[31:16] : io_writedata[15:0]),")
    text=replace_once(text,
        ".data_b(io_32 ? io_writedata[31:16] : io_writedata[15:0]),",
        ".data_b((io_32 && !io_cnt[0]) ? io_writedata[31:16] : io_writedata[15:0]),")
    return NOTICE+text


def gayle() -> str:
    text=(SOURCE / "rtl/gayle.v").read_text()
    text=replace_once(text,"\toutput        led\n", "\toutput [5:0] monitor,\n\toutput        led\n")
    text=replace_once(text,"\t.request(ide_req[2:0]),", "\t.monitor(monitor),\n\t.request(ide_req[2:0]),")
    return NOTICE+text


def agnus_window() -> str:
    """Expose the native programmed window, without changing its timing."""
    text = (SOURCE / "rtl/agnus.v").read_text()
    text = replace_once(text, "\toutput        hde,", "\toutput [8:0] diw_hstart,diw_hstop,\n\toutput [10:0] diw_vstart,diw_vstop,\n\toutput        hde,")
    text = replace_once(text, "\t.hde(hde),", "\t.hde(hde),\n\t.diw_hstart(diw_hstart),.diw_hstop(diw_hstop),\n\t.diw_vstart(diw_vstart),.diw_vstop(diw_vstop),")
    return NOTICE + text


def bitplane_window() -> str:
    text = (SOURCE / "rtl/agnus_bitplanedma.v").read_text()
    text = replace_once(text, "  output reg            hde,", "  output wire [8:0] diw_hstart,diw_hstop,\n  output wire [10:0] diw_vstart,diw_vstop,\n  output reg            hde,")
    text = replace_once(text, "endmodule", "// Read-only board capture metadata; native register behavior is unchanged.\nassign diw_hstart=hdiwstrt;\nassign diw_hstop=hdiwstop;\nassign diw_vstart=vdiwstrt;\nassign diw_vstop=vdiwstop;\nendmodule")
    return NOTICE + text


def floppy() -> str:
    text = (SOURCE / "rtl/paula_floppy.v").read_text()
    text = replace_once(text,"\tif(reset)\n  \t\t{disk_fluxdensitymode[3:0], disk_fluxmode[3:0], disk_writable[3:0],disk_present[3:0]} <= 16'b0000_0000_0000_0000;\n  \telse if (rx_data[15:12]==4'b0001 && stb7 && !cmd_cnt)", "\t// Mounted image presence belongs to ARM and survives guest reset.\n  \tif (rx_data[15:12]==4'b0001 && stb7 && !cmd_cnt)")
    text = replace_once(text,"reg   [3:0] disk_present;", "reg   [3:0] disk_present=0;")
    text = replace_once(text,"reg   [3:0] disk_writable;", "reg   [3:0] disk_writable=0;")
    text = replace_once(text,"reg   [3:0] disk_fluxmode;", "reg   [3:0] disk_fluxmode=0;")
    text = replace_once(text,"reg   [3:0] disk_fluxdensitymode;", "reg   [3:0] disk_fluxdensitymode=0;")
    text = remove_instance(text, "MiSTerFloppyVirtualFluxDrive", "virtualFluxDrive", "" ) if "virtualFluxDrive" in text else text
    # Instance names remain checked against the pinned source.
    matches = re.findall(r"\b(MiSTerFloppy\w+)\s*(?:#\s*\([^;]*?\)\s*)?(\w+)\s*\(", text, re.S)
    for kind, name in matches:
        text = remove_instance(text, kind, name, f"// {kind} {name} omitted: ADF MFM images use Paula's native FIFO.")
    text += "\n"
    ties = """
// Dedicated MiSTer physical/flux drive hardware is absent on Multitini.
assign _virtualFluxDataReady=1'b0;
assign _virtReadData=1'b1;
assign virtualFloppyRequestsData=1'b0;
assign virtualFluxDataRead=1'b0;
assign _virtualFluxIndex=1'b1;
assign ext_floppy_rx=16'd0;
assign ext_floppy_wr=1'b0;
assign ext_floppy_rd=1'b0;
assign ext_floppy_rd_del=1'b0;
assign syncWordNOW=1'b0;
assign bitdetected=1'b0;
assign diskByte=8'd0;
assign diskByteReady=1'b0;
assign _dskrd=1'b1;
assign interfaceBusy=1'b0;
assign _dkwd=1'b1;
assign _dkwe=1'b1;
assign _index_ext=1'b1;
assign _track0_ext=1'b1;
assign _wprot_ext=1'b1;
assign _change_ext=1'b1;
assign _ready_ext=1'b1;
assign USER_OUT=7'h7f;
assign mister_floppy_status=3'b000;
"""
    return NOTICE + replace_once(text, "endmodule", ties + "\nendmodule")


def cpu() -> str:
    text = (SOURCE / "rtl/cpu_wrapper.v").read_text()
    text = replace_once(text, "\tinput       [4:0] cpucfg,", "\tinput       [4:0] cpucfg,\n\toutput            cpu_step,")
    text = replace_once(text, "wire clkena_p_throttled = clkena_p_rest | (ramready & clkena_p_thr);", """// PAL 68020 14 MHz mode uses the existing opposite native bus phases.
// These alternate accepted 28 MHz edges and include the chipset-ready phase.
// The wrapper/chip bus stay at 28 MHz and global memory holds freeze both
// phases together. An independent divide-by-two can miss every ready pulse.
wire cpu_speed_ce = !cpucfg[3] || ph1n || ph2n;
wire clkena_p_throttled = (clkena_p_rest | (ramready & clkena_p_thr)) & cpu_speed_ce;
assign cpu_step = clkena_p_throttled;""")
    text = replace_once(text, ".ipl_autovector(1)", ".ipl_autovector(1'b1)")
    for pin in ("HALTn","BERRn","BRn","BGACKn"):
        text = replace_once(text,f".{pin}(1)",f".{pin}(1'b1)")
    text = replace_once(text, "ac_toccata  <= cdtv_mode ? 1'b0 : 1'b1;", "ac_toccata  <= 1'b0; // no expansion audio card")
    text = replace_once(text, "ac_a2065    <= 1;", "ac_a2065    <= 1'b0; // no Ethernet card")
    text = replace_once(text, "ac_cdtv     <= cdtv_mode;", "ac_cdtv     <= 1'b0;")
    text = replace_once(text, "assign toccata_ena = ~ac_toccata & ~cdtv_mode;", "assign toccata_ena = 1'b0;")
    text = replace_once(text, "assign a2065_ena   = ~ac_a2065;", "assign a2065_ena   = 1'b0;")
    return NOTICE + text


def userio() -> str:
    text = (SOURCE / "rtl/userio.v").read_text()
    text = replace_once(text, "module userio\n", "module userio #(parameter AGA_PAL = 1'b0)\n")
    text = replace_once(text, "reg [7:0] t_memory_config = 8'b0_0_00_01_01;", "reg [7:0] t_memory_config = 8'h33; // 2MiB Chip + 8MiB Zorro-II Fast")
    text = replace_once(text, "reg [5:0] t_chipset_config = 0;", "reg [5:0] t_chipset_config = 6'h08; // ECS PAL")
    text = replace_once(text, "chipset_config <= t_chipset_config;", "chipset_config <= AGA_PAL ? 6'h18 : 6'h08;")
    text = replace_once(text, "cpu_cfg <= {t_cpu_config[7:6], t_cpu_config[5], t_cpu_config[1:0]};", """// A1200 stock uses the upstream 68EC020 bus throttle and 24-bit addressing.
		if (AGA_PAL)
			cpu_cfg <= t_cpu_config[1:0]==2'b11 && !t_cpu_config[5] ? 5'h03 : 5'h07;
		else
			cpu_cfg <= {1'b0, (t_cpu_config[1:0]==2'b11) & t_cpu_config[6], (t_cpu_config[1:0]==2'b11) & t_cpu_config[5], {2{t_cpu_config[1:0]==2'b11}}}; // bit3 selects alternate-edge 68020 execution""")
    text = replace_once(text, "memory_config[5:0] <= t_memory_config[5:0];", "memory_config[5:0] <= {(t_memory_config[5:4]==2'd2 ? 2'd2 : 2'd3), 2'b00, 2'b11}; // 2MiB Chip; 4/8MiB Fast")
    text = replace_once(text, "memory_config[7] <= t_memory_config[7];", "memory_config[7] <= 1'b0;")
    text = replace_once(text, "memory_config[6] <= t_memory_config[6];", "memory_config[6] <= 1'b0; // no monitor cartridge")
    text = replace_once(text, "\treg       bootrom_r;", "\treg       bootrom_r=0;")
    text = replace_once(text,"\treg [5:0] ide_cfg = 0;", "")
    text = replace_once(text,"\treg [4:0] cpu_cfg = 0;", "")
    text = replace_once(text,"// configuration changes only while reset is active", "reg [5:0] ide_cfg=0;\nreg [4:0] cpu_cfg=AGA_PAL ? 5'h07 : 5'd0;\n// configuration changes only while reset is active")
    text = replace_once(text, "reg [7:0] cmd;", """reg [7:0] cmd;
initial begin
    memory_config=8'h33; chipset_config=AGA_PAL ? 6'h18 : 6'h08; floppy_config=5'h04;
    floppy_ext_drive=12'd0; user_port_mode=1'b0;
    ide_config=6'd0; cpu_config=AGA_PAL ? 5'h07 : 5'd0; cache_config=3'd0;
    usrrst=1'b0; cpurst=1'b0; cpuhlt=1'b0;
    aud_mix=2'd0; scanline=3'd0; ar=2'd0; blver=2'd0;
    IO_WAIT=1'b0; host_cs=1'b0; host_we=1'b0;
end
""")
    return NOTICE + text


def cpu_cycles() -> str:
    text = (SOURCE / "rtl/cpu_cycles.v").read_text()
    for rom, width, depth in (("cyc", 6, 16384), ("ea", 5, 2048)):
        before = re.search(rf'\(\* ram_init_file = "rtl/tg68k/m68k_{rom}_idx.mif"\s*\*\) reg \[{width-1}:0\] {rom}_rom\[{depth}\];', text)
        if before is None:
            raise ValueError(f"Missing {rom} timing ROM")
        text = text.replace(before.group(), f'reg [{width-1}:0] {rom}_rom[0:{depth-1}];\ninitial $readmemh("m68k_{rom}_idx.hex", {rom}_rom);')
    return NOTICE + text


def fx68k() -> str:
    text = (SOURCE / "rtl/fx68k/fx68k.sv").read_text()
    return NOTICE + text


def mif_hex(name: str, count: int, width: int) -> str:
    text = (SOURCE / f"rtl/tg68k/{name}.mif").read_text()
    words = [None] * count
    for lo, hi, single, value in re.findall(r"(?:\[([0-9A-F]+)\.\.([0-9A-F]+)\]|([0-9A-F]+))\s*:\s*([0-9A-F]+)\s*;", text, re.I):
        start, end = (int(lo,16),int(hi,16)) if lo else (int(single,16),int(single,16))
        for i in range(start,end+1):
            if i >= count or words[i] is not None or int(value,16) >= (1 << width):
                raise ValueError(f"Invalid MIF word {name}:{i}")
            words[i] = int(value,16)
    if any(x is None for x in words):
        raise ValueError(f"Incomplete MIF {name}")
    return "".join(f"{value:02x}\n" for value in words)


RAM = """// SPDX-License-Identifier: GPL-3.0-only
// Original Multitini portable implementations of the documented RAM interfaces.
// No Intel megafunction source is included. Single-clock RAM, write-first on
// the writing port; mixed-port same-address reads return old data.
module dpram #(parameter addr_width=8, data_width=8, mem_init_file=" ") (
 input wire clock,
 input wire [addr_width-1:0] address_a,
 input wire [data_width-1:0] data_a=0,
 input wire enable_a=1'b1, wren_a=1'b0,
 output wire [data_width-1:0] q_a,
 input wire cs_a=1'b1,
 input wire [addr_width-1:0] address_b=0,
 input wire [data_width-1:0] data_b=0,
 input wire enable_b=1'b1, wren_b=1'b0,
 output wire [data_width-1:0] q_b,
 input wire cs_b=1'b1
);
 (* ram_style="block" *) reg [data_width-1:0] mem[0:(1<<addr_width)-1];
 reg [data_width-1:0] qa,qb;
 integer i;
 initial begin
  for(i=0;i<(1<<addr_width);i=i+1) mem[i]=0;
  qa=0; qb=0;
  if(mem_init_file!=" " && mem_init_file!="") $readmemh(mem_init_file,mem);
 end
 always @(posedge clock) if(enable_a) begin
  qa<=mem[address_a];
  if(wren_a && cs_a) begin mem[address_a]<=data_a; qa<=data_a; end
 end
 always @(posedge clock) if(enable_b) begin
  qb<=mem[address_b];
  if(wren_b && cs_b) begin mem[address_b]<=data_b; qb<=data_b; end
 end
 assign q_a=cs_a ? qa : {data_width{1'b1}};
 assign q_b=cs_b ? qb : {data_width{1'b1}};
endmodule

module denise_colortable_ram_mf (
 input wire [3:0] byteena_a,
 input wire clock,
 input wire [31:0] data,
 input wire enable,
 input wire [7:0] rdaddress,wraddress,
 input wire wren,
 output reg [31:0] q
);
 (* ram_style="block" *) reg [31:0] mem[0:255];
 integer i,b;
 initial begin for(i=0;i<256;i=i+1) mem[i]=0; q=0; end
 always @(posedge clock) if(enable) begin
  q<=mem[rdaddress];
  for(b=0;b<4;b=b+1) if(wren && byteena_a[b]) mem[wraddress][8*b+:8]<=data[8*b+:8];
 end
endmodule
"""


def hoist_module_declarations(text: str) -> str:
    """Move only module-scope net/register declarations before their consumers.

    Quartus accepts implicit nets subsequently redeclared with a wider type;
    strict SystemVerilog tools reject that ordering. Procedural and generate
    scopes are untouched. Initializers and RAM attributes stay attached.
    """
    def blank(match: re.Match) -> str:
        return re.sub(r"[^\n]", " ", match.group())
    clean=re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"',blank,text,flags=re.S)
    edits=[]
    for match in re.finditer(r"\bmodule\b[^;]*;",clean):
        body=match.end()
        end=clean.index("endmodule",body)
        depth=0
        candidates=[]
        tokens=list(re.finditer(r"\bbegin\b|\bend\b|\bfunction\b|\bendfunction\b|\btask\b|\bendtask\b|(?m:^[ \t]*(?:\(\*[^;]*?\*\)\s*)?(?:wire|reg|integer|genvar|localparam|parameter)\b[^;]*;)",clean[body:end]))
        for token in tokens:
            value=token.group().strip()
            if value in ("begin","function","task"): depth+=1
            elif value in ("end","endfunction","endtask"): depth-=1
            elif depth==0:
                start,stop=body+token.start(),body+token.end()
                candidates.append(text[start:stop])
                edits.append((start,stop,""))
        if candidates:
            params=[];decls=[];assigns=[]
            for declaration in candidates:
                if declaration.strip().startswith(("localparam ","parameter ")):
                    params.append(declaration)
                elif declaration.strip().startswith("wire") and "=" in declaration:
                    left,right=declaration.rsplit("=",1) if declaration.count("=")==1 else declaration.split("=",1)
                    # The first '=' is the net initializer, subsequent ones can
                    # occur in comparisons in its right-hand expression.
                    left,right=declaration.split("=",1)
                    name=re.search(r"([A-Za-z_$][\w$]*)\s*$",left)
                    if name is None: raise ValueError(f"Unsupported net initializer: {declaration}")
                    decls.append(left.rstrip()+";")
                    assigns.append(f"assign {name.group(1)} ="+right)
                else: decls.append(declaration)
            edits.append((body,body,"\n// Strict-tool declaration order; no functional change.\n"+"\n".join(params+decls+assigns)+"\n"))
    for start,stop,replacement in sorted(edits,reverse=True):
        text=text[:start]+replacement+text[stop:]
    return text


def m68k_bridge() -> str:
    text=(SOURCE / "rtl/minimig_m68k_bridge.v").read_text()
    return replace_once(text,"\t\tif (_as && cpu_halt) halt <= #1 1'b1;\n"
        "\t\telse if (_as && !cpu_halt) halt <= #1 1'b0;",
        "\t\t// The CPU can start a cycle while the host owns the bus.\n"
        "\t\t// Release on host idle so the waiting CPU cycle can get DTACK.\n"
        "\t\tif (halt) begin\n"
        "\t\t\tif (!cpu_halt && !host_cs) halt <= #1 1'b0;\n"
        "\t\tend else if (_as && cpu_halt) halt <= #1 1'b1;")


def generated() -> dict[str, str]:
    result={"minimig.v": minimig(), "fastchip.v": fastchip(), "ide.v": ide(), "gayle.v": gayle(), "paula_floppy.v": floppy(), "cpu_wrapper.v": cpu(),
            "agnus.v": agnus_window(), "agnus_bitplanedma.v": bitplane_window(),
            "userio.v": userio(), "cpu_cycles.v": cpu_cycles(), "fx68k.sv": fx68k(),
            "minimig_m68k_bridge.v": m68k_bridge(),
            "amiga_ram.sv": RAM, "m68k_cyc_idx.hex": mif_hex("m68k_cyc_idx",16384,6),
            "m68k_ea_idx.hex": mif_hex("m68k_ea_idx",2048,5)}
    for path in sorted((SOURCE / "rtl").glob("*.v")):
        original=result.get(path.name,path.read_text())
        portable=hoist_module_declarations(original)
        if portable!=original or path.name in result:
            result[path.name]=portable if original.startswith(NOTICE) else NOTICE+portable
    ddr=(ROOT / "hdl/core/apple_iigs/multitini_apple_iigs_ddr_path.sv").read_text()
    result["multitini_amiga_ddr_path.sv"]=replace_once(ddr,"module multitini_apple_iigs_ddr_path",NOTICE+"// Shared original Multitini DDR path; only the module name differs.\nmodule multitini_amiga_ddr_path")
    return result


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check",action="store_true")
    parser.add_argument("--no-licenses",action="store_true",help="Leave the shared license list to its owner")
    args=parser.parse_args()
    manifest=json.loads((ROOT / "provenance/amiga.import.json").read_text())
    for entry in manifest["files"]:
        if hashlib.sha256((SOURCE / entry["path"]).read_bytes()).hexdigest()!=entry["sha256"]:
            raise SystemExit(f"Pinned Amiga source changed: {entry['path']}")
    failed=False
    for name, expected in generated().items():
        target=TARGET / name
        current=target.read_text() if target.exists() else ""
        if current==expected: continue
        if args.check:
            print("".join(difflib.unified_diff(current.splitlines(True),expected.splitlines(True),fromfile=str(target),tofile="generated Amiga port")))
            failed=True
        else:
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_text(expected)
    if not args.check and not args.no_licenses:
        csv_path=ROOT / "licenses/files.csv"
        with csv_path.open(newline="",encoding="utf-8") as stream: rows=list(csv.reader(stream))
        rows=[r for r in rows if not r[0].startswith("hdl/core/amiga/ports/") and r[0] not in ("scripts/import_amiga_core.py","scripts/port_amiga_core.py")]
        for name in generated():
            original=name in ("amiga_ram.sv","multitini_amiga_ddr_path.sv")
            rows.append([f"hdl/core/amiga/ports/{name}","Multitini One" if original else "MiSTer Minimig and Multitini One","" if original else PIN,"GPL-3.0-only" if original else "GPL-3.0-or-later","source-reviewed","Reproduced by scripts/port_amiga_core.py; ECS A600 only"])
        for name in ("import_amiga_core.py","port_amiga_core.py"):
            rows.append([f"scripts/{name}","Multitini One","","GPL-3.0-only","source-reviewed","Original deterministic Minimig source import and port tool"])
        own=["hdl/core/amiga/multitini_amiga_machine.sv","hdl/board/multitini_amiga_timing.xdc",
             "hdl/sim/tb_amiga_ports.sv","scripts/test_amiga_ports.py"]
        own += [p.relative_to(ROOT).as_posix() for p in sorted((ROOT / "hdl/core/amiga").glob("*.sv"))]
        own += [p.relative_to(ROOT).as_posix() for p in sorted((ROOT / "hdl/board").glob("multitini_amiga_*.sv"))]
        own += [p.relative_to(ROOT).as_posix() for p in sorted((ROOT / "hdl/sim").glob("tb_amiga*.sv"))]
        own += [p.relative_to(ROOT).as_posix() for p in sorted((ROOT / "scripts").glob("test_amiga*.py"))]
        own=sorted(set(own))
        rows=[r for r in rows if r[0] not in own]
        for name in own:
            derived=name=="hdl/core/amiga/multitini_amiga_machine.sv"
            rows.append([name,"MiSTer Minimig and Multitini One" if derived else "Multitini One",PIN if derived else "","GPL-3.0-only","source-reviewed",
                         "Original integration; CPU phase relation derived from licensed upstream Minimig.sv; exact reference/notices retained" if derived else
                         "Original ECS Amiga integration; host/simulation evidence remains separate from board acceptance"])
        with csv_path.open("w",newline="",encoding="utf-8") as stream: csv.writer(stream,lineterminator="\n").writerows(rows)
    print("Amiga ports differ" if failed else "Amiga ports verified")
    return int(failed)


if __name__=="__main__":
    raise SystemExit(main())
