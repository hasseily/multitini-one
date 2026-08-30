// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// One standard, serial-IEC 1541 for the first read-only D64 milestone.
// Raw-G64, parallel-drive ROMs, writes, second drives, and 1581 support are
// deliberately outside this boundary.
module multitini_c64_1541 #(
    parameter integer CORE_CLOCK_HZ = 31527954
) (
    input  logic        clk,
    input  logic        reset,
    input  logic        soft_reset,
    input  logic        clear_error,

    input  logic        drive_rom_write,
    input  logic [13:0] drive_rom_address,
    input  logic [7:0]  drive_rom_write_data,
    input  logic        drive_rom_loaded,

    input  logic        c64_iec_atn,
    input  logic        c64_iec_data,
    input  logic        c64_iec_clk,
    output logic        drive_iec_data,
    output logic        drive_iec_clk,

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

    input  logic [7:0]  d64_load_data,
    input  logic        d64_load_valid,
    output logic        d64_load_ready,
    input  logic        d64_load_first,
    input  logic        d64_load_last,
    input  logic [31:0] d64_load_offset,
    input  logic [31:0] d64_load_total,

    output logic        mounted,
    output logic        drive_led,
    output logic        error
);
    logic drive_reset;
    logic drive_rom_write_delayed;
    logic [7:0] drive_rom_write_data_delayed;
    logic drive_ce;
    logic [31:0] drive_phase;
    logic [3:0] phase_div;
    logic ph2_r;
    logic ph2_f;

    logic [14:0] drive_rom_read_address;
    logic [7:0]  drive_rom_read_data;

    logic [7:0] drive_data_from_disk;
    logic [7:0] drive_data_to_disk;
    logic       disk_mode;
    logic [1:0] stepper_phase;
    logic [1:0] previous_stepper_phase;
    logic [1:0] stepper_move;
    logic       motor_on;
    logic [1:0] motor_frequency;
    logic       byte_ready_n;
    logic       sync_n;
    logic       drive_activity;
    logic       drive_logic_iec_data;
    logic       drive_logic_iec_clk;

    logic [6:0] head_half_track;
    logic [5:0] buffer_track;
    logic       disk_busy;
    logic       disk_buffer_write;
    logic [12:0] disk_buffer_address;
    logic [7:0] disk_buffer_data;
    logic [7:0] unused_disk_buffer_dout;
    logic       unused_disk_write;
    logic [31:0] unused_mounted_generation;

    assign drive_reset = reset || soft_reset || !drive_rom_loaded ||
                         drive_rom_write || drive_rom_write_delayed;
    assign drive_iec_data = drive_reset ? 1'b1 : drive_logic_iec_data;
    assign drive_iec_clk  = drive_reset ? 1'b1 : drive_logic_iec_clk;
    assign drive_led = drive_activity || (mounted && disk_busy);
    assign stepper_move = stepper_phase - previous_stepper_phase;

    always_ff @(posedge clk) begin
        if (reset) begin
            drive_rom_write_delayed <= 1'b0;
            drive_rom_write_data_delayed <= 8'd0;
        end else begin
            drive_rom_write_delayed <= drive_rom_write;
            if (drive_rom_write)
                drive_rom_write_data_delayed <= drive_rom_write_data;
        end
    end

    // Generate the 16 MHz enable expected by the imported 1541 logic while
    // preserving the authentic PAL/NTSC C64 master clock.
    always_ff @(posedge clk) begin
        drive_ce <= 1'b0;
        if (reset) begin
            drive_phase <= 32'd0;
        end else if (drive_phase + 32'd16000000 >= CORE_CLOCK_HZ) begin
            drive_phase <= drive_phase + 32'd16000000 - CORE_CLOCK_HZ;
            drive_ce    <= 1'b1;
        end else begin
            drive_phase <= drive_phase + 32'd16000000;
        end
    end

    // The drive CPU runs at 1 MHz from the same phase convention used by the
    // pinned MiSTer c1541_multi wrapper.
    always_ff @(posedge clk) begin
        ph2_r <= 1'b0;
        ph2_f <= 1'b0;
        if (drive_reset) begin
            phase_div <= 4'd0;
        end else if (drive_ce) begin
            phase_div <= phase_div + 4'd1;
            ph2_r <= !phase_div[3] && !phase_div[2] &&
                     !phase_div[1] && !phase_div[0];
            ph2_f <= phase_div[3] && !phase_div[2] &&
                     !phase_div[1] && !phase_div[0];
        end
    end

    // Standard 1541-II ROMs are 16 KiB. The store intentionally starts empty
    // and remains inaccessible to the running drive until a complete validated
    // ROM object has been received.
    iecdrv_mem #(8, 14) drive_rom (
        .clock_a(clk), .address_a(drive_rom_address),
        // iecdrv_mem delays its write-enable and address by one clock but not
        // its data input, so hold the matching byte through the commit edge.
        .data_a(drive_rom_write_data_delayed),
        .wren_a(drive_rom_write), .q_a(),
        .clock_b(clk), .address_b(drive_rom_read_address[13:0]),
        .data_b(8'd0), .wren_b(1'b0), .q_b(drive_rom_read_data)
    );

    c1541_logic drive_logic (
        .clk(clk), .reset(drive_reset),
        .ce(drive_ce), .ph2_r(ph2_r), .ph2_f(ph2_f),
        .iec_clk_in(c64_iec_clk), .iec_data_in(c64_iec_data),
        .iec_atn_in(c64_iec_atn), .iec_clk_out(drive_logic_iec_clk),
        .iec_data_out(drive_logic_iec_data),
        .ext_en(1'b0), .rom_addr(drive_rom_read_address),
        .rom_data(drive_rom_read_data),
        .par_data_in(8'hFF), .par_stb_in(1'b1),
        .par_data_out(), .par_stb_out(),
        .ds(2'b00), .din(drive_data_from_disk),
        .dout(drive_data_to_disk), .mode(disk_mode),
        .stp(stepper_phase), .mtr(motor_on), .freq(motor_frequency),
        .sync_n(sync_n), .byte_n(byte_ready_n),
        .wps_n(!mounted), .tr00_sense_n(|head_half_track),
        .act(drive_activity)
    );

    // D64 sector bytes are converted to the bitstream seen by the emulated
    // drive electronics. Write protect is asserted permanently in this first
    // implementation, so the reverse conversion output is intentionally unused.
    c1541_gcr disk_gcr (
        .clk(clk), .ce(drive_ce),
        .dout(drive_data_from_disk), .din(drive_data_to_disk),
        .mode(disk_mode), .mtr(motor_on), .freq(motor_frequency),
        .sync_n(sync_n), .byte_n(byte_ready_n),
        .track(buffer_track), .busy(disk_busy), .we(unused_disk_write),
        .sd_clk(clk), .sd_lba(media_request_lba),
        .sd_buff_addr(disk_buffer_address),
        .sd_buff_dout(disk_buffer_data),
        .sd_buff_din(unused_disk_buffer_dout),
        .sd_buff_wr(disk_buffer_write)
    );

    // Track zero is half-track 0; the physical head is clamped by the media
    // service to the number of tracks represented by the mounted D64.
    always_ff @(posedge clk) begin
        previous_stepper_phase <= stepper_phase;
        if (drive_reset) begin
            head_half_track        <= 7'd34;
            previous_stepper_phase <= 2'd0;
        end else if (motor_on && stepper_move[0]) begin
            if (!stepper_move[1] && head_half_track < 7'd84)
                head_half_track <= head_half_track + 7'd1;
            else if (stepper_move[1] && head_half_track > 7'd0)
                head_half_track <= head_half_track - 7'd1;
        end
    end

    multitini_c64_d64_track track_service (
        .clk(clk), .reset(reset), .soft_reset(soft_reset),
        .clear_error(clear_error),
        .service_enable(drive_rom_loaded && !drive_rom_write &&
                        !drive_rom_write_delayed),
        .head_half_track(head_half_track),
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
        .load_data(d64_load_data), .load_valid(d64_load_valid),
        .load_ready(d64_load_ready), .load_first(d64_load_first),
        .load_last(d64_load_last), .load_offset(d64_load_offset),
        .load_total(d64_load_total), .buffer_write(disk_buffer_write),
        .buffer_address(disk_buffer_address), .buffer_data(disk_buffer_data),
        .buffer_track(buffer_track), .mounted(mounted),
        .mounted_generation(unused_mounted_generation),
        .disk_busy(disk_busy), .error(error)
    );
endmodule
