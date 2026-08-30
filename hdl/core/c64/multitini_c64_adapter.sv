// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// PAL/NTSC build-time adapter around the pinned FPGA64 machine core. The first
// drive boundary implements one read-only D64-backed serial-IEC 1541.
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

    input  logic        media_mount,
    input  logic        media_eject,
    input  logic        media_complete,
    input  logic [7:0]  media_slot,
    input  logic [7:0]  media_kind,
    input  logic        media_read_only,
    input  logic [31:0] media_image_size,
    input  logic [31:0] media_generation,
    input  logic [31:0] media_complete_request_id,
    input  logic [31:0] media_complete_status,
    input  logic [15:0] media_complete_block_count,
    input  logic [31:0] media_complete_generation,

    output logic        media_request_valid,
    output logic        media_request_write,
    output logic [31:0] media_request_id,
    output logic [31:0] media_request_lba,
    output logic [15:0] media_request_block_count,
    output logic [31:0] media_request_generation,

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
    logic        rom_load_valid;
    logic        rom_load_ready;
    logic        rom_load_error;
    logic        d64_load_valid;
    logic        d64_load_ready;
    logic        drive_media_error;
    logic        drive_rom_wr;
    logic [13:0] drive_rom_addr;
    logic [7:0]  drive_rom_data;
    logic        drive_rom_loaded;
    logic        core_enable;
    logic        core_reset_n;
    logic        drive_soft_reset;

    logic        c64_iec_data;
    logic        c64_iec_clk;
    logic        c64_iec_atn;
    logic        drive_iec_data;
    logic        drive_iec_clk;
    logic        drive_mounted;
    logic        drive_led;

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
    assign rom_load_valid = load_valid && load_kind != MT_LOAD_C64_D64;
    assign d64_load_valid = load_valid && load_kind == MT_LOAD_C64_D64;
    // The asynchronous mailbox presents its payload while valid is stalled, so
    // D64 bytes can apply drive-buffer backpressure without blocking a later
    // ROM object whose kind selects the ordinary loader instead.
    assign load_ready = load_kind == MT_LOAD_C64_D64 ?
                        d64_load_ready : rom_load_ready;
    assign load_error = rom_load_error || drive_media_error;
    assign ready = core_enable && !reset && !core_reset_request &&
                   !rom_load_valid && !load_active;
    assign core_reset_n = ready;
    assign drive_soft_reset = core_reset_request || !core_enable;

    multitini_c64_loader loader_i (
        .clk(core_clk), .reset(reset), .clear_error(clear_load_error),
        .load_data(load_data), .load_valid(rom_load_valid),
        .load_ready(rom_load_ready),
        .load_first(load_first), .load_last(load_last), .load_kind(load_kind),
        .load_offset(load_offset), .load_total(load_total),
        .rom_wr(rom_wr), .rom_bank(rom_bank), .rom_addr(rom_addr),
        .rom_data(rom_data), .rom_loaded(rom_loaded),
        .drive_rom_wr(drive_rom_wr), .drive_rom_addr(drive_rom_addr),
        .drive_rom_data(drive_rom_data),
        .drive_rom_loaded(drive_rom_loaded),
        .roms_ready(roms_ready), .active(load_active), .error(rom_load_error)
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
            rom_load_valid || rom_wr || drive_rom_wr)
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
        .iec_data_o(c64_iec_data), .iec_data_i(drive_iec_data),
        .iec_clk_o(c64_iec_clk), .iec_clk_i(drive_iec_clk),
        .iec_atn_o(c64_iec_atn),
        .c64rom_addr(rom_addr), .c64rom_data(rom_data), .c64rom_wr(rom_wr),
        .cass_motor(), .cass_write(), .cass_sense(1'b1), .cass_read(1'b1)
    );

    multitini_c64_1541 #(.CORE_CLOCK_HZ(CORE_CLOCK_HZ)) drive_i (
        .clk(core_clk), .reset(reset), .soft_reset(drive_soft_reset),
        .clear_error(clear_load_error),
        .drive_rom_write(drive_rom_wr),
        .drive_rom_address(drive_rom_addr),
        .drive_rom_write_data(drive_rom_data),
        .drive_rom_loaded(drive_rom_loaded),
        .c64_iec_atn(c64_iec_atn), .c64_iec_data(c64_iec_data),
        .c64_iec_clk(c64_iec_clk), .drive_iec_data(drive_iec_data),
        .drive_iec_clk(drive_iec_clk),
        .media_mount(media_mount), .media_eject(media_eject),
        .media_complete(media_complete), .media_slot(media_slot),
        .media_kind(media_kind), .media_read_only(media_read_only),
        .media_image_size(media_image_size),
        .media_generation(media_generation),
        .media_complete_request_id(media_complete_request_id),
        .media_complete_status(media_complete_status),
        .media_complete_block_count(media_complete_block_count),
        .media_complete_generation(media_complete_generation),
        .media_request_valid(media_request_valid),
        .media_request_write(media_request_write),
        .media_request_id(media_request_id),
        .media_request_lba(media_request_lba),
        .media_request_block_count(media_request_block_count),
        .media_request_generation(media_request_generation),
        .d64_load_data(load_data), .d64_load_valid(d64_load_valid),
        .d64_load_ready(d64_load_ready), .d64_load_first(load_first),
        .d64_load_last(load_last), .d64_load_offset(load_offset),
        .d64_load_total(load_total), .mounted(drive_mounted),
        .drive_led(drive_led), .error(drive_media_error)
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
            // MiSTer consumes bits 17:2 as signed 16-bit PCM. Preserve that
            // full-scale convention at the common 24-bit boundary.
            audio_l <= $signed({raw_audio_l, 6'b000000});
            audio_r <= $signed({raw_audio_r, 6'b000000});
            audio_valid <= 1'b1;
        end else begin
            audio_phase <= audio_phase + AUDIO_SAMPLE_HZ;
        end
    end

    wire unused_drive_status = d64_load_ready ^ drive_mounted ^ drive_led;
endmodule
