//============================================================================
//  MMU -- bank/shadow/language-card address decode
//
//  Copyright (c) Pierre Cornier
//  Copyright (c) Steven A. Wilson
//  Copyright (c) Alan Steremberg
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 3 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
//  more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program. If not, see <https://www.gnu.org/licenses/>.
//============================================================================

// Apple IIgs memory decode. Pure combinational translation of a logical bus
// access and the current soft switches. Reference: KEGS 1.38 moremem.c.
// See docs/APPLE_IIGS_MMU_AUDIT.md for the reference policy and regressions.

// Multitini port, 2026-10-14: reproduced by scripts/port_apple_iigs_core.py.
module mmu #(
    parameter RAMSIZE = 128           // number of 64K fast-RAM banks
)(
    // Logical address (CPU view, post HDD-DMA mux, pre-translation)
    input  wire [7:0]  bank_log,
    input  wire [15:0] addr_log,
    input  wire        we,            // 1 = write cycle
    input  wire        vpb_n,         // 65C816 vector pull, active low
    input  wire        rom_select,    // 1 = ROM1 build (no TEXT2 shadow)

    // Soft-switch / register state
    input  wire        LCRAM2,        // LC $Dxxx bank 2 selected
    input  wire        RDROM,         // LC reads come from ROM
    input  wire        LC_WE,         // LC write enable
    input  wire [7:0]  shadow,        // $C035 shadow register (bits are inhibits)
    input  wire        ALTZP,
    input  wire        RAMRD,
    input  wire        RAMWRT,
    input  wire        STORE80,
    input  wire        PAGE2,
    input  wire        HIRES_MODE,
    input  wire        bank_latch,    // NEWVIDEO[0]: 1 = E1 distinct from E0
    input  wire        shadow_all,    // CYAREG[4]: shadow video pages in all banks
    input  wire [7:0]  SLTROMSEL,     // $C02D slot ROM select
    input  wire        INTCXROM,
    input  wire        SLOTC3ROM,
    input  wire        INTC8ROM,

    // Simulation debug only (referenced by `ifdef debug $display; unused in synthesis)
    input  wire [7:0]  dbg_wdata,
    input  wire [23:0] dbg_cpu_addr,

    // Physical address & decode
    output wire [23:0] addr_bus,      // translated physical address
    output reg         aux,           // main/aux select (ORed into bank bit 0)
    output wire        lcram2_sel,    // LC RAM window active (feeds inhibit_cxxx)
    output wire        inhibit_cxxx,  // suppress $Cxxx slot/ROM decode
    output wire        IO,            // internal I/O space $C000-$C0FF
    output wire        EXTERNAL_IO,   // slot I/O $C090-$C0FF routed off-chip
    output wire        is_internal_io,
    output wire        is_internal,   // $CNxx slot ROM mapped to internal firmware
    output reg         fastram_ce,
    output reg         slowram_ce,
    output wire        slowram_we,
    output wire        rom1_ce,       // bank FE
    output wire        rom2_ce,       // bank FF + IIe-style windows in bank 00
    output wire        romc_ce,       // bank FC
    output wire        romd_ce,       // bank FD
    output wire        slot_ce,
    output wire        slot_internalrom_ce,
    output wire        rom_writethrough
);

  wire low_bank = bank_log == 8'h00 || bank_log == 8'h01;
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
