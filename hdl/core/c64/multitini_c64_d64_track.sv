// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Read-only D64 track-buffer service for one emulated 1541 (device 8).
//
// The geometry and directory-track-first mount sequence follow the sector
// buffer design immediately before MiSTer C64 commit a1a560e1cc51ab27c842
// 4678a39254aab67a5afe (parent f11608f66bcb2b017680d7c719aa547f537bfd3f).
// The Multitini request contract differs in two important ways: block_count is
// an actual count of 256-byte blocks, and request metadata remains asserted and
// stable until a matching completion arrives.
module multitini_c64_d64_track (
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
    output logic [12:0] buffer_address,
    output logic [7:0]  buffer_data,
    output logic [5:0]  buffer_track,
    output logic        mounted,
    output logic [31:0] mounted_generation,
    output logic        disk_busy,
    output logic        error
);
    import multitini_c64_defs_pkg::*;

    localparam logic [7:0] DRIVE8_SLOT = 8'd0;
    localparam logic [5:0] DIRECTORY_TRACK_INDEX = 6'd17;

    logic [5:0]  mounted_track_count;
    logic [5:0]  buffered_track_index;
    logic [5:0]  loading_track_index;
    logic        buffer_valid;
    logic        directory_pending;
    logic        request_gap;
    logic        faulted;
    logic [31:0] next_request_id;
    logic [31:0] stream_bytes_received;
    logic        stream_started;
    logic        stream_complete;
    logic        stream_object_error;

    function automatic logic [5:0] d64_track_count(input logic [31:0] size);
        case (size)
            32'd174848, 32'd175531: d64_track_count = 6'd35;
            32'd196608, 32'd197376: d64_track_count = 6'd40;
            32'd205312, 32'd206114: d64_track_count = 6'd42;
            default:                 d64_track_count = 6'd0;
        endcase
    endfunction

    function automatic logic [15:0] d64_track_blocks(input logic [5:0] track);
        if (track < 6'd17)
            d64_track_blocks = 16'd21;
        else if (track < 6'd24)
            d64_track_blocks = 16'd19;
        else if (track < 6'd30)
            d64_track_blocks = 16'd18;
        else
            d64_track_blocks = 16'd17;
    endfunction

    function automatic logic [31:0] d64_track_lba(input logic [5:0] track);
        if (track < 6'd17)
            d64_track_lba = track * 32'd21;
        else if (track < 6'd24)
            d64_track_lba = 32'd357 + (track - 6'd17) * 32'd19;
        else if (track < 6'd30)
            d64_track_lba = 32'd490 + (track - 6'd24) * 32'd18;
        else
            d64_track_lba = 32'd598 + (track - 6'd30) * 32'd17;
    endfunction

    function automatic logic [31:0] next_nonzero_id(input logic [31:0] value);
        next_nonzero_id = (value == 32'hFFFF_FFFF) ? 32'd1 : value + 32'd1;
    endfunction

    logic [5:0] requested_track_count;
    logic [5:0] desired_track_index;
    logic [31:0] expected_stream_bytes;
    logic completion_matches;
    logic completion_succeeds;

    always_comb begin
        requested_track_count = d64_track_count(media_image_size);
        if (mounted_track_count == 6'd0)
            desired_track_index = 6'd0;
        else if ({1'b0, head_half_track[6:1]} >= mounted_track_count)
            desired_track_index = mounted_track_count - 6'd1;
        else
            desired_track_index = head_half_track[6:1];

        expected_stream_bytes = {media_request_block_count, 8'd0};
        completion_matches = media_request_valid && media_complete &&
                             media_complete_request_id == media_request_id &&
                             media_complete_generation ==
                                 media_request_generation;
        completion_succeeds = completion_matches &&
                              media_complete_status == 32'd0 &&
                              media_complete_block_count ==
                                  media_request_block_count &&
                              stream_complete && !stream_object_error;
    end

    assign load_ready = media_request_valid && mounted && service_enable &&
                        !faulted;
    assign disk_busy = !mounted || !buffer_valid || media_request_valid ||
                       request_gap || faulted;

    always_ff @(posedge clk) begin : service_state
        logic [5:0] issue_track;
        logic       issue_request;
        logic       beat_good;

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
            mounted_track_count       <= 6'd0;
            buffered_track_index      <= 6'd0;
            loading_track_index       <= 6'd0;
            buffer_track              <= 6'd18;
            buffer_valid              <= 1'b0;
            directory_pending         <= 1'b0;
            request_gap               <= 1'b0;
            faulted                   <= 1'b0;
            stream_bytes_received     <= 32'd0;
            stream_started            <= 1'b0;
            stream_complete           <= 1'b0;
            stream_object_error       <= 1'b0;
            buffer_address            <= 13'd0;
            buffer_data               <= 8'd0;
            error                     <= 1'b0;
        end else if (clear_error) begin
            error                 <= 1'b0;
            faulted               <= 1'b0;
            media_request_valid   <= 1'b0;
            request_gap           <= 1'b1;
            buffer_valid          <= 1'b0;
            directory_pending     <= mounted;
            stream_bytes_received <= 32'd0;
            stream_started        <= 1'b0;
            stream_complete       <= 1'b0;
            stream_object_error   <= 1'b0;
        end else if (media_mount) begin
            if (media_slot == DRIVE8_SLOT &&
                media_kind == MT_LOAD_C64_D64 && media_read_only &&
                media_generation != 32'd0 && requested_track_count != 6'd0) begin
                mounted                   <= 1'b1;
                mounted_generation        <= media_generation;
                mounted_track_count       <= requested_track_count;
                media_request_valid       <= 1'b0;
                request_gap               <= 1'b1;
                buffer_valid              <= 1'b0;
                directory_pending         <= 1'b1;
                faulted                   <= 1'b0;
                stream_bytes_received     <= 32'd0;
                stream_started            <= 1'b0;
                stream_complete           <= 1'b0;
                stream_object_error       <= 1'b0;
            end else begin
                error <= 1'b1;
            end
        end else if (media_eject) begin
            // Eject carries a new invalidation generation, not the generation
            // of the previous mount. Empty-drive eject is intentionally
            // idempotent so firmware can establish a clean generation epoch.
            if (media_slot == DRIVE8_SLOT && media_generation != 32'd0) begin
                mounted                 <= 1'b0;
                mounted_generation      <= media_generation;
                mounted_track_count     <= 6'd0;
                media_request_valid     <= 1'b0;
                request_gap             <= 1'b1;
                buffer_valid            <= 1'b0;
                directory_pending       <= 1'b0;
                faulted                 <= 1'b0;
                stream_bytes_received   <= 32'd0;
                stream_started          <= 1'b0;
                stream_complete         <= 1'b0;
                stream_object_error     <= 1'b0;
            end else begin
                error <= 1'b1;
            end
        end else if (soft_reset) begin
            // A C64 soft reset also restarts the drive CPU, but the inserted
            // disk and its generation remain owned by the active mount.
            media_request_valid   <= 1'b0;
            request_gap           <= 1'b1;
            buffer_valid          <= 1'b0;
            directory_pending     <= mounted;
            faulted               <= 1'b0;
            stream_bytes_received <= 32'd0;
            stream_started        <= 1'b0;
            stream_complete       <= 1'b0;
            stream_object_error   <= 1'b0;
        end else begin
            if (request_gap)
                request_gap <= 1'b0;

            if (load_valid && load_ready) begin
                beat_good = load_total == expected_stream_bytes &&
                            load_offset == stream_bytes_received &&
                            load_first == (stream_bytes_received == 32'd0) &&
                            load_last == (load_offset + 32'd1 == load_total) &&
                            load_offset < load_total && !stream_complete;
                if (beat_good) begin
                    buffer_write          <= 1'b1;
                    buffer_address        <= load_offset[12:0];
                    buffer_data           <= load_data;
                    stream_started        <= 1'b1;
                    stream_bytes_received <= stream_bytes_received + 32'd1;
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
                stream_started        <= 1'b0;
                stream_complete       <= 1'b0;
                stream_object_error   <= 1'b0;
                if (completion_succeeds) begin
                    buffer_valid         <= 1'b1;
                    buffered_track_index <= loading_track_index;
                    if (directory_pending &&
                        loading_track_index == DIRECTORY_TRACK_INDEX)
                        directory_pending <= 1'b0;
                end else begin
                    buffer_valid <= 1'b0;
                    faulted      <= 1'b1;
                    error        <= 1'b1;
                end
            end else if (!media_request_valid && !request_gap && mounted &&
                         service_enable && !faulted) begin
                issue_request = 1'b0;
                issue_track   = desired_track_index;
                if (directory_pending) begin
                    issue_request = 1'b1;
                    issue_track   = DIRECTORY_TRACK_INDEX;
                end else if (!buffer_valid ||
                             buffered_track_index != desired_track_index) begin
                    issue_request = 1'b1;
                end

                if (issue_request) begin
                    media_request_valid       <= 1'b1;
                    media_request_write       <= 1'b0;
                    media_request_id          <= next_request_id;
                    media_request_lba         <= d64_track_lba(issue_track);
                    media_request_block_count <= d64_track_blocks(issue_track);
                    media_request_generation  <= mounted_generation;
                    next_request_id           <= next_nonzero_id(next_request_id);
                    loading_track_index       <= issue_track;
                    buffer_track              <= issue_track + 6'd1;
                    stream_bytes_received     <= 32'd0;
                    stream_started            <= 1'b0;
                    stream_complete           <= 1'b0;
                    stream_object_error       <= 1'b0;
                end
            end
        end
    end

    wire unused_stream_started = stream_started;
endmodule
