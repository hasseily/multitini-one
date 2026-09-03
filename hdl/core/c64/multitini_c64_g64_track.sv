// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Read-only raw-G64 half-track service for one emulated 1541. The firmware
// presents every half track as a normalized 8 KiB logical extent:
//   LBA = half_track * 32, block_count = 32 (256-byte blocks)
//   bytes 0..1 = little-endian raw-track length in bits 12:0 and the fixed
//                G64 speed zone in bits 14:13
//   bytes 2..  = raw GCR data, followed by 0xFF padding
module multitini_c64_g64_track (
    input  logic        clk,
    input  logic        reset,
    input  logic        soft_reset,
    input  logic        clear_error,
    input  logic        service_enable,
    input  logic [6:0]  head_half_track,

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

    input  logic [7:0]  load_data,
    input  logic        load_valid,
    output logic        load_ready,
    input  logic        load_first,
    input  logic        load_last,
    input  logic [31:0] load_offset,
    input  logic [31:0] load_total,

    output logic        buffer_write,
    output logic [13:0] buffer_address,
    output logic [63:0] buffer_data,
    output logic [1:0]  track_speed,
    output logic        mounted,
    output logic [31:0] mounted_generation,
    output logic        disk_busy,
    output logic        error
);
    import multitini_c64_defs_pkg::*;

    localparam logic [7:0] DRIVE8_SLOT = 8'd0;
    localparam logic [15:0] G64_TRACK_BLOCKS = 16'd32;
    localparam logic [31:0] G64_TRACK_BYTES = 32'd8192;

    logic [6:0] buffered_half_track;
    logic [6:0] loading_half_track;
    logic       buffer_valid;
    logic       request_gap;
    logic       faulted;
    logic [31:0] next_request_id;
    logic [31:0] stream_bytes_received;
    logic        stream_complete;
    logic        stream_object_error;
    logic [63:0] pack_word;

    logic [6:0] desired_half_track;
    logic completion_matches;
    logic completion_succeeds;

    function automatic logic [31:0] next_nonzero_id(
        input logic [31:0] value
    );
        next_nonzero_id = (value == 32'hFFFF_FFFF) ? 32'd1 : value + 32'd1;
    endfunction

    always_comb begin
        // G64 v0 supports at most 84 half tracks, indexed 0 through 83.
        desired_half_track = head_half_track > 7'd83 ? 7'd83 :
                             head_half_track;
        completion_matches = media_request_valid && media_complete &&
                             media_complete_request_id == media_request_id &&
                             media_complete_generation ==
                                 media_request_generation;
        completion_succeeds = completion_matches &&
                              media_complete_status == 32'd0 &&
                              media_complete_block_count == G64_TRACK_BLOCKS &&
                              stream_complete && !stream_object_error;
    end

    assign load_ready = media_request_valid && !media_request_write &&
                        mounted && service_enable && !faulted;
    assign disk_busy = !mounted || !buffer_valid || media_request_valid ||
                       request_gap || faulted ||
                       buffered_half_track != desired_half_track;

    always_ff @(posedge clk) begin : service_state
        logic beat_good;
        logic [63:0] assembled_word;

        buffer_write <= 1'b0;

        if (reset) begin
            media_request_valid       <= 1'b0;
            media_request_write       <= 1'b0;
            media_request_id          <= 32'd0;
            media_request_lba         <= 32'd0;
            media_request_block_count <= 16'd0;
            media_request_generation  <= 32'd0;
            next_request_id           <= 32'd1;
            mounted                   <= 1'b0;
            mounted_generation        <= 32'd0;
            buffered_half_track       <= 7'd0;
            loading_half_track        <= 7'd0;
            buffer_valid              <= 1'b0;
            request_gap               <= 1'b0;
            faulted                   <= 1'b0;
            stream_bytes_received     <= 32'd0;
            stream_complete           <= 1'b0;
            stream_object_error       <= 1'b0;
            pack_word                 <= 64'd0;
            buffer_address            <= 14'd0;
            buffer_data               <= 64'd0;
            track_speed               <= 2'd0;
            error                     <= 1'b0;
        // Ordered media commands take priority over the independent
        // error-clear pulse so retry cannot discard a mount or eject.
        end else if (media_mount) begin
            if (media_slot == DRIVE8_SLOT &&
                media_kind == MT_LOAD_C64_G64 && media_read_only &&
                media_image_size != 32'd0 && media_generation != 32'd0) begin
                mounted                  <= 1'b1;
                mounted_generation       <= media_generation;
                media_request_valid      <= 1'b0;
                request_gap              <= 1'b1;
                buffer_valid             <= 1'b0;
                faulted                  <= 1'b0;
                stream_bytes_received    <= 32'd0;
                stream_complete          <= 1'b0;
                stream_object_error      <= 1'b0;
                pack_word                <= 64'd0;
                error                    <= 1'b0;
            end else begin
                error <= 1'b1;
            end
        end else if (media_eject) begin
            if (media_slot == DRIVE8_SLOT && media_generation != 32'd0) begin
                mounted                 <= 1'b0;
                mounted_generation      <= media_generation;
                media_request_valid     <= 1'b0;
                request_gap             <= 1'b1;
                buffer_valid            <= 1'b0;
                faulted                 <= 1'b0;
                stream_bytes_received   <= 32'd0;
                stream_complete         <= 1'b0;
                stream_object_error     <= 1'b0;
                pack_word               <= 64'd0;
                error                   <= 1'b0;
            end else begin
                error <= 1'b1;
            end
        end else if (clear_error) begin
            error                   <= 1'b0;
            faulted                 <= 1'b0;
            media_request_valid     <= 1'b0;
            request_gap             <= 1'b1;
            buffer_valid            <= 1'b0;
            stream_bytes_received   <= 32'd0;
            stream_complete         <= 1'b0;
            stream_object_error     <= 1'b0;
            pack_word               <= 64'd0;
        end else if (soft_reset) begin
            media_request_valid     <= 1'b0;
            request_gap             <= 1'b1;
            buffer_valid            <= 1'b0;
            faulted                 <= 1'b0;
            stream_bytes_received   <= 32'd0;
            stream_complete         <= 1'b0;
            stream_object_error     <= 1'b0;
            pack_word               <= 64'd0;
        end else begin
            if (request_gap)
                request_gap <= 1'b0;

            if (load_valid && load_ready) begin
                beat_good = load_total == G64_TRACK_BYTES &&
                            load_offset == stream_bytes_received &&
                            load_first == (stream_bytes_received == 32'd0) &&
                            load_last == (load_offset == G64_TRACK_BYTES - 1) &&
                            !stream_complete;
                if (beat_good) begin
                    assembled_word = pack_word;
                    assembled_word[load_offset[2:0] * 8 +: 8] = load_data;
                    pack_word <= assembled_word;
                    stream_bytes_received <= stream_bytes_received + 32'd1;
                    if (load_offset[2:0] == 3'd7) begin
                        buffer_write   <= 1'b1;
                        buffer_address <= load_offset[13:0] - 14'd7;
                        buffer_data    <= assembled_word;
                        pack_word      <= 64'd0;
                        if (load_offset == 32'd7)
                            track_speed <= assembled_word[14:13];
                    end
                    if (load_last)
                        stream_complete <= 1'b1;
                end else begin
                    stream_object_error <= 1'b1;
                    error               <= 1'b1;
                end
            end

            if (completion_matches) begin
                media_request_valid   <= 1'b0;
                request_gap           <= 1'b1;
                stream_bytes_received <= 32'd0;
                stream_complete       <= 1'b0;
                stream_object_error   <= 1'b0;
                pack_word             <= 64'd0;
                if (completion_succeeds) begin
                    buffer_valid        <= 1'b1;
                    buffered_half_track <= loading_half_track;
                end else begin
                    buffer_valid <= 1'b0;
                    faulted      <= 1'b1;
                    error        <= 1'b1;
                end
            end else if (!media_request_valid && !request_gap && mounted &&
                         service_enable && !faulted &&
                         (!buffer_valid || buffered_half_track !=
                                           desired_half_track)) begin
                media_request_valid       <= 1'b1;
                media_request_write       <= 1'b0;
                media_request_id          <= next_request_id;
                media_request_lba         <= desired_half_track * 32'd32;
                media_request_block_count <= G64_TRACK_BLOCKS;
                media_request_generation  <= mounted_generation;
                next_request_id           <= next_nonzero_id(next_request_id);
                loading_half_track        <= desired_half_track;
                stream_bytes_received     <= 32'd0;
                stream_complete           <= 1'b0;
                stream_object_error       <= 1'b0;
                pack_word                 <= 64'd0;
            end
        end
    end
endmodule
