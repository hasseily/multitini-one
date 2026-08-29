// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Minimal PAL/NTSC build-time adapter around the pinned FPGA64 machine core.
// Runtime clock switching, scaling, cartridges, PRG injection, and drive media
// are separate services and are intentionally not implied by this boundary.
module multitini_c64_adapter #(
    parameter bit NTSC_MODE = 1'b0,
    parameter integer AUDIO_SAMPLE_HZ = 48000
) (
    input  logic        core_clk,
    input  logic        reset,
    input  logic        core_reset_request,
    input  logic        clear_load_error,

    input  logic [15:0] input_code,
    input  logic [15:0] input_value,
    input  logic        input_valid,
    output logic        input_ready,

    input  logic [7:0]  load_data,
    input  logic        load_valid,
    output logic        load_ready,
    input  logic        load_first,
    input  logic        load_last,
    input  logic [7:0]  load_kind,
    input  logic [31:0] load_offset,
    input  logic [31:0] load_total,

    output logic [31:0] core_id,
    output logic        ready,
    output logic        load_error,
    output logic [2:0]  rom_loaded,

    output logic [7:0]  video_r,
    output logic [7:0]  video_g,
    output logic [7:0]  video_b,
    output logic        video_de,
    output logic        video_hsync,
    output logic        video_vsync,
    output logic        video_valid,

    output logic signed [23:0] audio_l,
    output logic signed [23:0] audio_r,
    output logic               audio_valid
);
    import multitini_c64_defs_pkg::*;

    localparam integer CORE_CLOCK_HZ = NTSC_MODE ? MT_C64_NTSC_HZ : MT_C64_PAL_HZ;

    logic        rom_wr;
    logic        rom_bank;
    logic [13:0] rom_addr;
    logic [7:0]  rom_data;
    logic        roms_ready;
    logic        load_active;
    logic        core_enable;
    logic        core_reset_n;

    logic [10:0] ps2_key;
    logic [6:0]  joy_a;
    logic [6:0]  joy_b;

    logic [15:0] ram_addr;
    logic [7:0]  ram_din = 8'd0;
    logic [7:0]  ram_dout;
    logic        ram_ce;
    logic        ram_we;
    logic [7:0]  main_ram [0:65535];

    logic        raw_hsync;
    logic        raw_vsync;
    logic [7:0]  raw_r;
    logic [7:0]  raw_g;
    logic [7:0]  raw_b;
    logic        sync_hsync;
    logic        sync_vsync;
    logic        hblank;
    logic        vblank;
    logic        sync_pixel_tick;
    logic        sync_pixel_tick_d;

    logic [17:0] raw_audio_l;
    logic [17:0] raw_audio_r;
    integer unsigned audio_phase;

    assign core_id = MT_C64_CORE_ID;
    assign ready = core_enable && !reset && !core_reset_request &&
                   !load_valid && !load_active;
    assign core_reset_n = ready;

    multitini_c64_loader loader_i (
        .clk(core_clk), .reset(reset), .clear_error(clear_load_error),
        .load_data(load_data), .load_valid(load_valid), .load_ready(load_ready),
        .load_first(load_first), .load_last(load_last), .load_kind(load_kind),
        .load_offset(load_offset), .load_total(load_total),
        .rom_wr(rom_wr), .rom_bank(rom_bank), .rom_addr(rom_addr),
        .rom_data(rom_data), .rom_loaded(rom_loaded),
        .roms_ready(roms_ready), .active(load_active), .error(load_error)
    );

    multitini_c64_input input_i (
        .clk(core_clk), .reset(!core_reset_n),
        .input_code(input_code), .input_value(input_value),
        .input_valid(input_valid), .input_ready(input_ready),
        .ps2_key(ps2_key), .joy_a(joy_a), .joy_b(joy_b)
    );

    // Delay reset release until the final registered ROM write has reached
    // the VHDL memory. Any replacement ROM object immediately stops the core.
    always_ff @(posedge core_clk) begin
        if (reset || core_reset_request || !roms_ready || load_active ||
            load_valid || rom_wr)
            core_enable <= 1'b0;
        else
            core_enable <= 1'b1;
    end

    // One synchronous byte-wide 64 KiB store matches the FPGA64 bus latency.
    // RAM contents are deliberately not initialized or cleared as ROM data.
    always_ff @(posedge core_clk) begin
        if (ram_ce) begin
            if (ram_we) begin
                main_ram[ram_addr] <= ram_dout;
                ram_din <= ram_dout;
            end else begin
                ram_din <= main_ram[ram_addr];
            end
        end
    end

    fpga64_sid_iec core_i (
        .clk32(core_clk), .reset_n(core_reset_n),
        .bios({1'b0, rom_bank}), .pause(1'b0), .pause_out(),
        .ps2_key(ps2_key), .kbd_reset(!core_reset_n), .shift_mod(2'b11),
        .ramAddr(ram_addr), .ramDin(ram_din), .ramDout(ram_dout),
        .ramCE(ram_ce), .ramWE(ram_we),
        .io_cycle(), .ext_cycle(), .refresh(),
        .cia_mode(1'b0), .turbo_mode(2'b00), .turbo_speed(2'b00),
        .vic_variant(2'b00), .ntscMode(NTSC_MODE),
        .hsync(raw_hsync), .vsync(raw_vsync), .palette(3'b000),
        .r(raw_r), .g(raw_g), .b(raw_b),
        .game(1'b1), .exrom(1'b1), .io_rom(1'b0), .io_ext(1'b0),
        .io_data(8'hFF), .irq_n(1'b1), .nmi_n(1'b1), .nmi_ack(),
        .romL(), .romH(), .UMAXromH(), .IOE(), .IOF(),
        .freeze_key(), .mod_key(), .tape_play(), .tape_rew(),
        .tape_stop(), .tape_ff(), .tape_reset_counter(),
        .dma_req(1'b0), .dma_cycle(), .dma_addr(16'd0),
        .dma_dout(8'd0), .dma_din(), .dma_we(1'b0), .irq_ext_n(1'b1),
        .joyA(joy_a), .joyB(joy_b),
        .pot1(8'hFF), .pot2(8'hFF), .pot3(8'hFF), .pot4(8'hFF),
        .audio_l(raw_audio_l), .audio_r(raw_audio_r),
        .sid_filter(2'b11), .sid_ver(2'b11), .sid_mode(3'b000),
        .sid_cfg(4'b0000), .sid_fc_off_l(13'd0), .sid_fc_off_r(13'd0),
        .sid_ld_clk(core_clk), .sid_ld_addr(12'd0), .sid_ld_data(16'd0),
        .sid_ld_wr(1'b0), .sid_digifix(1'b0),
        .pb_i(8'hFF), .pb_o(), .pa2_i(1'b1), .pa2_o(), .pc2_n_o(),
        .flag2_n_i(1'b1), .sp2_i(1'b1), .sp2_o(),
        .sp1_i(1'b1), .sp1_o(), .cnt2_i(1'b1), .cnt2_o(),
        .cnt1_i(1'b1), .cnt1_o(),
        .iec_data_o(), .iec_data_i(1'b1), .iec_clk_o(),
        .iec_clk_i(1'b1), .iec_atn_o(),
        .c64rom_addr(rom_addr), .c64rom_data(rom_data), .c64rom_wr(rom_wr),
        .cass_motor(), .cass_write(), .cass_sense(1'b1), .cass_read(1'b1)
    );

    video_sync sync_i (
        .clk32(core_clk), .pause(!core_reset_n),
        .hsync(raw_hsync), .vsync(raw_vsync), .ntsc(NTSC_MODE),
        .wide(1'b0), .hsync_out(sync_hsync), .vsync_out(sync_vsync),
        .hblank(hblank), .vblank(vblank), .pixel_tick(sync_pixel_tick)
    );

    // video_sync locks its divide-by-four pixel phase to the VIC hsync. Wait
    // one complete core-clock edge after that tick before registering the
    // entire video tuple, so RGB, blanking, and sync all describe one stable
    // source pixel when video_valid is asserted.
    always_ff @(posedge core_clk) begin
        if (!core_reset_n) begin
            sync_pixel_tick_d <= 1'b0;
            video_r           <= 8'd0;
            video_g           <= 8'd0;
            video_b           <= 8'd0;
            video_de          <= 1'b0;
            video_hsync       <= 1'b0;
            video_vsync       <= 1'b0;
            video_valid       <= 1'b0;
        end else begin
            sync_pixel_tick_d <= sync_pixel_tick;
            video_valid       <= sync_pixel_tick_d;
            if (sync_pixel_tick_d) begin
                video_r     <= raw_r;
                video_g     <= raw_g;
                video_b     <= raw_b;
                video_de    <= !hblank && !vblank;
                video_hsync <= sync_hsync;
                video_vsync <= sync_vsync;
            end
        end
    end

    // Sample the signed SID outputs at 48 kHz without assuming an integer
    // relationship between the authentic PAL/NTSC core clock and audio rate.
    always_ff @(posedge core_clk) begin
        audio_valid <= 1'b0;
        if (!core_reset_n) begin
            audio_phase <= 0;
            audio_l <= 24'sd0;
            audio_r <= 24'sd0;
        end else if (audio_phase + AUDIO_SAMPLE_HZ >= CORE_CLOCK_HZ) begin
            audio_phase <= audio_phase + AUDIO_SAMPLE_HZ - CORE_CLOCK_HZ;
            audio_l <= $signed({{6{raw_audio_l[17]}}, raw_audio_l});
            audio_r <= $signed({{6{raw_audio_r[17]}}, raw_audio_r});
            audio_valid <= 1'b1;
        end else begin
            audio_phase <= audio_phase + AUDIO_SAMPLE_HZ;
        end
    end
endmodule
