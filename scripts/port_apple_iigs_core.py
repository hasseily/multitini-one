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
    return normalize(mmu_state_iigs(external_cpu_port(text)))


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
// Honor the guest's timing requests only for the mounted drive it selects.
// Sony entry code requests fast speed; its long delay helpers clear C036.7.
// Disk II software can instead request motor-based 1 MHz timing via C036.2.
wire floppy_guest_native_request = iwm_selected_media &&
    (!CYAREG[7] || (!DISK35[6] && CYAREG[2] && iwm_motor_command));
// The actual routed write protocol can outlive a Sony C031 deselection.
wire floppy_timing_hold = EXTERNAL_CPU &&
    (floppy_guest_native_request || iwm_write_timing);
""")
    text=replace(text,"  iwm_woz iwmc (\n", "  // Media/command/write state stays in the native IWM clock domain.\n  wire iwm_drive_media, iwm_motor_command, iwm_write_timing;\n  iwm_woz iwmc (\n")
    text=replace(text,"      .DRIVE35_EJECT_REQ(drive35_eject_req)\n  );", "      .DRIVE35_EJECT_REQ(drive35_eject_req),\n      .SELECTED_MEDIA(iwm_drive_media),\n      .MOTOR_COMMAND_ON(iwm_motor_command),\n      .WRITE_TIMING_ACTIVE(iwm_write_timing)\n  );")
    text=replace(text,"                          && iwm_holdoff == 15'd0\n",
                 "                          && (iwm_holdoff == 15'd0 || (EXTERNAL_CPU && !iwm_selected_media))\n")
    # IIgs paddle reset is C070 only; C071-C07F are IRQ dispatch ROM.
    # Match the native read/write decode, including its full 12-bit selector.
    text=replace(text,"wire acc_ptrig = bus_phi2 && IO && (addr_bef[7:4] == 4'h7);    // $C070-$C07F paddle trigger",
                 "wire acc_ptrig = bus_phi2 && IO && (addr_bef[11:0] == 12'h070); // IIgs paddle trigger only")
    # Raw IWM motor state can remain on after probing an empty drive. Keep it
    # intact for status and dirty flushing, but hold the accelerator only when
    # that drive family actually contains media. The Sony spindle can remain
    # on while its bus is deselected; an external CPU must resume acceleration
    # in that idle window. IWM access delay is separate.
    text=replace(text,"                          && !floppy_motor_on && !floppy35_motor_on",
                 "                          && !(floppy_motor_on && (|DISK_READY[1:0]) && !EXTERNAL_CPU)\n"
                 "                          && !(floppy35_motor_on && (|DISK_READY[3:2]) && !EXTERNAL_CPU)\n"
                 "                          && !floppy_timing_hold")
    text=replace(text,"// ZipGS IWM compatibility hold-off: a real accelerator drops to native speed\n"
                 "// around floppy activity -- the IWM and the ROM disk drivers' cycle-counted\n"
                 "// delays (spin-up, head settle) cannot be accelerated. Any CPU access to the\n"
                 "// IWM ($C0E0-$C0EF) arms a ~2 ms native window, refreshed per access, so the\n"
                 "// whole driver runs at the speed it was written for while the disk is being\n"
                 "// worked, and acceleration resumes between disk phases. (Without this the\n"
                 "// ROM3 3.5\" driver mis-times the drive at 7.16 MHz: \"Check startup device\".)",
                 "// IWM CPU timing compatibility: controller and flux clocks stay fixed.\n"
                 "// Cycle-counted software delays and byte/command protocols need native\n"
                 "// CPU pacing. An IWM access ($C0E0-$C0EF) arms a retriggerable two-ms\n"
                 "// guard for nearby protocol work; the external CPU can accelerate other\n"
                 "// computation when the guest and write engine no longer request native\n"
                 "// timing. No driver PC or ROM revision is hardcoded in this policy.")
    text=replace(text,"// (rtl/iwm_woz.v) already track real motor state; gate on those too so\n"
                 "// acceleration stays off for the drive's whole motor-on span, not just the\n"
                 "// last 2 ms of register activity.",
                 "// (rtl/iwm_woz.v) retain that whole-motor policy on the original CPU path.\n"
                 "// The external CPU instead honors the selected mounted drive's C036\n"
                 "// native-speed request and its active write protocol. The two-ms access\n"
                 "// guard still protects nearby IWM traffic. Ordinary computation can run\n"
                 "// fast between those intervals while the spindle continues rotating. Raw\n"
                 "// motor, mechanical/flux clocks and dirty flushing are unchanged.")
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
    # KEGS adb_kbd_talk_reg0 always completes, including an empty keyboard.
    # adb_response_packet(0, ...) emits $80; C027 DATA_VALID and data IRQ
    # depend on that header independently of the payload count.
    # https://github.com/a2kegs/kegs/blob/main/src/adb.c
    text = replace(text, "// IRQ generation\nwire data_irq = data_int & (pending_data > 0);",
                   "// A response header is valid data even when it has no payload.\n"
                   "// Both C027 status paths and enabled data IRQ include that header.\n"
                   "wire data_irq = data_int & (cmd_response_ready | (pending_data > 0));")
    text = replace(text, "(kbd_autopoll_off & data_int & (pending_data > 3'd0))",
                   "(kbd_autopoll_off & data_irq)")
    text = replace(text, "(pending_data > 0) | kbd_srq_pending | desk_mgr_pending,",
                   "cmd_response_ready | (pending_data > 0) | kbd_srq_pending | desk_mgr_pending,", 2)
    text = replace(text, "c026_status_read_with_data", "c026_response_read", 9)
    text = replace(text, "reg c026_response_read;  // Track if C026 status was read with pending data (for IDLE->DATA transition)",
                   "reg c026_response_read;  // Acknowledge a C026 response header after its read strobe falls")
    text = replace(text, """                  // Clear on rising edge only
                  if (strobe & ~strobe_prev) begin
                    cmd_response_ready <= 1'b0;  // Clear after returning status
`ifdef DEBUG_ADB
                    $display("ADB C026: returning 0x80, no data");
`endif
                  end""", """                  // Keep the header visible throughout the CPU read, including
                  // late combinational sampling by the external bus bridge.
                  if (strobe & (addr == 8'h26)) c026_response_read <= 1'b1;""")
    text = replace(text, """                  end else begin
                    // No queued data / device not present: send NO response so the
                    // firmware moves on to poll other devices (per gsplus).
                    state <= IDLE;
                  end
                end
                default: begin""", """                  end else if (din[3:0] == 4'd2) begin
                    // An empty keyboard TALK still completes at the IIgs host
                    // interface with header $80 and no payload (KEGS adb.c).
                    // Raw ADB clients use this completion to allow another TALK.
                    pending_data <= 3'd0;
                    cmd_response_ready <= 1'b1;
                    // A simultaneous key arrival must keep its newly set SRQ.
                    state <= IDLE;
                  end else begin
                    // Other empty devices and absent addresses retain their
                    // existing no-response behavior.
                    state <= IDLE;
                  end
                end
                default: begin""")
    text = replace(text, """    if (~strobe & strobe_prev & c026_response_read) begin
      state <= DATA;""", """    if (~strobe & strobe_prev & c026_response_read) begin
      state <= (pending_data > 3'd0) ? DATA : IDLE;""")
    text = replace(text, """    // Clear cmd_response_ready when no pending data (after status byte was read)
    // This handles the case where there's no data to return after the status byte
    if (~strobe & strobe_prev & cmd_response_ready & (pending_data == 3'd0) & (state == IDLE)) begin
      cmd_response_ready <= 1'b0;
    end

""", "")
    text = replace(text, "//   (b) data_int & pending_data>0 — a TALK-R0 RESPONSE is waiting to be read.",
                   "//   (b) data_int & (header or payload) — a TALK-R0 RESPONSE is waiting.")
    text = replace(text, "// reads the response (pending_data -> 0) and is gated on data_int (the firmware's",
                   "// reads the header and payload, and is gated on data_int (the firmware's")
    text = replace(text, "    // IDLE->DATA transition: on the strobe FALLING edge after the response\n"
                        "    // header was read (flag set in the C026 IDLE read block). Doing this on the",
                   "    // Acknowledge the header on its read strobe FALLING edge, entering DATA\n"
                   "    // only when payload bytes remain. Doing this on the")
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
                   "    output          SELECTED_MEDIA,\n"
                   "    // Timing policy observes command state, never alters drive behavior.\n"
                   "    output          MOTOR_COMMAND_ON,\n"
                   "    output reg      WRITE_TIMING_ACTIVE\n);")
    text = replace(text, "    // Disk mounted status\n",
                   "    // Disk mounted status\n"
                   "    assign SELECTED_MEDIA = is_35_inch ? (drive_sel ? DISK_READY[3] : DISK_READY[2])\n"
                   "                                       : (drive_sel ? DISK_READY[1] : DISK_READY[0]);\n"
                   "    assign MOTOR_COMMAND_ON = drive_on;\n"
                   "    // Use actual routed media: Sony cleanup may clear DISK35 before\n"
                   "    // the write protocol ends. SmartPort packet writes are excluded\n"
                   "    // by disk_write_mode itself.\n"
                   "    // Publish a registered guard: immediate mode decode consumes the\n"
                   "    // current bus cycle and must not feed the divider combinationally.\n"
                   "    // The access lease covers this one-native-edge entry latency.\n"
                   "    always @(posedge CLK_14M) begin\n"
                   "        if (RESET) WRITE_TIMING_ACTIVE <= 1'b0;\n"
                   "        else WRITE_TIMING_ACTIVE <= disk_write_mode && disk_mounted;\n"
                   "    end\n")
    return normalize(text)


def enable_clocked_blocks(text: str, clock: str, enable: str) -> str:
    """Retain pinned sequential logic, adding a primary-clock CE around it.

    Token positions are kept while stripping comments/strings for matching.
    Async reset remains live without CE; synchronous reset keeps its old edge.
    """
    masked = re.sub(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"',
                    lambda m: ' ' * len(m[0]), text)
    edits = []
    pattern = re.compile(r'always\s*@\s*\(\s*posedge\s+' + re.escape(clock) +
                         r'(?P<async>\s+or\s+posedge\s+(?P<reset>\w+))?\s*\)\s*begin\b')
    for match in pattern.finditer(masked):
        depth = 1
        for token in re.finditer(r'\bbegin\b|\bend\b', masked[match.end():]):
            depth += 1 if token[0] == 'begin' else -1
            if depth == 0:
                finish = match.end() + token.start()
                break
        else:
            raise ValueError(f'unclosed sequential block on {clock}')
        if match['async']:
            # Keep the asynchronous reset as the first hardware condition.
            # Wrapping it in (CE || RESET) gives Vivado a different async
            # set/reset structure and can infer extra latches/FDPEs.
            reset_match = re.search(r'\bif\s*\(\s*' + re.escape(match['reset']) +
                                    r'\s*\)\s*begin\b', masked[match.end():finish])
            if reset_match is None:
                raise ValueError(f'no explicit async reset branch on {clock}')
            reset_start = match.end() + reset_match.end()
            reset_depth = 1
            for token in re.finditer(r'\bbegin\b|\bend\b', masked[reset_start:finish]):
                reset_depth += 1 if token[0] == 'begin' else -1
                if reset_depth == 0:
                    reset_end = reset_start + token.end()
                    break
            else:
                raise ValueError(f'unclosed reset branch on {clock}')
            following = re.match(r'\s*else\s+(begin\b|if\b)', masked[reset_end:finish])
            if following is None:
                raise ValueError(f'no nonreset branch on {clock}')
            else_start = reset_end + following[0].index('else')
            if following[1]=='begin':
                edits.append((else_start + len('else'), f' if ({enable})'))
            else:
                edits.extend([(else_start + len('else'), f' if ({enable}) begin\n        '),
                              (finish, 'end\n    ')])
        else:
            edits.extend([(match.end(), f'\n        if ({enable}) begin'),
                          (finish, 'end\n    ')])
    if not edits:
        raise ValueError(f'no sequential blocks on {clock}')
    for position, addition in sorted(edits, reverse=True):
        text = text[:position] + addition + text[position:]
    return text


def generate_fast_floppy_iigs(text: str) -> str:
    text = replace(text, "input CPU_PUBLISH=1'b0,", """input CPU_PUBLISH=1'b0,
// Optional four-times-native floppy virtual time. Other peripherals retain CLK14.
input fast_floppy_request=1'b0,
input fast_floppy_stall=1'b0,
output fast_floppy_active,
output floppy_engine_ce,
output floppy_backend_ce,
output cpu_floppy_access,
output cpu_floppy_guard,
output cpu_floppy_slow,
// Captured Fast IWM bus and its separate registered-response path.
input CPU_FLOPPY_PENDING=1'b0,
input CPU_FLOPPY_REQUEST=1'b0,
input [7:0] CPU_FLOPPY_ADDR=8'd0,
input [7:0] CPU_FLOPPY_DATA=8'd0,
input CPU_FLOPPY_READ=1'b1,
output [7:0] cpu_floppy_data,
output [1:0] floppy_write_pending,
output floppy_write_drain,""")
    text = replace(text, "  // Media/command/write state stays in the native IWM clock domain.\n  wire iwm_drive_media, iwm_motor_command, iwm_write_timing;\n  iwm_woz iwmc (",
                   """  // The optional floppy island uses the primary clock and explicit enables.
  // The native enable preserves the original edge/BRAM sequence when disabled.
  wire iwm_drive_media, iwm_motor_command, iwm_write_timing, iwm_smartport_mode;
  // A native access already published before eligibility rises must still
  // commit once on its native edge, before any captured Fast bus can own it.
  wire iwm_native_pending = EXTERNAL_CPU && NATIVE_COMMIT && IO && addr_bef[7:4]==4'hE;
  wire iwm_bus_commit = CPU_FLOPPY_PENDING ? CPU_FLOPPY_REQUEST :
      (EXTERNAL_CPU ? (NATIVE_COMMIT && !hdd_dma && NATIVE_EDGE) : bus_phi2);
  wire iwm_bus_select = CPU_FLOPPY_PENDING ? 1'b1 :
      (fast_floppy_active ? (IO && addr_bef[7:4]==4'hE) : iwm_device_select);
  assign cpu_floppy_data = iwm_dout;
  // Finish a captured bus at its original rate before changing eligibility.
  // The CPU cannot start a SmartPort packet until this bounded access retires.
  wire fast_floppy_clock_active = (fast_floppy_active || CPU_FLOPPY_PENDING) && !iwm_native_pending;
  reg floppy_7m_phase;
  always @(posedge CPU_CLK) begin
    if (reset) floppy_7m_phase<=0;
    else if (!fast_floppy_clock_active) begin
      if (NATIVE_EDGE) floppy_7m_phase<=~clk_7M_en;
    end
    else if (floppy_engine_ce) floppy_7m_phase<=~floppy_7m_phase;
  end
  iwm_woz iwmc (""")
    # Only this instance is moved; every unrelated CLK14 consumer is retained.
    start = text.index('  iwm_woz iwmc (')
    end = text.index('\n  );', start) + len('\n  );')
    instance = text[start:end]
    instance = replace(instance, '.CLK_14M(CLK_14M),', '.CLK_14M(EXTERNAL_CPU ? CPU_CLK : CLK_14M),\n      .CLOCK_ENABLE(EXTERNAL_CPU ? floppy_engine_ce : 1\'b1),\n      .WRITE_DRAIN_ENABLE(floppy_write_drain),\n      .NATIVE_ENABLE(EXTERNAL_CPU ? NATIVE_EDGE : 1\'b1),')
    instance = replace(instance, '.CLK_7M_EN(clk_7M_en),', '.CLK_7M_EN(fast_floppy_clock_active ? floppy_7m_phase : clk_7M_en),')
    instance = replace(instance, '.PH2(bus_phi2),', '.PH2(iwm_bus_commit),')
    instance = replace(instance, '.DEVICE_SELECT(iwm_device_select),', '.DEVICE_SELECT(iwm_bus_select),')
    instance = replace(instance, '.WR_CYCLE(cpu_we_n),', '.WR_CYCLE(CPU_FLOPPY_PENDING ? CPU_FLOPPY_READ : cpu_we_n),')
    instance = replace(instance, '.A({8\'b0, cpu_addr[7:0]}),', '.A({8\'b0, CPU_FLOPPY_PENDING ? CPU_FLOPPY_ADDR : cpu_addr[7:0]}),')
    instance = replace(instance, '.D_IN(cpu_dout),', '.D_IN(CPU_FLOPPY_PENDING ? CPU_FLOPPY_DATA : cpu_dout),')
    instance = replace(instance, '.WRITE_TIMING_ACTIVE(iwm_write_timing)', '.WRITE_TIMING_ACTIVE(iwm_write_timing),\n      .SMARTPORT_MODE(iwm_smartport_mode),\n      .WRITE_PIPELINE_PENDING(floppy_write_pending)')
    text = text[:start] + instance + text[end:]
    text = replace(text, 'always @(posedge CLK_14M) begin\n  if (reset)\n    iwm_holdoff <= 15\'d0;',
                   "always @(posedge (EXTERNAL_CPU ? CPU_CLK : CLK_14M)) begin\n  if (reset)\n    iwm_holdoff <= 15'd0;")
    text = replace(text, "  else if (bus_phi2 && IO && addr_bef[7:4] == 4'hE)\n    iwm_holdoff <= 15'd28636;               // 2 ms at 14.318 MHz\n  else if (iwm_holdoff != 15'd0)\n    iwm_holdoff <= iwm_holdoff - 15'd1;",
                   """  else if (!EXTERNAL_CPU || floppy_engine_ce) begin
    if (iwm_bus_commit && IO && addr_bef[7:4] == 4'hE)
      iwm_holdoff <= 15'd28636; // Two milliseconds of floppy virtual time.
    else if (iwm_holdoff != 15'd0)
      iwm_holdoff <= iwm_holdoff - 15'd1;
  end""")
    text = replace(text, 'wire [3:0] fast_thresh =', """// SmartPort packets keep their native schedule and original bus bridge.
// Eligibility follows configured acceleration, independently of C036 requests.
assign fast_floppy_active = EXTERNAL_CPU && fast_floppy_request && accel_capable &&
    eff_accel_en && eff_speed_code>=3'd4 && !iwm_smartport_mode &&
    !io_slow_holdoff && !irq_slowdown && !twgs_boot_armed;
// A rate change cannot abandon an already accepted Fast write. Retain the
// primary-clock drain/fence only until its pending bit reaches track RAM.
reg floppy_fast_history;
always @(posedge CPU_CLK) begin
    if(reset)floppy_fast_history<=0;
    else if(fast_floppy_clock_active)floppy_fast_history<=1;
    else if(!(|floppy_write_pending))floppy_fast_history<=0;
end
assign floppy_write_drain=fast_floppy_clock_active || (floppy_fast_history && (|floppy_write_pending));
assign floppy_engine_ce = EXTERNAL_CPU && (fast_floppy_clock_active ? !fast_floppy_stall : NATIVE_EDGE);
assign floppy_backend_ce = EXTERNAL_CPU && (floppy_write_drain || NATIVE_EDGE);
assign cpu_floppy_access = fast_floppy_active && !hdd_dma && valid && IO && addr_bef[7:4]==4'hE;
assign cpu_floppy_guard = fast_floppy_active &&
    ((iwm_holdoff!=0 && iwm_selected_media) || floppy_timing_hold);
assign cpu_floppy_slow = !CYAREG[7] ||
    (iwm_selected_media && !DISK35[6] && CYAREG[2] && iwm_motor_command);
wire [3:0] fast_thresh =""")
    text = replace(text, "assign cpu_speed = accel_active ? eff_speed_code : 3'd0;",
                   "assign cpu_speed = (accel_active || cpu_floppy_guard || cpu_floppy_access) ? eff_speed_code : 3'd0;")
    # IWM responses use the adapter's captured reply; no live IWM data may
    # rejoin CPU_DIN after the combinational CPU-enable/cache decision.
    text = replace(text, "assign cpu_fast_cycle = !hdd_dma && (!valid || cpu_local_read ||",
                   "assign cpu_fast_cycle = !hdd_dma && (!valid || cpu_local_read || cpu_floppy_access ||")
    text = replace(text, '// IWM CPU timing compatibility: controller and flux clocks stay fixed.',
                   '// Normal floppy mode keeps the original fixed controller/flux timebase.')
    text = replace(text, '// motor, mechanical/flux clocks and dirty flushing are unchanged.',
                   '// motor, mechanical/flux clocks and dirty flushing are unchanged in Normal mode.\n'
                   '// Fast mode advances only the floppy island at four times native time,\n'
                   '// pacing driver CPU work with it and pausing virtual time on real stalls.')
    return normalize(text)


def generate_enabled_floppy(text: str, name: str) -> str:
    clock = 'clk' if name in ('woz_floppy_controller', 'smartport_dev') else 'CLK_14M'
    declaration = f"    input             {clock}," if name=='woz_floppy_controller' else (
        '    input  wire        clk,' if name=='smartport_dev' else (
        '    input           CLK_14M,' if name=='iwm_woz' else
        '    input  wire        CLK_14M,'))
    # Preserve each pinned source's spacing/comment after the clock token.
    if declaration not in text:
        raise ValueError(f'{name}: clock declaration not found')
    text = replace(text, declaration, declaration + "\n    input wire CLOCK_ENABLE=1'b1,")
    if name=='iwm_woz':
        text = replace(text, "    input wire CLOCK_ENABLE=1'b1,", "    input wire CLOCK_ENABLE=1'b1,\n    input wire WRITE_DRAIN_ENABLE=1'b0,\n    input wire NATIVE_ENABLE=1'b1,")
        text = replace(text, '    output reg      WRITE_TIMING_ACTIVE\n);', '    output reg      WRITE_TIMING_ACTIVE,\n    output SMARTPORT_MODE,\n    output [1:0] WRITE_PIPELINE_PENDING\n);')
        text = replace(text, '    assign MOTOR_COMMAND_ON = drive_on;', "    assign MOTOR_COMMAND_ON = drive_on;\n    // Registered mode only: current CPU strobe must not feed its own enable.\n    assign SMARTPORT_MODE = !mode_reg[3] && mode_reg[1];")
        text = replace(text, '    smartport_dev sp_dev (\n        .clk(CLK_14M),', '    smartport_dev sp_dev (\n        .clk(CLK_14M),\n        .CLOCK_ENABLE(NATIVE_ENABLE),')
        text = replace(text, '        .CLK_14M(CLK_14M),', '        .CLK_14M(CLK_14M),\n        .CLOCK_ENABLE(CLOCK_ENABLE),', 4)
        text = replace(text, '        .WRITE_BYTE_OUT(drive35_write_byte),', '        .WRITE_DRAIN_ENABLE(WRITE_DRAIN_ENABLE),\n        .WRITE_PIPELINE_PENDING(WRITE_PIPELINE_PENDING[0]),\n        .WRITE_BYTE_OUT(drive35_write_byte),')
        text = replace(text, '        .WRITE_BYTE_OUT(drive525_write_byte),', '        .WRITE_DRAIN_ENABLE(WRITE_DRAIN_ENABLE),\n        .WRITE_PIPELINE_PENDING(WRITE_PIPELINE_PENDING[1]),\n        .WRITE_BYTE_OUT(drive525_write_byte),')
    if name=='flux_drive':
        text = replace(text, "    input wire CLOCK_ENABLE=1'b1,", "    input wire CLOCK_ENABLE=1'b1,\n    input wire WRITE_DRAIN_ENABLE=1'b0,\n    output wire WRITE_PIPELINE_PENDING,")
        # An IWM strobe already emitted before a pause is a queued bit, not
        # a new time event. Admit it once while draining; mask it on resume
        # until the paused IWM itself has cleared its held output pulse.
        text = re.sub(r'\bWRITE_STROBE\b', 'accepted_write_strobe', text)
        text = replace(text, 'input  wire        accepted_write_strobe,', 'input  wire        WRITE_STROBE,')
    if name=='woz_floppy_controller':
        text = replace(text, "    input wire CLOCK_ENABLE=1'b1,", "    input wire CLOCK_ENABLE=1'b1,\n    input wire BIT_CLOCK_ENABLE=1'b1,\n    input wire WRITE_PENDING=1'b0,")
        text = text.replace(".enable_a(1'b1)", '.enable_a(CLOCK_ENABLE)').replace(".enable_b(1'b1)", '.enable_b(CLOCK_ENABLE)')
        # Track port B belongs to virtual mechanics. It must pause with its
        # RMW source while port A/controller continue servicing ARM and SD.
        text = replace(text, '.enable_b(CLOCK_ENABLE)', '.enable_b(CLOCK_ENABLE && BIT_CLOCK_ENABLE)', 3)
        # The first B port is metadata, read by the backend FSM rather than
        # the mechanics. Keep it running while a track is loading.
        text = text.replace('.enable_b(CLOCK_ENABLE && BIT_CLOCK_ENABLE)', '.enable_b(CLOCK_ENABLE)', 1)
        # Save/load admission is fenced until every already-emitted bit has
        # reached track RAM. In-flight SD service remains fully enabled.
        text = replace(text, '                S_IDLE: begin', '                S_IDLE: begin\n                    if (!WRITE_PENDING) begin')
        text = replace(text, '                // Lookup FLUX (if available) then TMAP, then TRKS',
                       '                    end // pending serial/RMW writes have drained\n\n                // Lookup FLUX (if available) then TMAP, then TRKS')
    if name=='smartport_dev':
        # The SD-facing block RAM has a separate write always block too.
        text = NOTICE + text
    text = enable_clocked_blocks(text, clock, 'CLOCK_ENABLE')
    if name=='flux_drive':
        text = replace(text, '    reg [31:0] write_count;  // Debug counter for writes\n', """    reg drain_strobe_seen,drain_phase_pending;
    wire accepted_write_strobe=WRITE_STROBE && !drain_strobe_seen;
    wire pending_strobe=accepted_write_strobe && WRITE_MODE && !WRITE_PROTECT && TRACK_LOADED;
    assign WRITE_PIPELINE_PENDING=pending_strobe || write_strobe_d1 || WRITE_WE_OUT;
    always @(posedge CLK_14M or posedge RESET) begin
        if(RESET)drain_strobe_seen<=0;
        else if(!WRITE_STROBE)drain_strobe_seen<=0;
        else if(WRITE_DRAIN_ENABLE && pending_strobe)drain_strobe_seen<=1;
    end
    reg [31:0] write_count;  // Debug counter for writes
""")
        a=text.index('    reg [31:0] write_count;  // Debug counter for writes')
        b=text.index('    // Head Stepper Motor Logic',a)
        rmw=replace(text[a:b],'end else if (CLOCK_ENABLE) begin',
                    'end else if (CLOCK_ENABLE || (WRITE_DRAIN_ENABLE && WRITE_PIPELINE_PENDING)) begin')
        text=text[:a]+rmw+text[b:]
        # Drain the already emitted serial bit's position/phase-lock event.
        # The rotation timers, flux playback, mechanics and IWM remain frozen.
        tail='''    end
    end

    reg [4:0] debug_read_count;  // Counter for track dump logging'''
        replacement='''    end else if (WRITE_DRAIN_ENABLE && pending_strobe && motor_spinning &&
                 !IS_FLUX_TRACK && !skip_position_advance) begin
        if (TRACK_BIT_COUNT>0 && effective_bit_position+1>=track_bit_count_17)
            bit_position<=0;
        else bit_position<=bit_position+1'b1;
        write_strobe_steal<=1;
        drain_phase_pending<=1;
    end
    end

    reg [4:0] debug_read_count;  // Counter for track dump logging'''
        text = replace(text, tail, replacement)
        text = replace(text, "            bit_position <= 17'd0;\n            bit_timer <= bit_cell_base;",
                       "            bit_position <= 17'd0;\n            drain_phase_pending<=0;\n            bit_timer <= bit_cell_base;")
        text = replace(text, '                        if (WRITE_MODE && accepted_write_strobe && !skip_position_advance) begin\n                        // Keep the physical bit-cell timer phase locked to the IWM\'s write',
                       '                        if (WRITE_MODE && (accepted_write_strobe || drain_phase_pending) && !skip_position_advance) begin\n                        drain_phase_pending<=0;\n                        // Keep the physical bit-cell timer phase locked to the IWM\'s write')
    return normalize(text)


def mmu_state_iigs(text: str) -> str:
    """Apply KEGS MMU state/retirement rules to the machine and its CPU peek."""
    text = replace(text, "    .SLTROMSEL(SLTROMSEL),",
                   "    .SLTROMSEL(SLTROMSEL),\n    .INTCXROM(INTCXROM),\n"
                   "    .SLOTC3ROM(SLOTC3ROM),\n    .INTC8ROM(INTC8ROM),", count=2)
    text = replace(text, "  logic               SLOTC3ROM;",
                   "  // KEGS keeps C00A/B and C02D bit 3 in the same slot byte.\n"
                   "  wire                SLOTC3ROM = SLTROMSEL[3];")
    text = text.replace("SLOTC3ROM<=", "SLTROMSEL[3]<=").replace("SLOTC3ROM <=", "SLTROMSEL[3] <=")
    start = text.index("      // C036 Speed Register initialization")
    end = text.index("      NEWVIDEO <=", start)
    text = text[:start] + """      // KEGS cold start: fast mode and ROM03 power-on status. A warm
      // reset preserves the complete speed register, including shadow-all.
      if (cold_reset) CYAREG <= rom_select ? 8'h80 : 8'hc0;
      shadow <= 8'h00;
      DMAREG <= 8'h00;
""" + text[end:]
    text = replace(text, "      INTCXROM<=0;", "      INTCXROM<=1;")
    text = replace(text, "      NEWVIDEO <= 8'h41;",
                   "      NEWVIDEO <= cold_reset ? 8'h41 : (NEWVIDEO & 8'h41);")
    text = replace(text, "NEWVIDEO <= dout;", "NEWVIDEO <= dout & 8'he1;", count=2)
    text = replace(text, "      TEXTCOLOR<='hf2;", "      TEXTCOLOR<=8'hf0;")
    text = replace(text, "      AN3<=0;",
                   "      AN3<=1;\n      TEXTG<=1;\n      MIXG<=0;\n      HIRES_MODE<=0;")
    text = replace(text,
        "            // These should ONLY respond to bank $00 and $E0 accesses, NOT bank $01/$E1\n"
        "            // This prevents MVN/MVP block moves through bank $01 I/O space from corrupting state",
        "            // All four mapped I/O aliases have identical side effects.")
    text, count = re.subn(r"(12'h00[0-9A-F]: begin )if \(bank_bef == 8'h00 \|\| bank_bef == 8'he0\) begin (.*?) end end",
                          r"\1\2 end", text)
    if count != 32:
        raise ValueError(f"Expected 32 guarded MMU soft switches, found {count}")
    text = replace(text, "CYAREG <= dout;",
                   "CYAREG <= (dout & 8'h9f) | ((!rom_select && !dout[5]) ? (dout & 8'h40) : 8'h00);", count=2)
    start = text.index("// A Cn access selects that slot's C800 firmware.")
    end = text.index("assign dbg_hdd_dma", start)
    text = text[:start] + """// C3 accesses latch internal C800 when slot 3 is internal. Other slots do
// not clear that latch. CFFF releases it after its read/write data phase.
// Slot 7's expansion firmware is a board extension, below internal priority.
reg INTC8ROM=1'b0;
reg hdd_expansion_owner=1'b0;
wire hdd_slot_bank=bank_bef==8'h00 || bank_bef==8'h01 || bank_bef==8'he0 || bank_bef==8'he1;
wire hdd_expansion_select=hdd_expansion_owner && hdd_slot_bank && !inhibit_cxxx &&
    !CXROM && !INTC8ROM && SLTROMSEL[7] && addr_bef>=16'hc800 && addr_bef<=16'hcfff;
always @(posedge CLK_14M) begin
    if(reset)begin
        INTC8ROM<=0;
        hdd_expansion_owner<=0;
    end else if(bus_phi2 && valid && !hdd_dma && hdd_slot_bank && !inhibit_cxxx)begin
        if(addr_bef==16'hcfff)begin
            INTC8ROM<=0;
            hdd_expansion_owner<=0;
        end else begin
            if(addr_bef[15:8]==8'hc3 && !SLOTC3ROM)INTC8ROM<=1;
            if(addr_bef>=16'hc100 && addr_bef<16'hc800)
                hdd_expansion_owner<=io_select[7];
        end
    end
end
""" + text[end:]
    text = replace(text, "wire cpu_plain_bus = !IO",
                   "// All mapped C1-CF accesses retire through the native bridge, including\n"
                   "// internal ROM reads. This clocks C3/CFFF ownership once at every speed.\n"
                   "wire cpu_mapped_slot_bus = hdd_slot_bank && !inhibit_cxxx &&\n"
                   "    addr_bef>=16'hc100 && addr_bef<=16'hcfff;\n"
                   "wire cpu_plain_bus = !cpu_mapped_slot_bus && !IO")
    text = replace(text, "// the primary-clock slow-RAM port. E0/E1 LC ROM reads use the ROM cache.",
                   "// the primary-clock slow-RAM port. KEGS E0/E1 LC windows always use RAM.")
    text = replace(text, "wire [7:0] din =\n",
                   "// KEGS zero-fills the unused FC/FD half of a ROM01 image.\n"
                   "wire reserved_rom01 = rom_select && (bank_bef==8'hfc || bank_bef==8'hfd);\n"
                   "wire [7:0] din =\n  reserved_rom01 ? 8'h00 :\n")
    text = replace(text,
                   "// Floating bus: reads from non-existent RAM banks return the bank number\n"
                   "wire [7:0] floating_bus = (bank_bef >= RAMSIZE && bank_bef < 8'hE0) ? bank_bef : 8'h80;",
                   "// KEGS returns zero for uninstalled memory; mapped empty I/O retains\n"
                   "// this core's existing bus value. The KEGS EF DOC alias is not exposed.\n"
                   "wire unmapped_bank = bank_bef >= RAMSIZE && bank_bef != 8'he0 &&\n"
                   "    bank_bef != 8'he1 && bank_bef < 8'hfc;\n"
                   "wire [7:0] floating_bus = unmapped_bank ? 8'h00 : 8'h80;")
    return text


def generate_mmu() -> str:
    text = upstream("rtl/mmu.sv")
    text = replace(text, "module mmu", NOTICE + "module mmu")
    # Keep the imported license/interface; implement the audited mapping as one
    # decoder. KEGS 1.38 is the selected compatibility authority; see the audit
    # for deliberate differences from GSSquared and unsupported KEGS switches.
    start = text.index("// ============================================================================")
    end = text.index(NOTICE, start)
    text = text[:start] + """// Apple IIgs memory decode. Pure combinational translation of a logical bus
// access and the current soft switches. Reference: KEGS 1.38 moremem.c.
// See docs/APPLE_IIGS_MMU_AUDIT.md for the reference policy and regressions.

""" + text[end:]
    text = replace(text, "    input  wire [7:0]  SLTROMSEL,     // $C02D slot ROM select",
                   "    input  wire [7:0]  SLTROMSEL,     // $C02D slot ROM select\n"
                   "    input  wire        INTCXROM,\n"
                   "    input  wire        SLOTC3ROM,\n"
                   "    input  wire        INTC8ROM,")
    text = text[:text.index("  // --------------------------------------------------------------------------")]
    return normalize(text + """  wire low_bank = bank_log == 8'h00 || bank_log == 8'h01;
  wire slow_bank = bank_log == 8'he0 || bank_log == 8'he1;
  wire iolc_bank = (low_bank && !shadow[6]) || slow_bank;
  wire mapped_cxxx = iolc_bank && addr_log[15:12] == 4'hc;
  wire flat_low = low_bank && shadow[6];

  // KEGS fixup_altzp/ramrd/ramwrt and fixup_bank0_{0400,2000}.
  // Higher banks never inherit the bank-00 aux switches. KEGS does not support
  // NEWVIDEO[0]=0; retain the existing direct E1 latch-off implementation.
  wire [7:0] bank_eff = (bank_log == 8'he1 && !bank_latch) ? 8'he0 : bank_log;
  wire switched_bank = bank_eff == 8'h00 || bank_eff == 8'he0;
  always_comb begin
    aux = 1'b0;
    if (switched_bank) begin
      if (addr_log < 16'h0200 || addr_log >= 16'hc000)
        // IOLC-inhibited Cxxx bypasses ALTZP (KEGS fixup_shadow_iolc).
        aux = ALTZP && !(flat_low && addr_log[15:12] == 4'hc);
      else if (STORE80 && (addr_log >= 16'h0400 && addr_log < 16'h0800 ||
               HIRES_MODE && addr_log >= 16'h2000 && addr_log < 16'h4000))
        aux = PAGE2;
      else
        aux = we ? RAMWRT : RAMRD;
    end else if (bank_eff == 8'h01 || bank_eff == 8'he1)
      aux = 1'b1;
  end
  wire [7:0] bank_phys = {bank_eff[7:1], bank_eff[0] | aux};

  // Keep KEGS's backing layout: LC bank 1 at Cxxx, bank 2 at Dxxx.
  // IOLC inhibit swaps C/D, exposing bank 2 at Cxxx and bank 1 at Dxxx.
  // E0/E1 never overlay ROM or write-protect RAM; RDROM still selects the
  // read-side Dxxx backing window in KEGS fixup_lc.
  wire lc_dxxx_fold = addr_log[15:12] == 4'hd &&
      (flat_low || (iolc_bank && !LCRAM2 &&
       (!RDROM || (we && (slow_bank || LC_WE)))));
  wire flat_cxxx_swap = flat_low && addr_log[15:12] == 4'hc;
  assign addr_bus = {bank_phys, addr_log[15:13],
                    addr_log[12] ^ (lc_dxxx_fold || flat_cxxx_swap), addr_log[11:0]};
  assign lcram2_sel = iolc_bank && addr_log >= 16'hd000 &&
                     (lc_dxxx_fold || !RDROM);
  assign inhibit_cxxx = lcram2_sel || flat_low;

  assign slowram_we = we;
  assign is_internal_io = !SLTROMSEL[addr_log[6:4]];
  assign EXTERNAL_IO = iolc_bank && addr_log >= 16'hc090 && addr_log < 16'hc100 && !is_internal_io;
  assign IO = iolc_bank && addr_log[15:8] == 8'hc0 && !EXTERNAL_IO;
  assign is_internal = INTCXROM ||
      ((addr_log[10:8] == 3'd3) ? !SLOTC3ROM : !SLTROMSEL[addr_log[10:8]]);
  wire slot_window = mapped_cxxx && addr_log >= 16'hc100 && addr_log < 16'hc800;
  assign slot_ce = slot_window && !is_internal;
  assign slot_internalrom_ce = slot_window && is_internal;

  // ROM 01 has no FC/FD payload. The machine returns the KEGS zero-filled
  // reserved-ROM value without accessing or aliasing the loaded FE/FF image.
  assign romc_ce = bank_log == 8'hfc && !rom_select;
  assign romd_ce = bank_log == 8'hfd && !rom_select;
  assign rom1_ce = bank_log == 8'hfe;
  assign rom2_ce = bank_log == 8'hff ||
      (low_bank && !shadow[6] && addr_log >= 16'hd000 && (RDROM || !vpb_n)) ||
      (mapped_cxxx && addr_log >= 16'hc800 && (INTCXROM || INTC8ROM)) ||
      (iolc_bank && addr_log >= 16'hc071 && addr_log <= 16'hc07f);
  assign rom_writethrough = low_bank && !shadow[6] && addr_log >= 16'hd000 && LC_WE && we;

  wire txt1_shadow = !shadow[0] && addr_log >= 16'h0400 && addr_log < 16'h0800;
  wire txt2_shadow = !rom_select && !shadow[5] && addr_log >= 16'h0800 && addr_log < 16'h0c00;
  wire hgr1_shadow = !shadow[1] && addr_log >= 16'h2000 && addr_log < 16'h4000;
  wire hgr2_shadow = !shadow[2] && addr_log >= 16'h4000 && addr_log < 16'h6000;
  wire shr_shadow = !shadow[3] && addr_log >= 16'h2000 && addr_log < 16'ha000;
  // Aux HGR requires both its page enable and AUXHGR, unless SHR overrides.
  // Test the resolved bank, including bank-00 RAMWRT/80STORE redirection.
  wire low_shadow = txt1_shadow || txt2_shadow ||
      (bank_phys[0] ? (shr_shadow || (!shadow[4] && (hgr1_shadow || hgr2_shadow))) :
                     (hgr1_shadow || hgr2_shadow));

  always_comb begin
    fastram_ce = 1'b0;
    slowram_ce = 1'b0;
    // Slot/ROM writes must not corrupt LC bank 1 behind the Cxxx window.
    // Test the logical address so a legitimate Dxxx->Cxxx LC write survives.
    if (!mapped_cxxx) begin
      if (slow_bank)
        slowram_ce = 1'b1;
      else if (low_bank) begin
        if (!(addr_log >= 16'hd000 && !shadow[6] &&
              (we ? !LC_WE : (RDROM || !vpb_n)))) begin
          fastram_ce = 1'b1;
          slowram_ce = we && low_shadow;
        end
      end else if (bank_log < RAMSIZE && bank_log < 8'he0) begin
        fastram_ce = 1'b1;
        // KEGS fixup_shadow_all_banks: installed odd banks 03+, SHR only.
        slowram_ce = we && shadow_all && bank_log[0] && shr_shadow;
      end
    end
  end

`ifdef VERILATOR
  always @(*) begin
    if (addr_bus[11:0] != addr_log[11:0])
      $fatal(1, "mmu: translation changed A[11:0]");
    if (addr_bus[15:13] != addr_log[15:13])
      $fatal(1, "mmu: translation changed A[15:13]");
    if (bank_phys[7:1] != bank_log[7:1])
      $fatal(1, "mmu: unexpected bank translation");
    if (fastram_ce && slow_bank)
      $fatal(1, "mmu: fast RAM enabled for Mega II bank");
    if (mapped_cxxx && (fastram_ce || slowram_ce))
      $fatal(1, "mmu: mapped Cxxx enabled RAM");
  end
`endif
endmodule
""")


def outputs() -> dict[str, str]:
    result = {"iigs.sv": generate_fast_floppy_iigs(generate_iigs()), "vgc.sv": generate_vgc(), "adb.sv": generate_adb(),
            "hdd.sv": generate_hdd(), "prtc.sv": generate_prtc(), "bram.sv": generate_bram(),
            "runtime_rom.sv": generate_runtime_rom(), "twgs_card.sv": normalize(generate_twgs()),
            "woz_floppy_controller.sv": generate_woz(), "iir_filter.sv": generate_iir(),
            "es5503.sv": generate_es5503(), "rxuart.sv": generate_uart("rxuart"), "txuart.sv": generate_uart("txuart"),
            "P65C816.sv": generate_cpu(), "mcode.sv": generate_mcode(),
            "zipgs_regs.sv": generate_zipgs(), "sound.sv": generate_sound(), "soundglu.sv": generate_soundglu(),
            "flux_drive.sv": generate_flux_drive(), "iwm_flux.sv": generate_iwm_flux(),
            "iwm_woz.sv": generate_iwm_woz(), "mmu.sv": generate_mmu(),
            "smartport_dev.sv": upstream('rtl/smartport_dev.v')}
    for name in ('iwm_woz','iwm_flux','flux_drive','woz_floppy_controller','smartport_dev'):
        result[name+'.sv']=generate_enabled_floppy(result[name+'.sv'],name)
    return result

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
