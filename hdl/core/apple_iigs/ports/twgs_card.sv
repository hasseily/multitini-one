// SPDX-License-Identifier: GPL-3.0-only
// Multitini port, 2026-10-14: reproduced by scripts/port_apple_iigs_core.py.
// Interface retained; initial Multitini accelerator has no TWGS firmware.
// This absent-card stub consumes no ROM or X2444 state and never overlays RAM.
module twgs_card (
    input  wire        clk,          // CLK_14M
    input  wire        reset,
    input  wire        enable,       // OSD: TWGS present

    input  wire [7:0]  bank,         // bank_bef
    input  wire [15:0] addr,         // addr_bef
    input  wire        we,
    input  wire        phi2,         // one pulse per CPU cycle
    input  wire        vda,          // CPU valid-data-address (this cycle)
    input  wire        vpa,          // CPU valid-program-address
    input  wire [7:0]  wr_data,      // dout (CPU write data)
    input  wire        cyareg7,      // CYAREG $C036 bit 7 (Fast)
    input  wire [2:0]  turbo_code,   // OSD turbo ceiling (host_speed); step when accel
    input  wire        host_irq_en,  // OSD AppleTalk/IRQ delay -> $BC0000 bit3 (inverted)
    input  wire        host_gfx_en,  // OSD Startup Graphics -> NVRAM word2 bit2
    input  wire        host_snd_en,  // OSD Startup Sound    -> NVRAM word2 bit3
    output wire        gfx_en,       // live NVRAM config bits, for the OSD mirror
    output wire        snd_en,

    // Tier C reset overlay: while armed (from reset until execution reaches
    // bank $BC), bank-0 reads of $F800-$FFFF serve the card ROM's top 2 KB --
    // the CPU's reset vector fetch lands on the firmware's $BCFFFC vector
    // (LFB20: JMPL $BCFA9A), and the JMPL moves execution into bank $BC where
    // the normal ROM mapping takes over. See twgs.3.s LFA9A/LFB07.
    input  wire        boot_armed,

    // Phase 2 (CDA install): while nmi_armed, serve ONLY the native NMI
    // vector ($00FFEA/B -> LFB24) and its 4-byte JMPL ($00FB24-27) from the
    // card ROM -- a tight window so runtime bank-0 language-card traffic is
    // untouched (unlike the broad reset overlay, nothing else is remapped).
    input  wire        nmi_armed,

    output wire        sel,          // 1 = TWGS card space -> overlay cpu_din
    output wire [7:0]  dout,         // read data for this access
    output wire        accel_en,     // TWGS acceleration engaged
    output wire [2:0]  speed_code,   // 0 native .. 3 = 7.16 MHz
    output wire [2:0]  cfg_speed_code,// configured speed (ignores CYAREG.7) for the OSD mirror
    output wire        cache_enable, // $BC0000.1 (advisory)
    output wire        irq_logic_en, // ~$BC0000.3 (advisory)

    // X2444 NVRAM backup port (byte view; MiSTer SD save/load)
    input  wire [4:0]  bk_addr,
    input  wire        bk_wr,
    input  wire [7:0]  bk_data,
    output wire [7:0]  bk_q
);

    assign sel=0, dout=8'hFF, accel_en=0, speed_code=0, cfg_speed_code=0;
    assign cache_enable=0, irq_logic_en=0, gfx_en=0, snd_en=0, bk_q=8'hFF;
endmodule
