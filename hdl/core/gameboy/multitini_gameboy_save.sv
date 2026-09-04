// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Battery-backed cartridge-RAM persistence over the common 256-byte media
// contract. Runtime writes only mark the image dirty. After a quiet interval,
// or immediately for an eject barrier, one coherent snapshot is read from the
// common memory service and presented to firmware as a media write request.
module multitini_gameboy_save #(
    parameter integer AUTO_FLUSH_CYCLES = 33554432
) (
    input  logic        clk,
    input  logic        reset,
    input  logic        clear_error,

    input  logic        runtime_write_committed,
    input  logic [23:0] runtime_write_address,

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
    output logic [7:0]  media_write_data,
    output logic        media_write_data_valid,
    output logic        media_write_data_last,
    input  logic        media_write_data_ready,
    output logic        media_state_mounted,
    output logic        media_state_dirty,
    output logic        media_state_eject_pending,

    output logic        memory_read,
    output logic [23:0] memory_address,
    input  logic        memory_ready,
    input  logic [63:0] memory_rdata,
    input  logic        memory_rvalid,
    output logic        pause_core,
    output logic        error
);
    import multitini_gameboy_defs_pkg::*;

    localparam logic [7:0] SAVE_SLOT = 8'd0;

    typedef enum logic [2:0] {
        SAVE_IDLE,
        SAVE_READ_ISSUE,
        SAVE_READ_WAIT,
        SAVE_PRESENT,
        SAVE_WAIT_COMPLETE
    } save_state_t;

    save_state_t state;
    logic [31:0] mounted_size;
    logic [31:0] mounted_generation;
    logic [31:0] pending_eject_generation;
    logic [31:0] next_request_id;
    logic [31:0] snapshot_offset;
    logic [63:0] snapshot_line;
    logic [2:0] snapshot_byte;
    logic [31:0] idle_count;
    logic faulted;

    logic valid_mount;
    logic completion_matches;
    logic completion_succeeds;
    logic runtime_write_in_range;

    function automatic logic [31:0] next_nonzero_id(
        input logic [31:0] value
    );
        next_nonzero_id = value == 32'hffff_ffff ? 32'd1 : value + 32'd1;
    endfunction

    always_comb begin
        valid_mount = media_slot == SAVE_SLOT &&
                      media_kind == MT_LOAD_GB_SAVE &&
                      !media_read_only && media_generation != 32'd0 &&
                      media_image_size >= 32'd256 &&
                      media_image_size <= MT_SIZE_GB_CART_RAM_MAX &&
                      media_image_size[7:0] == 8'd0;
        completion_matches = media_request_valid && media_complete &&
                             media_complete_request_id == media_request_id &&
                             media_complete_generation ==
                                 media_request_generation;
        completion_succeeds = completion_matches &&
                              media_complete_status == 32'd0 &&
                              media_complete_block_count ==
                                  media_request_block_count &&
                              state == SAVE_WAIT_COMPLETE;
        runtime_write_in_range = media_state_mounted &&
            runtime_write_address >= MT_GB_MEMORY_CART_RAM_BASE &&
            runtime_write_address <
                MT_GB_MEMORY_CART_RAM_BASE + mounted_size;
    end

    assign media_request_write = 1'b1;
    assign media_request_lba = 32'd0;
    assign media_request_block_count = mounted_size[23:8];
    assign media_request_generation = mounted_generation;
    assign media_write_data =
        snapshot_line[snapshot_byte * 8 +: 8];
    assign media_write_data_valid = state == SAVE_PRESENT;
    assign media_write_data_last = state == SAVE_PRESENT &&
        snapshot_offset + {29'd0, snapshot_byte} + 32'd1 == mounted_size;
    assign memory_read = state == SAVE_READ_ISSUE;
    assign memory_address = MT_GB_MEMORY_CART_RAM_BASE +
                            snapshot_offset[23:0];
    assign pause_core = state != SAVE_IDLE ||
                        (media_state_eject_pending && media_state_dirty);

    task automatic begin_snapshot;
        begin
            media_request_valid <= 1'b1;
            media_request_id <= next_request_id;
            next_request_id <= next_nonzero_id(next_request_id);
            snapshot_offset <= 32'd0;
            snapshot_byte <= 3'd0;
            state <= SAVE_READ_ISSUE;
        end
    endtask

    always_ff @(posedge clk) begin
        if (reset) begin
            state <= SAVE_IDLE;
            mounted_size <= 32'd0;
            mounted_generation <= 32'd0;
            pending_eject_generation <= 32'd0;
            next_request_id <= 32'd1;
            snapshot_offset <= 32'd0;
            snapshot_line <= 64'd0;
            snapshot_byte <= 3'd0;
            idle_count <= 32'd0;
            faulted <= 1'b0;
            media_request_valid <= 1'b0;
            media_request_id <= 32'd0;
            media_state_mounted <= 1'b0;
            media_state_dirty <= 1'b0;
            media_state_eject_pending <= 1'b0;
            error <= 1'b0;
        end else begin
            if (runtime_write_committed && runtime_write_in_range) begin
                media_state_dirty <= 1'b1;
                idle_count <= 32'd0;
            end else if (media_state_dirty && state == SAVE_IDLE &&
                         !faulted && !media_state_eject_pending &&
                         AUTO_FLUSH_CYCLES != 0 &&
                         idle_count < AUTO_FLUSH_CYCLES) begin
                idle_count <= idle_count + 32'd1;
            end

            if (media_mount) begin
                if (valid_mount && state == SAVE_IDLE &&
                    !media_state_dirty && !media_state_eject_pending) begin
                    mounted_size <= media_image_size;
                    mounted_generation <= media_generation;
                    pending_eject_generation <= 32'd0;
                    media_state_mounted <= 1'b1;
                    media_state_dirty <= 1'b0;
                    media_state_eject_pending <= 1'b0;
                    media_request_valid <= 1'b0;
                    idle_count <= 32'd0;
                    faulted <= 1'b0;
                    error <= 1'b0;
                end else begin
                    error <= 1'b1;
                end
            end else if (media_eject) begin
                if (media_slot != SAVE_SLOT || media_generation == 32'd0) begin
                    error <= 1'b1;
                end else if (!media_state_mounted) begin
                    mounted_generation <= media_generation;
                    error <= 1'b0;
                end else begin
                    pending_eject_generation <= media_generation;
                    media_state_eject_pending <= 1'b1;
                    faulted <= 1'b0;
                    error <= 1'b0;
                    if (!media_state_dirty && state == SAVE_IDLE) begin
                        media_state_mounted <= 1'b0;
                        media_state_eject_pending <= 1'b0;
                        mounted_generation <= media_generation;
                        pending_eject_generation <= 32'd0;
                    end else if (state == SAVE_IDLE) begin
                        begin_snapshot();
                    end
                end
            end else if (clear_error) begin
                faulted <= 1'b0;
                error <= 1'b0;
                media_request_valid <= 1'b0;
                state <= SAVE_IDLE;
                if (media_state_eject_pending && media_state_dirty)
                    begin_snapshot();
            end else begin
                case (state)
                SAVE_IDLE: begin
                    if (media_state_mounted && media_state_dirty && !faulted &&
                        (media_state_eject_pending ||
                         (AUTO_FLUSH_CYCLES != 0 &&
                          idle_count >= AUTO_FLUSH_CYCLES))) begin
                        begin_snapshot();
                    end
                end
                SAVE_READ_ISSUE:
                    if (memory_read && memory_ready)
                        state <= SAVE_READ_WAIT;
                SAVE_READ_WAIT:
                    if (memory_rvalid) begin
                        snapshot_line <= memory_rdata;
                        snapshot_byte <= 3'd0;
                        state <= SAVE_PRESENT;
                    end
                SAVE_PRESENT:
                    if (media_write_data_valid && media_write_data_ready) begin
                        if (media_write_data_last) begin
                            state <= SAVE_WAIT_COMPLETE;
                        end else if (snapshot_byte == 3'd7) begin
                            snapshot_offset <= snapshot_offset + 32'd8;
                            snapshot_byte <= 3'd0;
                            state <= SAVE_READ_ISSUE;
                        end else begin
                            snapshot_byte <= snapshot_byte + 3'd1;
                        end
                    end
                SAVE_WAIT_COMPLETE: ;
                default: state <= SAVE_IDLE;
                endcase

                if (completion_matches) begin
                    media_request_valid <= 1'b0;
                    state <= SAVE_IDLE;
                    idle_count <= 32'd0;
                    if (completion_succeeds) begin
                        media_state_dirty <= 1'b0;
                        faulted <= 1'b0;
                        error <= 1'b0;
                        if (media_state_eject_pending) begin
                            media_state_mounted <= 1'b0;
                            media_state_eject_pending <= 1'b0;
                            mounted_generation <= pending_eject_generation;
                            pending_eject_generation <= 32'd0;
                        end
                    end else begin
                        faulted <= 1'b1;
                        error <= 1'b1;
                    end
                end
            end
        end
    end
endmodule
