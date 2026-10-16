#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce narrow Xilinx/runtime-ROM ports of the pinned MiSTer Apple IIgs."""
from __future__ import annotations
import argparse
import difflib
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
UPSTREAM = ROOT / "third_party/apple-iigs/upstream"
PORTS = ROOT / "hdl/core/apple_iigs/ports"
NOTICE = "// Multitini port, 2026-10-14: reproduced by scripts/port_apple_iigs_core.py.\n"

def upstream(relative: str) -> str:
    return (UPSTREAM / relative).read_text(encoding="utf-8")

def replace(text: str, before: str, after: str, count: int = 1) -> str:
    if text.count(before) != count:
        raise ValueError(f"Pinned Apple IIgs source changed at {before!r}: {text.count(before)} occurrences")
    return text.replace(before, after)

def normalize(text: str) -> str:
    return "\n".join(line.rstrip() for line in text.splitlines()) + "\n"

def rom_inputs(address_width: int = 12) -> str:
    return f"input load_rom_clk,\ninput load_rom_we,\ninput [{address_width-1}:0] load_rom_addr,\ninput [7:0] load_rom_data,\n"

def generate_iigs() -> str:
    text = upstream("rtl/iigs.sv")
    text = replace(text, "module iigs\n", NOTICE + "module iigs\n")
    text = replace(text, "   input              reset,", "   // Runtime auxiliary ROM upload, only while the machine is held in reset.\n" +
                   "   // 0000=CHAR,1000=ADB3,2000=HDD,3000=ADB1; each window is 4KiB.\n" +
                   rom_inputs(16) + "output cpu_valid,\noutput [9:0] video_x,\noutput [8:0] video_y,\noutput video_pal,\n" +
                   "   input              reset,")
    # Keep the external raw bus cadence visible to the memory adapter. Every
    # internal bus side effect and CPU CE uses the completed cadence instead.
    text = re.sub(r"(?<!\.)\bphi2\b", "bus_phi2", text)
    text = replace(text, "  output bus_phi2,", "  output phi2,")
    text = replace(text, "assign bus_phi2 = ph2_en;", "assign phi2 = ph2_en;\nwire bus_phi2 = ph2_en & (~mem_stall | reset);\n" +
                   "assign cpu_valid = cpu_vpa | cpu_vda | hdd_dma;\nassign video_x = H;\nassign video_y = V;\nassign video_pal = C02BVAL[4];")
    text = replace(text, "`ifdef VERILATOR\ndpram #(.widthad_a(17),.prefix(\"slow\"),.p(\" e\")) slowram\n`else\nbram #(.widthad_a(17)) slowram\n`endif", "apple_iigs_slowram slowram")
    text = replace(text, "        .wren_a(slowram_we),", "        .wren_a(slowram_we & bus_phi2),")
    text = replace(text, "  wire adb_strobe_mux = adb_read | adb_strobe;", "  wire adb_strobe_mux = (adb_read | adb_strobe) & ~mem_stall;")
    text = replace(text, "`ifdef VERILATOR\n        .ce_a(slowram_ce),\n`else\n        .enable_a(slowram_ce),\n`endif", "        .enable_a(slowram_ce),")
    text = replace(text, "vgc vgc(\n", "vgc vgc(\n        .load_rom_clk(load_rom_clk),\n        .load_rom_we(load_rom_we & (load_rom_addr[15:12]==4'h0)),\n        .load_rom_addr(load_rom_addr[11:0]),\n        .load_rom_data(load_rom_data),\n")
    text = replace(text, "  adb adb(\n", "  adb adb(\n          .load_rom_clk(load_rom_clk),\n          .load_rom_we(load_rom_we & ((load_rom_addr[15:12]==4'h1) | (load_rom_addr[15:12]==4'h3))),\n          .load_rom_bank(load_rom_addr[13]),\n          .load_rom_addr(load_rom_addr[11:0]),\n          .load_rom_data(load_rom_data),\n")
    text = replace(text, "    hdd hdd(\n", "    hdd hdd(\n        .load_rom_clk(load_rom_clk),\n        .load_rom_we(load_rom_we & (load_rom_addr[15:12]==4'h2)),\n        .load_rom_addr(load_rom_addr[11:0]),\n        .load_rom_data(load_rom_data),\n")
    text = smartport_iigs(text)
    return normalize(external_cpu_port(text))


def smartport_iigs(text: str) -> str:
    """Keep native slot timing and DMA, widen only the slot-7 controller bus."""
    text=replace(text,"output [15:0] HDD_SECTOR", "output [31:0] HDD_SECTOR")
    for direction,name in (("output","HDD_READ"),("output","HDD_WRITE"),("input","HDD_MOUNTED"),("input","HDD_ACK")):
        text=re.sub(rf"({direction}\s+)\[1:0\](\s+{name})",r"\g<1>[7:0]\2",text)
    text=replace(text,"input [7:0]   HDD_ACK,", "input [7:0]   HDD_ACK,\n  input [7:0] HDD_ERROR=8'd0,")
    text=replace(text,"logic [15:0]        hdd_dma_addr;", "logic [23:0]        hdd_dma_addr;")
    text=replace(text,"hdd_dma ? {DMAREG, hdd_dma_addr} : cpu_addr", "hdd_dma ? hdd_dma_addr : cpu_addr")
    text=replace(text,".IO_SELECT(io_select[7]),", ".IO_SELECT(io_select[7] | hdd_expansion_select),")
    text=replace(text,".DMA_ADDR(hdd_dma_addr),", ".DMA_ADDR(hdd_dma_addr),\n        .DMA_BANK(DMAREG),")
    text=replace(text,".sd_ack(HDD_ACK)", ".sd_ack(HDD_ACK),\n        .sd_error(HDD_ERROR)")
    text=replace(text,"wire       hdd_dma;", """wire       hdd_dma;
// A Cn access selects that slot's C800 firmware. CFFF releases ownership
// after its data phase. Internal/other slots never inherit slot-7 ownership.
reg hdd_expansion_owner=1'b0;
wire hdd_slot_bank=bank_bef==8'h00 || bank_bef==8'h01 || bank_bef==8'he0 || bank_bef==8'he1;
wire hdd_expansion_select=hdd_expansion_owner && hdd_slot_bank && !inhibit_cxxx &&
    !CXROM && SLTROMSEL[7] && addr_bef>=16'hc800 && addr_bef<=16'hcfff;
always @(posedge CLK_14M) begin
    if(reset)hdd_expansion_owner<=0;
    else if(bus_phi2 && !hdd_dma && hdd_slot_bank && !inhibit_cxxx)begin
        if(addr_bef==16'hcfff)hdd_expansion_owner<=0;
        else if(addr_bef>=16'hc100 && addr_bef<16'hc800)
            hdd_expansion_owner<=io_select[7];
    end
end""")
    text=replace(text,"(io_select[7] == 1'b1 | device_select[7] == 1'b1 | (hdd_dma & hdd_dma_we))", "(io_select[7] | hdd_expansion_select | device_select[7] | (hdd_dma & hdd_dma_we))")
    return text

def external_cpu_port(text: str) -> str:
    text=replace(text,"module iigs\n", "module iigs #(parameter RAMSIZE=128, parameter EXTERNAL_CPU=0, parameter integer MAX_ACCEL_SPEED=6)\n")
    text=replace(text,"   parameter RAMSIZE = 128; // 16x64k = 1MB, max = 127x64k = 8MB", "   // RAMSIZE is retained as a configurable module parameter above.")
    text=replace(text,"output cpu_valid,", """// External CPU mode uses the primary clock. The adapter holds the native
// bus stable, commits it once on CLK_14M, then consumes the return toggle.
input CPU_CLK=1'b0,
input CPU_CE=1'b0,
input CPU_PUBLISH=1'b0,
input [7:0] CPU_DIN=8'hFF,
input NATIVE_COMMIT=1'b0,
output [2:0] cpu_speed,
output cpu_fast_cycle,
// A fast CPU enable that is not a native retirement, and the primary edge
// equivalent to the CLK_14M rise (one edge per native commit or DMA cycle).
input CPU_FAST_CE=1'b0,
input NATIVE_EDGE=1'b0,
// Primary-clock Mega II RAM path: direct E0/E1 RAM reads, and E0/E1 or
// shadowed writes, complete on CPU_CE without the native bridge. A read
// completes from registered port-A data (ready/data are registers). A write
// is staged and written on a later edge; pending is high while it is staged
// and no store may retire while it is pending on NATIVE_EDGE.
output cpu_slowram_read,
output cpu_slowram_write,
output cpu_slowram_ready,
output [7:0] cpu_slowram_data,
output cpu_slowram_pending,
// A native peripheral cannot take another host access yet (sound GLU busy).
output cpu_native_hold,
// Slot reads whose device output is registered twice need two native
// preparation edges before the capture edge.
output cpu_native_two_edge,
output cpu_memory_cycle,
output [23:0] cpu_logical_addr,
output cpu_vector_n,
output cpu_local_read,
output [7:0] cpu_local_data,
output reg [7:0] native_read_data,
output reg native_return_toggle,
output cpu_valid,""")
    text=replace(text,"wire bus_phi2 = ph2_en & (~mem_stall | reset);", """wire bus_phi2 = EXTERNAL_CPU
    ? (hdd_dma ? (ph2_en & ~mem_stall) : (NATIVE_COMMIT & ~reset))
    : (ph2_en & (~mem_stall | reset));
// A DMA transaction never creates a CPU response. CPU CE must also stay low
// until DMA relinquishes the address bus, enforced independently by RDY_IN.
always @(posedge CLK_14M) begin
    if (reset) begin native_read_data<=0; native_return_toggle<=0; end
    else if (EXTERNAL_CPU && NATIVE_COMMIT && !hdd_dma) begin
        native_read_data<=cpu_din;
        native_return_toggle<=~native_return_toggle;
    end
end
assign cpu_speed = accel_active ? eff_speed_code : 3'd0;
assign cpu_logical_addr = cpu_addr;
assign cpu_vector_n = cpu_vpb;
assign cpu_local_read = IO && !we && zip_unlocked && (addr_bef[7:3]==5'b01011);
assign cpu_local_data = zip_rdata;
// External slot registers/ROMs are native peripherals even when an MMU ROM
// enable overlaps their address. Shadow RAM writes still update main memory.
// Native IRQ dispatch at C071-C07F returns mapped system ROM through din.
// Its IO cycle still needs a memory lookup before the native commit.
wire cpu_irq_dispatch_read = IO && !we && addr_bef[15:0]>=16'hc071 && addr_bef[15:0]<=16'hc07f;
assign cpu_memory_cycle = cpu_valid && (fastram_ce || (rom_ce && !we)) &&
    (hdd_dma || ((!IO || cpu_irq_dispatch_read) && !EXTERNAL_IO && !(|device_select) && !(|io_select) && !slot_ce && !hdd_expansion_select));
// Use the current MMU classification, not CLK14's delayed slowMem register:
// Shadow reads select fast RAM even when the mirror enable is also asserted.
// Slot/IO and Mega II $C000-$CFFF use the native bridge. Direct E0/E1 RAM
// ($0000-$BFFF, $D000-$FFFF with aux/ALTZP/LC/bank-latch mapping already in
// the physical address) and every slow-RAM write (E0/E1 or shadow copy) use
// the primary-clock slow-RAM port. E0/E1 LC ROM reads use the ROM cache.
wire cpu_plain_bus = !IO && !EXTERNAL_IO && !(|device_select) && !(|io_select) && !slot_ce && !hdd_expansion_select;
wire cpu_mega2_bank = bank_bef == 8'hE0 || bank_bef == 8'hE1;
wire cpu_mega2_window = cpu_mega2_bank && (addr_bef < 16'hC000 || addr_bef >= 16'hD000);
assign cpu_slowram_read = EXTERNAL_CPU && !hdd_dma && valid && !we && cpu_plain_bus &&
    cpu_mega2_window && slowram_ce && !fastram_ce && !rom_ce && !twgs_sel;
assign cpu_slowram_write = EXTERNAL_CPU && !hdd_dma && valid && we && cpu_plain_bus &&
    slowram_ce && (cpu_mega2_window || fastram_ce);
wire cpu_mega2_rom_read = EXTERNAL_CPU && !hdd_dma && valid && !we && cpu_plain_bus &&
    cpu_mega2_bank && addr_bef >= 16'hD000 && rom_ce;
assign cpu_fast_cycle = !hdd_dma && (!valid || cpu_local_read ||
    (cpu_plain_bus && (!slowram_ce || (!we && fastram_ce)) && (fastram_ce || rom_ce)) ||
    cpu_slowram_read || cpu_slowram_write || cpu_mega2_rom_read);
// hdd.sv registers slot ROM twice (address -> rom_dout -> D_OUT) and its
// register mirror once. Without the 1 MHz sync the native bridge must give
// these reads a second preparation edge before its capture edge.
assign cpu_native_two_edge = !we && ((|io_select) || (|device_select) || hdd_expansion_select);
// The cache and slow-RAM write byte. Unlike dout it never passes through the
// I/O read mux (cpu_din); only write cycles consume it.
wire [7:0] cpu_bus_write_byte = (hdd_dma & hdd_dma_we) ? HDD_DO : cpu_dout;
// The sound GLU runs one host access per DOC oscillator slot (osc_en, every
// 16 CLK_14M). Without the 1 MHz sync an accelerated CPU reaches C03C-C03F
// again before that access ran, so a register/RAM write is lost or lands at
// a moved address and a read returns stale data. Hold the next GLU access
// on the native bridge until the GLU is idle. Stock timing is unchanged.
assign cpu_native_hold = external_accel && native_snd_sel && snd_busy;
""")
    # External CPU mode clocks slow-RAM port A from the primary clock. Every
    # port-A write is staged: a fast store on CPU_FAST_CE, or a native/DMA
    # commit on its CLK_14M edge, captures {address,data} from the held bus on
    # that edge. The write lands on the next primary edge that is not
    # NATIVE_EDGE (the video port's read edge) and exactly once per commit.
    # WEA, ENA and DIA then come from registers, never from CPU/MMU decode or
    # cpu_ce in the same cycle, and no write meets a same-edge video read.
    text=replace(text,"/* verilator lint_off PINMISSING */\napple_iigs_slowram slowram\n", """wire slowram_wr_set;
reg slowram_wr_pending = 1'b0;
reg [16:0] slowram_wr_addr = 17'd0;
reg [7:0] slowram_wr_data = 8'd0;
wire slowram_wren_a = EXTERNAL_CPU ? (slowram_wr_pending & ~NATIVE_EDGE) : (slowram_we & bus_phi2);
/* verilator lint_off PINMISSING */
apple_iigs_slowram slowram
""")
    text=replace(text,"        .clock_a(CLK_14M),", "        .clock_a(EXTERNAL_CPU ? CPU_CLK : CLK_14M),")
    text=replace(text,"        .address_a({bank[0], addr}),", "        .address_a((EXTERNAL_CPU && slowram_wr_pending) ? slowram_wr_addr : {bank[0], addr}),")
    text=replace(text,"        .data_a(dout),", "        .data_a(EXTERNAL_CPU ? slowram_wr_data : dout),")
    text=replace(text,"        .wren_a(slowram_we & bus_phi2),", "        .wren_a(slowram_wren_a),")
    # Port A reads its (live or staged) address on every primary edge, so the
    # MMU decode never reaches the block-RAM enables. Consumers use q_a only
    # while slowram_ce selects it, and each bank still enables by address.
    text=replace(text,"        .enable_a(slowram_ce),", "        .enable_a(EXTERNAL_CPU ? 1'b1 : slowram_ce),")
    text=replace(text,"        .enable_b(ce_pix)\n);\n/* verilator lint_on PINMISSING */\n", """        .enable_b(ce_pix)
);
/* verilator lint_on PINMISSING */
// External mode enables port A on every primary edge: q_a always holds the
// last (live or staged) address, and no MMU decode reaches the enables.
// Fast E0/E1 reads: the first held edge with no staged write (and no CPU_CE)
// reads the live address and arms; the next edge registers port A's output;
// the CPU completes from that register on the third edge. The bus and its
// MMU mapping cannot change while CPU_CE is low: soft switches change only
// on retired native cycles, and DMA clears the flags. The CPU data input and
// its select therefore see only registers on this path.
reg slowram_rd_armed = 1'b0;
reg slowram_rd_ready = 1'b0;
reg [7:0] slowram_rd_data = 8'd0;
generate if (EXTERNAL_CPU) begin : g_slowram_fast
  assign slowram_wr_set = slowram_we & slowram_ce &
      ((bus_phi2 & NATIVE_EDGE) | (CPU_FAST_CE & cpu_slowram_write));
  always @(posedge CPU_CLK) begin
    // A staged tuple is held until it is written. The edge that writes it may
    // stage the next store; on a NATIVE_EDGE that defers it the adapter
    // retires no write, and native/DMA commits are four primary edges apart.
    if (!slowram_wr_pending || !NATIVE_EDGE) begin
      slowram_wr_addr <= {bank[0], addr};
      slowram_wr_data <= cpu_bus_write_byte;
    end
    slowram_wr_pending <= slowram_wr_set | (slowram_wr_pending & NATIVE_EDGE);
    if (reset || CPU_CE || hdd_dma) begin
      slowram_rd_armed <= 1'b0;
      slowram_rd_ready <= 1'b0;
    end else begin
      slowram_rd_armed <= cpu_slowram_read & ~slowram_wr_pending;
      slowram_rd_ready <= slowram_rd_armed;
    end
    if (slowram_rd_armed) slowram_rd_data <= slowram_dout;
  end
end else begin : g_slowram_legacy
  assign slowram_wr_set = 1'b0;
end endgenerate
assign cpu_slowram_ready = EXTERNAL_CPU && slowram_rd_ready;
assign cpu_slowram_data = slowram_rd_data;
assign cpu_slowram_pending = EXTERNAL_CPU && slowram_wr_pending;
""")
    # External CPU acceleration has no physical Mega II bus to synchronize
    # with. While accelerated, the divider sees no slow-class cycle, so IO,
    # E0/E1, shadow writes and slot space never wait for the 1 MHz PH0 sync,
    # and CYAREG motor-detect bits cannot force 1 MHz. Accelerated HDD DMA
    # runs 3-tick cycles, the floor of hdd.sv's two-register sector pipeline.
    # Native timing (fast_thresh==4) feeds the upstream inputs unchanged.
    text=replace(text,"clock_divider clk_div_inst (\n", """wire external_accel = EXTERNAL_CPU && fast_thresh != 4'd4;
wire [7:0] cd_cyareg = external_accel ? {CYAREG[7:4], 4'b0000} : CYAREG;
wire [3:0] cd_fast_thresh = (external_accel && hdd_dma) ? 4'd2 : fast_thresh;
wire cd_dma_active = external_accel ? 1'b0 : hdd_dma;
wire cd_slot_access = external_accel ? 1'b0 : slot_delay_this;
wire cd_valid = external_accel ? 1'b0 : valid;
clock_divider clk_div_inst (
""")
    text=replace(text,"    .cyareg(CYAREG),","    .cyareg(cd_cyareg),")
    text=replace(text,"    .fast_thresh(fast_thresh),","    .fast_thresh(cd_fast_thresh),")
    text=replace(text,"    .dma_active(hdd_dma),","    .dma_active(cd_dma_active),")
    text=replace(text,"    .slot_access(slot_delay_this),","    .slot_access(cd_slot_access),")
    text=replace(text,"    .valid(valid),","    .valid(cd_valid),")
    # The 2 ms IWM access hold applies only while the drive the IWM selects
    # ($C031 bit 6 family, $C0EA/$C0EB drive) holds media. Probes of an empty
    # drive stay accelerated even when the other drive of its family has a disk.
    text=replace(text,"reg [14:0] iwm_holdoff;\n", """reg [14:0] iwm_holdoff;
wire iwm_selected_media = iwm_drive_media;
""")
    text=replace(text,"  iwm_woz iwmc (\n", "  // Media in the drive the IWM has selected ($C031 bit 6 and $C0EA/B).\n  wire iwm_drive_media;\n  iwm_woz iwmc (\n")
    text=replace(text,"      .DRIVE35_EJECT_REQ(drive35_eject_req)\n  );", "      .DRIVE35_EJECT_REQ(drive35_eject_req),\n      .SELECTED_MEDIA(iwm_drive_media)\n  );")
    text=replace(text,"                          && iwm_holdoff == 15'd0\n",
                 "                          && (iwm_holdoff == 15'd0 || (EXTERNAL_CPU && !iwm_selected_media))\n")
    # IIgs paddle reset is C070 only; C071-C07F are IRQ dispatch ROM.
    # Match the native read/write decode, including its full 12-bit selector.
    text=replace(text,"wire acc_ptrig = bus_phi2 && IO && (addr_bef[7:4] == 4'h7);    // $C070-$C07F paddle trigger",
                 "wire acc_ptrig = bus_phi2 && IO && (addr_bef[11:0] == 12'h070); // IIgs paddle trigger only")
    # Raw IWM motor state can remain on after probing an empty drive. Keep it
    # intact for status and dirty flushing, but hold the accelerator only when
    # that drive family actually contains media. IWM access delay is separate.
    text=replace(text,"                          && !floppy_motor_on && !floppy35_motor_on",
                 "                          && !(floppy_motor_on && (|DISK_READY[1:0]))\n"
                 "                          && !(floppy35_motor_on && (|DISK_READY[3:2]))")
    text=replace(text,"              .CLK(CLK_14M),", "              .CLK(EXTERNAL_CPU ? CPU_CLK : CLK_14M),")
    text=replace(text,"              .CE(bus_phi2),", "              .CE(EXTERNAL_CPU ? CPU_CE : bus_phi2),")
    text=replace(text,"              .RDY_IN(~hdd_dma & ~mem_stall),", "              .RDY_IN(~hdd_dma & (EXTERNAL_CPU | ~mem_stall)),")
    text=replace(text,"              .D_IN(cpu_din),", "              .D_IN(EXTERNAL_CPU ? CPU_DIN : cpu_din),")
    text=replace(text,"  always @(posedge CLK_14M) if (bus_phi2) cpu_last_bus <= we ? dout : cpu_din;", """  generate if (EXTERNAL_CPU) begin : external_last_bus
    // The floating CPU bus follows actual completed CPU transfers, including
    // fast cache hits that never consume a native clock edge.
    always @(posedge CPU_CLK) begin
      if (reset) cpu_last_bus<=0;
      else if (CPU_CE && !hdd_dma) cpu_last_bus<=cpu_we_n ? CPU_DIN : cpu_dout;
    end
  end else begin : legacy_last_bus
    always @(posedge CLK_14M) if (bus_phi2) cpu_last_bus<=we ? dout : cpu_din;
  end endgenerate""")
    text=replace(text,"    if (IO) begin\n      // interrupt_clear_pulse", "    if (IO && (!EXTERNAL_CPU || bus_phi2)) begin\n      // interrupt_clear_pulse")
    text=replace(text,"      if (cpu_vpa && cpu_vda && (cpu_addr[23:0] == 24'hFEFC3A)) begin\n        if (INTEN[3] && VBlank && !irq_pending[3]) begin", """      if (EXTERNAL_CPU ? cpu_isr_vbl_published :
          (cpu_vpa && cpu_vda && (cpu_addr[23:0] == 24'hFEFC3A) &&
           INTEN[3] && VBlank && !irq_pending[3])) begin
        begin""")
    text=replace(text,"  // Centralized IRQ management - matches GSplus/Clemens architecture", """  // Accepted fast opcode fetches may last only one primary tick. Capture
  // their complete event-time eligibility, coalesce until phase0, then hold
  // the publication across CLK14. A later beam/enable change cannot erase an
  // accepted request, and an already-pending VBL does not create a new event.
  wire cpu_isr_vbl_event=EXTERNAL_CPU && CPU_CE && cpu_vpa && cpu_vda &&
      cpu_addr==24'hFEFC3A && INTEN[3] && VBlank && !irq_pending[3];
  reg cpu_isr_vbl_accum,cpu_isr_vbl_published;
  generate if(EXTERNAL_CPU) begin : external_isr_event
    always @(posedge CPU_CLK) begin
      if(reset)begin cpu_isr_vbl_accum<=0;cpu_isr_vbl_published<=0;end
      else if(CPU_PUBLISH)begin
        cpu_isr_vbl_published<=cpu_isr_vbl_accum || cpu_isr_vbl_event;
        cpu_isr_vbl_accum<=0;
      end else if(cpu_isr_vbl_event)cpu_isr_vbl_accum<=1;
    end
  end else begin : legacy_isr_event
    assign cpu_isr_vbl_accum=0,cpu_isr_vbl_published=0;
  end endgenerate

  // Centralized IRQ management - matches GSplus/Clemens architecture""")
    for strobe in ("adb_strobe", "prtc_strobe", "iwm_strobe", "snd_strobe"):
        text=re.sub(r"if \("+strobe+r"\)(\s+io_dout)",r"if (!EXTERNAL_CPU && "+strobe+r")\1",text)
    # In external mode registered CPU strobes would miss their own commit.
    # Native ports receive the stable bus directly; every effect is qualified
    # by that one commit. Read values are combinational before the same edge.
    text=replace(text,"  wire adb_strobe_mux = (adb_read | adb_strobe) & ~mem_stall;", """  wire adb_write_now = IO && we && (addr_bef[7:0]==8'h10 ||
      addr_bef[7:0]==8'h26 || addr_bef[7:0]==8'h27 || addr_bef[7:0]==8'h70);
  wire adb_strobe_mux = EXTERNAL_CPU ? ((adb_read || adb_write_now) && bus_phi2)
      : ((adb_read | adb_strobe) & ~mem_stall);
  // The RTC data/control pair crosses a two-byte decode boundary.
  wire native_prtc_sel = IO && (addr_bef[7:0]==8'h33 || addr_bef[7:0]==8'h34);
  wire native_scc_sel = IO && (addr_bef[7:2]==6'h0E);
  wire native_snd_sel = IO && (addr_bef[7:2]==6'h0F);
  wire [7:0] snd_read_peek;
  wire snd_busy;""")
    text=replace(text,"          .addr(adb_addr_mux),", "          .addr(EXTERNAL_CPU ? addr_bef[7:0] : adb_addr_mux),")
    text=replace(text,"          .rw(adb_rw_mux),", "          .rw(EXTERNAL_CPU ? ~we : adb_rw_mux),")
    text=replace(text,"          .din(adb_din),", "          .din(EXTERNAL_CPU ? cpu_dout : adb_din),")
    text=replace(text,"            .addr(prtc_addr),", "            .addr(EXTERNAL_CPU ? ~addr_bef[0] : prtc_addr),")
    text=replace(text,"            .din(prtc_din),", "            .din(EXTERNAL_CPU ? cpu_dout : prtc_din),")
    text=replace(text,"            .rw(prtc_rw),", "            .rw(EXTERNAL_CPU ? ~(native_prtc_sel && we && bus_phi2) : prtc_rw),")
    text=replace(text,"            .strobe(prtc_strobe),", "            .strobe(EXTERNAL_CPU ? (native_prtc_sel && bus_phi2) : prtc_strobe),")
    text=replace(text,"            .cs(scc_cs),", "            .cs(EXTERNAL_CPU ? (native_scc_sel && bus_phi2) : scc_cs),")
    text=replace(text,"            .we(scc_we),", "            .we(EXTERNAL_CPU ? we : scc_we),")
    text=replace(text,"            .rs(scc_rs),", "            .rs(EXTERNAL_CPU ? addr_bef[1:0] : scc_rs),")
    text=replace(text,"            .wdata(scc_din),", "            .wdata(EXTERNAL_CPU ? cpu_dout : scc_din),")
    text=replace(text,"            .select(snd_strobe),", "            .select(EXTERNAL_CPU ? (native_snd_sel && bus_phi2) : snd_strobe),")
    text=replace(text,"            .wr(snd_rw),", "            .wr(EXTERNAL_CPU ? we : snd_rw),")
    text=replace(text,"            .host_addr(snd_addr),", "            .host_addr(EXTERNAL_CPU ? addr_bef[1:0] : snd_addr),")
    text=replace(text,"            .host_data_in(snd_din),", "            .host_data_in(EXTERNAL_CPU ? cpu_dout : snd_din),")
    text=replace(text,"            .host_data_out(snd_dout),", "            .host_data_out(snd_dout),\n            .host_read_peek(snd_read_peek),\n            .host_busy(snd_busy),")
    text=replace(text,"  wire [7:0] cpu_din = IO ? ((adb_read ? adb_dout : (iwm_strobe ? iwm_dout : io_dout)))", """  wire [7:0] external_io_din = adb_read ? adb_dout :
      native_prtc_sel ? prtc_dout : native_scc_sel ? scc_dout :
      native_snd_sel ? snd_read_peek : (addr_bef[7:4]==4'hE) ? iwm_dout : io_dout;
  wire [7:0] cpu_din = IO ? (EXTERNAL_CPU ? external_io_din : (adb_read ? adb_dout : (iwm_strobe ? iwm_dout : io_dout)))""")
    text=replace(text,"   assign top_dout = dout;", "   assign top_dout = EXTERNAL_CPU ? cpu_bus_write_byte : dout;")
    text=replace(text,"zipgs_regs zipgs (", "zipgs_regs #(.MAX_SPEED_CODE(MAX_ACCEL_SPEED)) zipgs (")
    text=replace(text,"    hdd hdd(", "    hdd #(.EXTERNAL_BUS(EXTERNAL_CPU)) hdd(")
    text=replace(text,"? (4'd4 - {1'b0, eff_speed_code})", "? ((eff_speed_code>=3'd4) ? 4'd0 : (4'd4 - {1'b0, eff_speed_code}))")
    return add_cpu_peek_iigs(text)

def generate_sound() -> str:
    text=NOTICE+upstream("rtl/sound.v")
    text=replace(text,"    output [7:0]      host_data_out,", "    output [7:0]      host_data_out,\n    output [7:0]      host_read_peek,")
    text=replace(text,"    output [7:0]      host_read_peek,", "    output [7:0]      host_read_peek,\n    output            host_busy,")
    text=replace(text,"      .host_data_out(host_data_out),", "      .host_data_out(host_data_out),\n      .host_read_peek(host_read_peek),\n      .host_busy(host_busy),")
    return normalize(text)

def generate_soundglu() -> str:
    text=NOTICE+upstream("rtl/soundglu.v")
    text=replace(text,"   output reg [7:0]  host_data_out,", "   output reg [7:0]  host_data_out,\n   output reg [7:0]  host_read_peek,\n   output wire       host_busy,")
    text=replace(text,"   always @(posedge CLK_14M) begin", """   // A host access is pending or finishing until the GLU returns to idle.
   assign host_busy = sound_cycle_state != ST_IDLE;
   // Read-only view of the existing host register mux. External CPU bridge
   // samples this before its commit edge, without speculative DOC accesses.
   always @* begin
      case (host_addr)
         0: host_read_peek={sound_cycle_state==ST_PENDING,ram_access,auto_increment,1'b1,volume};
         1: host_read_peek=read_data_reg;
         2: host_read_peek=sound_addr[7:0];
         3: host_read_peek=sound_addr[15:8];
      endcase
   end
   always @(posedge CLK_14M) begin""")
    return normalize(text)

def generate_zipgs() -> str:
    text=NOTICE+upstream("rtl/zipgs_regs.sv")
    text=replace(text,"module zipgs_regs (", "module zipgs_regs #(parameter integer MAX_SPEED_CODE=6) (")
    start=text.index("  // Zip speed percentage nibble ->")
    end=text.index("  // Host change detection:",start)
    text=text[:start]+"""  // Quantize down to the fastest supported rate <= the requested fraction
  // of the installed engine's maximum. Rates are in primary-clock units /12:
  // 3.58=.75, 4.77=1, 7.16=1.5, 14.32=3, 28.64=6, 57.27=12.
  // Integer units /60 represent every accelerated step exactly. Native uses
  // the authentic stretched CLK14 divider rather than this approximation.
  function automatic integer rate_units(input integer code);
    case(code)
      0: rate_units=12;
      1: rate_units=15;
      2: rate_units=20;
      3: rate_units=30;
      4: rate_units=60;
      5: rate_units=120;
      default: rate_units=240;
    endcase
  endfunction
  function automatic [2:0] sp_to_code(input [3:0] s);
    integer code;
    begin
      sp_to_code=0;
      for(code=1;code<=MAX_SPEED_CODE;code=code+1)
        if(rate_units(code)*16 <= rate_units(MAX_SPEED_CODE)*(16-int'(s)))
          sp_to_code=code[2:0];
    end
  endfunction
  // Pick a software percentage that round-trips exactly whenever the sixteen
  // percentage values can represent that host step. Host retains finer low
  // rates even when a 57MHz installation's minimum percentage exceeds them.
  function automatic [3:0] host_to_sp(input [2:0] h);
    integer percent;
    begin
      host_to_sp=15;
      for(percent=15;percent>=0;percent=percent-1)
        if(sp_to_code(percent[3:0])==h) host_to_sp=percent[3:0];
    end
  endfunction

"""+text[end:]
    # Host out-of-capability values fail down to the installed maximum, while
    # software and host share the same qualified installed-speed definition.
    text=replace(text,"speed_reg <= host_speed;", "speed_reg <= (host_speed>MAX_SPEED_CODE) ? 3'(MAX_SPEED_CODE) : host_speed;")
    text=replace(text,"sp        <= host_to_sp(host_speed);", "sp        <= host_to_sp((host_speed>MAX_SPEED_CODE) ? 3'(MAX_SPEED_CODE) : host_speed);")
    text=replace(text,"      unlock    <= 3'd0;", "      ms_ctr<=0; ms_toggle<=0;\n      unlock    <= 3'd0;")
    text=replace(text,"reg_c059  <= 8'h57;", "reg_c059  <= 8'h77;")
    return normalize(add_cpu_peek_zipgs(text))

def generate_vgc() -> str:
    text = upstream("rtl/vgc.v")
    text = replace(text, "module vgc (\n", NOTICE + "module vgc (\n" + rom_inputs())
    # Vivado 2025.2 loses the drivers of these forward-referenced net
    # declaration assignments, then removes the legacy graphics pipeline.
    # Declare them before any use and retain the original continuous logic.
    text = replace(text, "output [7:0] shr_bus_byte\n);",
                   "output [7:0] shr_bus_byte\n);\n\n"
                   "// Declare mode nets before their first use. Vivado otherwise loses\n"
                   "// these drivers and removes HGR/DHGR logic despite correct RTL simulation.\n"
                   "wire GR;\nwire lores_mode;\nwire dhires_mode;")
    for mode in ("GR", "lores_mode", "dhires_mode"):
        text = replace(text, f"wire {mode} =", f"assign {mode} =")
    text = replace(text, 'rom #(.memfile("chr.mem"),.AW(12)) charrom(\n',
                   "apple_iigs_runtime_rom #(.AW(12)) charrom(\n  .load_clk(load_rom_clk),\n  .load_we(load_rom_we),\n  .load_addr(load_rom_addr),\n  .load_data(load_rom_data),\n")
    # The synchronous video RAM needs the first byte two pixel edges before
    # its RGB use. Upstream starts one edge late, exposing one border column
    # at BL and dropping the final image pixel at BR. Keep the 640-wide crop.
    text = replace(text, "H==(BL-1)", "H==(BL-2)")
    text = replace(text, "H < (BL+1)", "H < BL")
    text = replace(text, "H == (BL+1) || (H == (BL+2) && !scb[7])",
                   "H == BL || (H == (BL+1) && !scb[7])")
    text = replace(text, "// SHRG: 33px left border (mem latency), 639px active",
                   "// SHR: all 640 pixels, after the earlier synchronous-RAM prefetch")
    text = replace(text, "\t// Clear SHRG pixel registers during left border to ensure clean start",
                   "\t// Seed fill-mode history with the same first byte used by the RGB bypass.\n"
                   "\t// The prefetch tick still sees the old palette byte; it is not a pixel.\n"
                   "\tif (SHRG && !scb[7] && H == BL) last_pixel <= video_data[7:4];\n\n"
                   "\t// Clear SHRG pixel registers during left border to ensure clean start")
    return normalize(text)

def generate_adb() -> str:
    text = upstream("rtl/adb.v")
    text = replace(text, "module adb(\n", NOTICE + "module adb(\n" + rom_inputs() + "input load_rom_bank,\n")
    for bank, name in ((0, "adb_rom_inst"), (1, "adb_rom1_inst")):
        source_name = "adb_rom3" if bank == 0 else "adb_rom1"
        text = replace(text, f'rom #(12, 8, "rtl/roms/{source_name}.hex") {name} (\n',
                       f"apple_iigs_runtime_rom #(.AW(12)) {name} (\n  .load_clk(load_rom_clk),\n  .load_we(load_rom_we & (load_rom_bank==1'b{bank})),\n  .load_addr(load_rom_addr),\n  .load_data(load_rom_data),\n")
    return normalize(text)

def generate_hdd() -> str:
    # Retain the original controller's license and fast native DMA architecture.
    # Command validation/status replace its optimistic, two-unit-only CPU shim.
    license_text=upstream("rtl/hdd.v").split("// Registers (per AppleWin")[0]
    return normalize(license_text+NOTICE+"""// Slot-7 ProDOS / SmartPort disk engine. C0F0 executes, C0F1 is final
// error (80 busy). F2 command bit7 selects direct SmartPort units1..8;
// bit6 records extended calls. Legacy commands0..2 use unit bit7.
// F4/5/F8=24-bit buffer, F6/7/9/A=32-bit LBA. Legacy DMA uses DMA_BANK.
// FB..FE=selected unit32-bit blockcount, FF=count:0000:protect:mounted.
module hdd #(parameter EXTERNAL_BUS=0)(
    input load_rom_clk,load_rom_we,input [11:0] load_rom_addr,input [7:0] load_rom_data,
    input CLK_14M,phi0,IO_SELECT,DEVICE_SELECT,RESET,
    input [15:0] A,input RD,input [7:0] D_IN,output reg [7:0] D_OUT,
    output reg DMA,output wire [23:0] DMA_ADDR,input [7:0] DMA_BANK=8'd0,
    output reg DMA_WE,output wire [31:0] sector,
    output reg [7:0] sd_rd,sd_wr,input [7:0] img_mounted,
    input img_readonly,input [63:0] img_size,
    input [8:0] hps_ram_addr,input [7:0] ram_di,output reg [7:0] ram_do,
    input ram_we,input [7:0] sd_ack,input [7:0] sd_error=8'd0
);
    reg [7:0] reg_status,reg_command,reg_unit,reg_mem_l,reg_mem_h,reg_mem_bank;
    reg [7:0] reg_block_l,reg_block_h,reg_block_u,reg_block_v;
    reg [7:0] hdd_mounted=0,hdd_protect=0;
    reg [31:0] hdd_size[0:7];
    reg [8:0] a2_ram_addr;
    reg [2:0] active_unit;
    reg [23:0] active_buffer;
    reg active_smart;
    reg [31:0] active_sector;
    wire [2:0] hdd_unit=reg_command[7] ? reg_unit[2:0]-3'd1 : {2'd0,reg_unit[7]};
    wire unit_valid=!reg_command[7] || (reg_unit>=1 && reg_unit<=8);
    wire current_unit_mounted=unit_valid && hdd_mounted[hdd_unit];
    wire current_unit_protect=hdd_protect[hdd_unit];
    wire any_unit_mounted=|hdd_mounted;
    wire [31:0] block_number=reg_command[7] ? {reg_block_v,reg_block_u,reg_block_h,reg_block_l} : {16'd0,reg_block_h,reg_block_l};
    wire [31:0] current_size=current_unit_mounted ? hdd_size[hdd_unit] : 32'd0;
    wire [15:0] legacy_size=|current_size[31:16] ? 16'hffff : current_size[15:0];
    reg [3:0] present_count;
    integer n;
    always @*begin
        present_count=0;
        for(integer unit_index=0;unit_index<8;unit_index=unit_index+1)
            present_count=present_count+{3'd0,hdd_mounted[unit_index]};
    end
    initial for(integer i=0;i<8;i=i+1)hdd_size[i]=0;
    always @(posedge CLK_14M)begin
        for(integer i=0;i<8;i=i+1)if(img_mounted[i])begin
            hdd_mounted[i]<=|img_size;
            hdd_protect[i]<=img_readonly;
            hdd_size[i]<=img_size[40:9];
        end
    end
    localparam ST_IDLE=0,ST_RD_ACK=1,ST_RD_HPS=2,ST_RD_A2=3,
        ST_WR_PRE=4,ST_WR_A2=5,ST_WR_ACK=6,ST_WR_HPS=7;
    reg [2:0] dma_state;
    wire dma_req_rd=DEVICE_SELECT && RD && A[3:0]==0 && phi0 && reg_command[5:0]==1;
    wire [7:0] sector_dout,sector_dma_q,rom_dout;
    wire a2_ram_we=(dma_state==ST_WR_PRE || dma_state==ST_WR_A2);
    wire [15:0] legacy_dma_offset=active_buffer[15:0]+{7'd0,a2_ram_addr};
    assign DMA_ADDR=active_smart ? active_buffer+{15'd0,a2_ram_addr} : {active_buffer[23:16],legacy_dma_offset};
    assign sector=active_sector;
    wire [7:0] completion_error=(sd_error==8'h28 || sd_error==8'h2b || sd_error==8'h2d || sd_error==8'h2f) ? sd_error : sd_error==0 ? 8'd0 : 8'h27;
    // A native bus byte retires only on phi0; read/BRAM pipelines remain
    // live between retirements exactly as in the upstream DMA engine.
    always @(posedge CLK_14M)begin
        if(RESET)begin
            dma_state<=ST_IDLE;DMA<=0;DMA_WE<=0;sd_rd<=0;sd_wr<=0;
            a2_ram_addr<=0;active_unit<=0;active_buffer<=0;active_sector<=0;active_smart<=0;
            reg_status<=0;reg_command<=0;reg_unit<=0;reg_mem_l<=0;reg_mem_h<=0;reg_mem_bank<=0;
            reg_block_l<=0;reg_block_h<=0;reg_block_u<=0;reg_block_v<=0;
        end else begin
            case(dma_state)
                ST_RD_ACK:if(sd_ack[active_unit])begin sd_rd[active_unit]<=0;dma_state<=ST_RD_HPS;end
                ST_RD_HPS:if(!sd_ack[active_unit] && phi0)begin
                    if(completion_error!=0)begin reg_status<=completion_error;DMA<=0;dma_state<=ST_IDLE;end
                    else begin a2_ram_addr<=0;DMA_WE<=1;dma_state<=ST_RD_A2;end
                end
                ST_RD_A2:if(phi0)begin
                    a2_ram_addr<=a2_ram_addr+1'b1;
                    if(a2_ram_addr==511)begin DMA_WE<=0;DMA<=0;reg_status<=0;dma_state<=ST_IDLE;end
                end
                ST_WR_PRE:if(phi0)dma_state<=ST_WR_A2;
                ST_WR_A2:if(phi0)begin
                    a2_ram_addr<=a2_ram_addr+1'b1;
                    if(a2_ram_addr==511)begin sd_wr[active_unit]<=1;dma_state<=ST_WR_ACK;end
                end
                ST_WR_ACK:if(sd_ack[active_unit])begin sd_wr[active_unit]<=0;dma_state<=ST_WR_HPS;end
                ST_WR_HPS:if(!sd_ack[active_unit] && phi0)begin
                    DMA<=0;reg_status<=completion_error;dma_state<=ST_IDLE;
                end
                default:;
            endcase
            if(phi0 && dma_state==ST_IDLE && DEVICE_SELECT)begin
                if(!RD)case(A[3:0])
                    2:begin reg_command<=D_IN;reg_status<=0;end
                    3:reg_unit<=D_IN;
                    4:reg_mem_l<=D_IN;
                    5:reg_mem_h<=D_IN;
                    6:reg_block_l<=D_IN;
                    7:reg_block_h<=D_IN;
                    8:reg_mem_bank<=D_IN;
                    9:reg_block_u<=D_IN;
                    10:reg_block_v<=D_IN;
                    default:;
                endcase
                else if(A[3:0]==0)begin
                    if(!unit_valid)reg_status<=8'h28;
                    else if(!current_unit_mounted)reg_status<=8'h2f;
                    else if(reg_command[5:0]==0)reg_status<=0;
                    else if(reg_command[5:0]!=1 && reg_command[5:0]!=2)reg_status<=8'h21;
                    else if(block_number>=current_size)reg_status<=8'h2d;
                    else if(reg_command[7] && {reg_mem_bank,reg_mem_h,reg_mem_l}>24'hfffe00)reg_status<=8'h22;
                    else if(reg_command[5:0]==2 && current_unit_protect)reg_status<=8'h2b;
                    else begin
                        reg_status<=8'h80;active_unit<=hdd_unit;active_sector<=block_number;active_smart<=reg_command[7];
                        active_buffer<={reg_command[7] ? reg_mem_bank : DMA_BANK,reg_mem_h,reg_mem_l};
                        a2_ram_addr<=0;DMA<=1;DMA_WE<=0;
                        if(reg_command[5:0]==1)begin sd_rd[hdd_unit]<=1;dma_state<=ST_RD_ACK;end
                        else dma_state<=ST_WR_PRE;
                    end
                end
            end
        end
    end
    // Registered native read mirror. C0F0's final result is sampled only
    // after DMA releases CPU, so errors reach legacy callers as well.
    always @(posedge CLK_14M)begin
        D_OUT<=8'hff;
        if(DMA && DMA_WE)D_OUT<=sector_dout;
        else if(DEVICE_SELECT && RD)case(A[3:0])
            0:begin
                if(DMA)D_OUT<=reg_status;
                else if(!unit_valid)D_OUT<=8'h28;
                else if(!current_unit_mounted)D_OUT<=8'h2f;
                else if(reg_command[5:0]==0)D_OUT<=0;
                else if(reg_command[5:0]!=1 && reg_command[5:0]!=2)D_OUT<=8'h21;
                else if(block_number>=current_size)D_OUT<=8'h2d;
                else if(reg_command[7] && {reg_mem_bank,reg_mem_h,reg_mem_l}>24'hfffe00)D_OUT<=8'h22;
                else if(reg_command[5:0]==2 && current_unit_protect)D_OUT<=8'h2b;
                else D_OUT<=reg_status;
            end
            1:D_OUT<=reg_status;
            2:D_OUT<=reg_command;
            3:D_OUT<=reg_unit;
            4:D_OUT<=reg_mem_l;
            5:D_OUT<=reg_mem_h;
            6:D_OUT<=reg_block_l;
            7:D_OUT<=reg_block_h;
            8:D_OUT<=reg_mem_bank;
            9:D_OUT<=legacy_size[7:0];
            10:D_OUT<=legacy_size[15:8];
            11:D_OUT<=current_size[7:0];
            12:D_OUT<=current_size[15:8];
            13:D_OUT<=current_size[23:16];
            14:D_OUT<=current_size[31:24];
            15:D_OUT<={present_count,2'b00,current_unit_mounted && current_unit_protect,current_unit_mounted};
        endcase
        else if(IO_SELECT && RD)D_OUT<=any_unit_mounted ? rom_dout : 8'hff;
    end
    bram #(.widthad_a(9)) sector_ram(
        .clock_a(CLK_14M),.wren_a(ram_we),.address_a(hps_ram_addr),.data_a(ram_di),.q_a(sector_dma_q),
        .clock_b(CLK_14M),.wren_b(a2_ram_we && phi0),.address_b(a2_ram_addr),.data_b(D_IN),.q_b(sector_dout),
        .enable_a(1'b1),.enable_b(1'b1));
    always @(posedge CLK_14M)ram_do<=sector_dma_q;
    wire [11:0] rom_address=A[11:8]==4'h7 ? {4'd0,A[7:0]} : 12'h100+{1'd0,A[10:0]};
    apple_iigs_runtime_rom #(.AW(12)) hddrom(
        .load_clk(load_rom_clk),.load_we(load_rom_we),.load_addr(load_rom_addr),.load_data(load_rom_data),
        .clock(CLK_14M),.ce(1'b1),.address(rom_address),.q(rom_dout));
endmodule
""")

def generate_prtc() -> str:
    text = upstream("rtl/prtc.v")
    text = replace(text, "module prtc(\n", NOTICE + "module prtc(\n")
    before = text[text.index("  // Initialize PRAM from hex file"):text.index("\n`ifdef DEBUG_PRTC", text.index("initial begin"))]
    text = replace(text, before, "  // Invalid blank PRAM makes the user's IIgs ROM install its own defaults.\n  // No Apple/MAME PRAM bytes are embedded. Runtime bk_* restores saved PRAM.\n  for (integer index=0; index<256; index=index+1) pram[index]=0;\n")
    return normalize(text)

def generate_bram() -> str:
    return """// SPDX-License-Identifier: GPL-3.0-only
// Multitini inferred replacement for MiSTer's rtl/dpram_bram.vhd altsyncram.
// Both enabled ports have one edge read latency and same-port write-first
// behavior, matching NEW_DATA_NO_NBE_READ. Different-clock address collisions
// are undefined by the hardware; clients must not concurrently write a byte.
// Zero power-up contents match power_up_uninitialized=FALSE.
module bram #(
    parameter integer width_a=8,
    parameter integer widthad_a=10
) (
    input wire clock_a, input wire enable_a=1'b1, input wire wren_a,
    input wire [widthad_a-1:0] address_a,
    input wire [width_a-1:0] data_a,
    output reg [width_a-1:0] q_a,
    input wire clock_b, input wire enable_b=1'b1, input wire wren_b,
    input wire [widthad_a-1:0] address_b,
    input wire [width_a-1:0] data_b,
    output reg [width_a-1:0] q_b
);
    // The two writers implement a true dual-port BRAM, rather than registers.
    /* verilator lint_off MULTIDRIVEN */
    (* ram_style="block" *) reg [width_a-1:0] mem[0:(1<<widthad_a)-1];
    /* verilator lint_on MULTIDRIVEN */
    initial begin
        for (integer index=0; index<(1<<widthad_a); index=index+1) mem[index]=0;
        q_a=0; q_b=0;
    end
    always @(posedge clock_a) if (enable_a) begin
        if (wren_a) begin mem[address_a]<=data_a; q_a<=data_a; end
        else q_a<=mem[address_a];
    end
    always @(posedge clock_b) if (enable_b) begin
        if (wren_b) begin mem[address_b]<=data_b; q_b<=data_b; end
        else q_b<=mem[address_b];
    end
endmodule

// Only the 128KiB slow/video RAM needs explicit depth banking. An unbanked
// 128Kx8 array can infer 7-series hard cascades whose optimized address nets
// violate REQP-1962. Each 16Kx8 bank instead maps to parallel 16Kx2 RAMB36s,
// without extension cascades. Preserve the bank boundaries during synthesis.
// The selected bank follows each enabled read edge independently on A/B;
// retaining it while disabled also retains the observable output, exactly
// like bram. The bank's registered data and this selector change together,
// so this adds no read cycle. Same-byte cross-clock collisions remain undefined.
module apple_iigs_slowram (
    input wire clock_a, input wire enable_a=1'b1, input wire wren_a,
    input wire [16:0] address_a,
    input wire [7:0] data_a, output wire [7:0] q_a,
    input wire clock_b, input wire enable_b=1'b1, input wire wren_b,
    input wire [16:0] address_b,
    input wire [7:0] data_b, output wire [7:0] q_b
);
    reg [2:0] bank_a=0, bank_b=0;
    wire [7:0] bank_q_a [0:7];
    wire [7:0] bank_q_b [0:7];
    always @(posedge clock_a) if (enable_a) bank_a<=address_a[16:14];
    always @(posedge clock_b) if (enable_b) bank_b<=address_b[16:14];
    assign q_a=bank_q_a[bank_a];
    assign q_b=bank_q_b[bank_b];
    for (genvar index=0; index<8; index=index+1) begin:g_bank
        (* keep_hierarchy="yes" *) bram #(.widthad_a(14)) ram (
            .clock_a(clock_a), .enable_a(enable_a && address_a[16:14]==3'(index)),
            .wren_a(wren_a), .address_a(address_a[13:0]), .data_a(data_a), .q_a(bank_q_a[index]),
            .clock_b(clock_b), .enable_b(enable_b && address_b[16:14]==3'(index)),
            .wren_b(wren_b), .address_b(address_b[13:0]), .data_b(data_b), .q_b(bank_q_b[index])
        );
    end
endmodule
"""

def generate_runtime_rom() -> str:
    return """// SPDX-License-Identifier: GPL-3.0-only
// Load a user ROM while the machine is held in reset; then synchronous reads.
// Runtime uploads retain MiSTer's one-edge, CE-qualified read latency. Never
// write while the CPU/video consumes this ROM. No Apple bytes are initialized.
module apple_iigs_runtime_rom #(parameter integer AW=12)(
    input wire clock, input wire ce,
    input wire [AW-1:0] address, output reg [7:0] q,
    input wire load_clk, input wire load_we,
    input wire [AW-1:0] load_addr, input wire [7:0] load_data
);
    (* ram_style="block" *) reg [7:0] data[0:(1<<AW)-1];
    always @(posedge load_clk) if (load_we) data[load_addr]<=load_data;
    always @(posedge clock) if (ce) q<=data[address];
endmodule
"""

def generate_twgs() -> str:
    text = upstream("rtl/twgs_card.sv")
    interface = text[text.index("module twgs_card ("):text.index("\n  // ---- bank-$BC")]
    return "// SPDX-License-Identifier: GPL-3.0-only\n" + NOTICE + \
        "// Interface retained; initial Multitini accelerator has no TWGS firmware.\n// This absent-card stub consumes no ROM or X2444 state and never overlays RAM.\n" + interface + """
    assign sel=0, dout=8'hFF, accel_en=0, speed_code=0, cfg_speed_code=0;
    assign cache_enable=0, irq_logic_en=0, gfx_en=0, snd_en=0, bk_q=8'hFF;
endmodule
"""

def generate_woz() -> str:
    text = upstream("rtl/woz_floppy_controller.sv")
    text = replace(text, "module woz_floppy_controller #(\n", NOTICE + "module woz_floppy_controller #(\n")
    text = replace(text, "    parameter IS_35_INCH = 0,", "    parameter integer CLOCK_MULTIPLIER = 1, // controller clock / 14.318MHz\n    parameter IS_35_INCH = 0,")
    text = replace(text, "20'd50000", "(20'd50000 * CLOCK_MULTIPLIER)", count=2)
    text = replace(text, "    reg [23:0] dirty_flush_timer;", "    localparam integer FLUSH_BITS=$clog2(7000000*CLOCK_MULTIPLIER+1);\n    reg [FLUSH_BITS-1:0] dirty_flush_timer;")
    text = replace(text, "    localparam [23:0] DIRTY_FLUSH_DELAY = 24'd7000000;", "    localparam [FLUSH_BITS-1:0] DIRTY_FLUSH_DELAY = 7000000*CLOCK_MULTIPLIER;")
    text = replace(text, "    input             active,", "    input             force_flush = 1'b0, // hold until dirty clears before eject\n    output wire       dirty,\n    input             active,")
    text = replace(text, "    reg         dirty_side1;", "    reg         dirty_side1;\n    assign dirty = dirty_side0 | dirty_side1;")
    text = replace(text, "if (prev_active && !active && (dirty_side0 || dirty_side1) && woz_valid)",
                   "if ((force_flush || (prev_active && !active)) && (dirty_side0 || dirty_side1) && woz_valid)")
    # Explicitly enable unconditionally-read buffers; the shared inferred
    # replacement honors CE rather than ignoring the Quartus BRAM enables.
    text = replace(text, "        .clock_a(clk),", "        .clock_a(clk),\n        .enable_a(1'b1),", count=text.count("        .clock_a(clk),"))
    text = replace(text, "        .clock_b(clk),", "        .clock_b(clk),\n        .enable_b(1'b1),", count=text.count("        .clock_b(clk),"))
    return normalize(text)

def generate_iir() -> str:
    text = upstream("sys/iir_filter.v")
    declarations = ("wire [39:0] tap0;", "wire [39:0] tap1;", "wire [39:0] tap2;", "reg        ch = 0;", "reg [15:0] inp, inp_m;")
    for declaration in declarations:
        text = replace(text, declaration, "")
    text = replace(text, "wire [59:0] inp_mul", NOTICE + "\n".join(declarations) + "\nwire [59:0] inp_mul")
    text = replace(text, "reg  [39:0] x1, y;", "")
    text = replace(text, "wire [39:0] x  =", "reg [39:0] x1, y;\nwire [39:0] x  =")
    return normalize(text)

def generate_es5503() -> str:
    text=upstream("rtl/es5503.v")
    text=replace(text, "module es5503\n", NOTICE+"module es5503\n")
    text=replace(text, "    output [3:0]      ca,", "    output reg [3:0]  ca,")
    return normalize(text)

def generate_cpu() -> str:
    text=upstream("rtl/65C816/P65C816.sv")
    text=replace(text, "module P65C816\n", NOTICE+"// Independent SingleStep and directed tests qualify WAI IRQ polarity,\n// instruction-final emulation stack page and same-cycle index-width changes.\nmodule P65C816\n")
    text=replace(text, "NMI_SYNC == 1'b1 | IRQ_N == 1'b1 | ABORT_N == 1'b0", "NMI_SYNC == 1'b1 | IRQ_N == 1'b0 | ABORT_N == 1'b0")
    text=replace(text, "if (XF == 1'b1 & oldXF == 1'b0 & EF == 1'b0)",
                 "if (EF == 1'b0 && ((XF && !oldXF) ||\n                (MC.LOAD_P == 3'b011 && D_IN[4]) ||\n                (MC.LOAD_P == 3'b110 && IR[5] && DR[4])))")
    start=text.index("            case (MC.LOAD_SP)")
    end=text.index("            endcase",start)+len("            endcase")
    before=text[start:end]
    text=replace(text,before,before+"\n            // Preserve full intermediate stack arithmetic for long pushes/pulls,\n            // but architectural S is always page $01 when E=1.\n            if (EF && LAST_CYCLE) SP[15:8] <= 8'h01;")
    return normalize(text)

def generate_mcode() -> str:
    text=upstream("rtl/65C816/mcode.sv")
    text=replace(text,"module mcode\n",NOTICE+"// Synchronous block-ROM lookup retains the original one-edge microcode\n// timing; only its small default-word selector has asynchronous reset.\nmodule mcode\n")
    text=replace(text,"localparam logic [52:0] M_TAB[0:2047]",'(* rom_style="block" *) logic [52:0] M_TAB[0:2047]')
    start=text.index("   always @(posedge CLK or negedge RST_N)")
    end=text.index("\n   assign M =",start)
    original=text[start:end]
    default=re.search(r"MI <= (\{[^;]+\});",original).group(1)
    port=f"""   localparam logic [52:0] DEFAULT_WORD={default};
   logic [52:0] rom_word;
   logic use_default;
   wire [3:0] state_minus_one=STATE-4'd1;
   always @(posedge CLK) if (EN)
      rom_word <= M_TAB[{{IR,state_minus_one[2:0]}}];
   always @(posedge CLK or negedge RST_N) begin
      if (!RST_N) use_default <= 1'b1;
      else if (EN) use_default <= (STATE==4'b0000);
   end
   assign MI=use_default ? DEFAULT_WORD : rom_word;
"""
    text=replace(text,original,port)
    return normalize(text)

def generate_uart(name: str) -> str:
    # The upstream file deliberately declares `default_nettype none but
    # leaves it enabled into subsequent translation units in Vivado. Restore
    # the default at this module boundary, without relaxing the UART itself.
    text=upstream("rtl/uart/"+name+".v")
    return normalize(NOTICE+text+"\n`default_nettype wire\n")

def add_cpu_peek_iigs(text: str) -> str:
    block=re.search(r'  mmu #\(\.RAMSIZE\(RAMSIZE\)\) mmu_inst \(.*?\n  \);',text,re.S).group()
    peek=block.replace('mmu_inst','cpu_peek_mmu').replace('.bank_log(bank_bef)','.bank_log(cpu_addr[23:16])').replace('.addr_log(addr_bef)','.addr_log(cpu_addr[15:0])').replace('.we(we)','.we(~cpu_we_n)')
    output=False
    lines=[]
    for line in peek.splitlines():
        if '.addr_bus(' in line:output=True
        if output:line=re.sub(r'\.(\w+)\([^)]*\)',lambda m:'.'+m[1]+'('+('cpu_peek_io' if m[1]=='IO' else '')+')',line)
        lines.append(line)
    peek='\n'.join(lines)
    text=replace(text,block,block+'\n\n  // The primary CPU read overlay never decodes the HDD DMA bus.\n  wire cpu_peek_io;\n  generate if (EXTERNAL_CPU) begin : g_cpu_peek\n'+peek+'\n  end else begin : g_cpu_peek_legacy\n    assign cpu_peek_io=IO;\n  end endgenerate')
    text=replace(text,'wire [7:0] zip_rdata;','wire [7:0] zip_rdata;\nwire [7:0] zip_peek_data;')
    text=replace(text,'    .rd_data(zip_rdata),','    .rd_data(zip_rdata),\n    .peek_addr(cpu_addr[2:0]),\n    .peek_data(zip_peek_data),')
    text=replace(text,"assign cpu_local_read = IO && !we && zip_unlocked && (addr_bef[7:3]==5'b01011);", "assign cpu_local_read = EXTERNAL_CPU ? (cpu_peek_io && cpu_we_n && zip_unlocked && (cpu_addr[7:3]==5'b01011)) :\n    (IO && !we && zip_unlocked && (addr_bef[7:3]==5'b01011));")
    return replace(text,'assign cpu_local_data = zip_rdata;','assign cpu_local_data = EXTERNAL_CPU ? zip_peek_data : zip_rdata;')

def add_cpu_peek_zipgs(text: str) -> str:
    text=replace(text,'    output reg  [7:0] rd_data,','    output reg  [7:0] rd_data,\n    input  wire [2:0] peek_addr,\n    output wire [7:0] peek_data,')
    a=text.index('  always_comb begin\n    case (rd_addr)');b=text.index('\n  end',a)+len('\n  end')
    old=text[a:b]
    new=old.replace('  always_comb begin\n    case (rd_addr)','  function automatic [7:0] read_register(input [2:0] selector);\n    case (selector)').replace('rd_data =','read_register =').replace('\n  end','\n  endfunction')
    new+='\n  always_comb rd_data = read_register(rd_addr);\n  assign peek_data = read_register(peek_addr);'
    return text[:a]+new+text[b:]


def generate_flux_drive() -> str:
    text = upstream("rtl/flux_drive.v")
    text = replace(text, "module flux_drive (\n", NOTICE + "module flux_drive (\n")
    # Fractions remain below 1000; their sum with a step needs eleven bits.
    text = replace(text, "task automatic load_bit_timers;\n        reg [9:0] tmp;",
                   "task automatic load_bit_timers;\n        reg [10:0] tmp;")
    return normalize(text)


def generate_iwm_flux() -> str:
    text = upstream("rtl/iwm_flux.v")
    text = replace(text, "module iwm_flux (\n", NOTICE + "module iwm_flux (\n")
    for task in ("load_full_window", "load_half_window"):
        text = replace(text, "task automatic " + task + ";\n        reg [9:0] tmp;",
                       "task automatic " + task + ";\n        reg [10:0] tmp;")
    # Debug logging must not enable the functional unread-byte countdown.
    text = replace(text, "flux_edge_log_count <= flux_edge_log_count + 1'd1;\n            end\n\n            // Latch mode hold counter",
                   "flux_edge_log_count <= flux_edge_log_count + 1'd1;\n            end\n`endif\n\n            // Latch mode hold counter")
    text = replace(text, "\n            end\n`endif\n\n            // Latch flux edges that arrive in EDGE_1",
                   "\n            end\n\n            // Latch flux edges that arrive in EDGE_1")
    return normalize(text)


def generate_iwm_woz() -> str:
    text = upstream("rtl/iwm_woz.v")
    text = replace(text, "module iwm_woz (\n", NOTICE + "module iwm_woz (\n")
    # A real IWM latches the mode register on a write to any odd address
    # while Q6=Q7=1 and the chip is idle, not only at $C0EF. The GS/OS 5.25"
    # driver stores mode $04 through $C0ED to make motor-off immediate; the
    # dropped write left the one-second motor-off timer running three times.
    # The motor-on access ($C0E9) is excluded: MAME turns the motor on first,
    # so that write is a data/motor access and leaves the mode unchanged.
    text = replace(text, "    wire is_mode_write_access = cpu_access_edge && cpu_wr && !iwm_active &&\n"
                   "                                (A[3:0] == 4'hF) && write_mode_q6 && write_mode_q7;",
                   "    wire is_mode_write_access = cpu_access_edge && cpu_wr && !iwm_active &&\n"
                   "                                A[0] && write_mode_q6 && write_mode_q7 &&\n"
                   "                                !(A[3:1] == 3'b100 && A[0]);")
    text = replace(text, "            if (cpu_access_edge && bus_wr && !iwm_active && (bus_addr == 4'hF)) begin",
                   "            if (cpu_access_edge && bus_wr && !iwm_active && bus_addr[0] && immediate_q6 && immediate_q7 &&\n"
                   "                !(bus_addr[3:1] == 3'b100 && bus_addr[0])) begin")
    # The immediate mode view feeds the drives and the status register on
    # the write tick itself; it must agree with the mode-register decode.
    text = replace(text, "    wire mode_write_active = bus_wr && !iwm_active && immediate_q7 && immediate_q6 && bus_addr[0];",
                   "    wire mode_write_active = bus_wr && !iwm_active && immediate_q7 && immediate_q6 && bus_addr[0] &&\n"
                   "                             !(bus_addr[3:1] == 3'b100);")
    # Export media presence of the drive software has selected: $C031 bit 6
    # picks the family and $C0EA/$C0EB the drive. The machine gates its 2 ms
    # IWM access hold on it, so polls of an empty drive stay accelerated.
    text = replace(text, "    output          DRIVE35_EJECT_REQ\n);",
                   "    output          DRIVE35_EJECT_REQ,\n\n"
                   "    // Media present in the selected drive ($C031 bit 6 family, drive select)\n"
                   "    output          SELECTED_MEDIA\n);")
    text = replace(text, "    // Disk mounted status\n",
                   "    // Disk mounted status\n"
                   "    assign SELECTED_MEDIA = is_35_inch ? (drive_sel ? DISK_READY[3] : DISK_READY[2])\n"
                   "                                       : (drive_sel ? DISK_READY[1] : DISK_READY[0]);\n")
    return normalize(text)


def outputs() -> dict[str, str]:
    return {"iigs.sv": generate_iigs(), "vgc.sv": generate_vgc(), "adb.sv": generate_adb(),
            "hdd.sv": generate_hdd(), "prtc.sv": generate_prtc(), "bram.sv": generate_bram(),
            "runtime_rom.sv": generate_runtime_rom(), "twgs_card.sv": normalize(generate_twgs()),
            "woz_floppy_controller.sv": generate_woz(), "iir_filter.sv": generate_iir(),
            "es5503.sv": generate_es5503(), "rxuart.sv": generate_uart("rxuart"), "txuart.sv": generate_uart("txuart"),
            "P65C816.sv": generate_cpu(), "mcode.sv": generate_mcode(),
            "zipgs_regs.sv": generate_zipgs(), "sound.sv": generate_sound(), "soundglu.sv": generate_soundglu(),
            "flux_drive.sv": generate_flux_drive(), "iwm_flux.sv": generate_iwm_flux(),
            "iwm_woz.sv": generate_iwm_woz()}

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify committed ports reproduce exactly")
    parser.add_argument("--build-hdd-rom", action="store_true", help="assemble source-built SmartPort controller firmware with ACME")
    parser.add_argument("--acme", default="C:/Users/hasse/tools/acme/acme.exe" if sys.platform=="win32" else "acme")
    parser.add_argument("--asset-dir", type=Path, default=ROOT / "build/apple-iigs-assets")
    args = parser.parse_args()
    generated = outputs()
    PORTS.mkdir(parents=True, exist_ok=True)
    for name, text in generated.items():
        target = PORTS / name
        if args.check:
            existing = target.read_text(encoding="utf-8") if target.exists() else ""
            if existing != text:
                sys.stderr.writelines(difflib.unified_diff(existing.splitlines(True), text.splitlines(True), fromfile=str(target), tofile="reproduced"))
                raise ValueError(f"IIgs port is not reproducible: {name}")
        else:
            target.write_text(text, encoding="utf-8", newline="\n")
    if args.build_hdd_rom:
        from build_apple_iigs_smartport_rom import build
        print(f"SmartPort controller ROM assembled: {build(args.asset_dir, args.acme)}")
    print(f"Apple IIgs ports: {len(generated)} files {'verified' if args.check else 'reproduced'}")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
