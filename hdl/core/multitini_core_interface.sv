// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Draft ABI 0.1 core-facing signal bundle, exercised by C64 and GBC adapters.
// It may still change before ABI 1.0. Board pins never appear here.
interface multitini_core_interface;
    logic        clk;
    logic        reset;
    logic [31:0] core_id;
    logic        ready;
    logic [7:0]  video_r;
    logic [7:0]  video_g;
    logic [7:0]  video_b;
    logic        video_de;
    logic        video_hsync;
    logic        video_vsync;
    logic        video_valid;
    logic signed [23:0] audio_l;
    logic signed [23:0] audio_r;
    logic        audio_valid;
    logic [15:0] input_code;
    logic [15:0] input_value;
    logic        input_valid;
    logic        input_ready;
    logic [7:0]  load_data;
    logic        load_valid;
    logic        load_ready;
    logic        load_first;
    logic        load_last;
    logic [7:0]  load_kind;
    logic [31:0] load_offset;
    logic [31:0] load_total;
    logic        load_error;
    // One-outstanding, 64-bit aligned external-memory service. Requests stay
    // stable through ready; every accepted request receives one response.
    logic [23:0] memory_addr;
    logic        memory_write;
    logic [63:0] memory_wdata;
    logic [7:0]  memory_wstrb;
    logic        memory_valid;
    logic        memory_ready;
    logic [63:0] memory_rdata;
    logic        memory_rvalid;
    logic        memory_fault;
    // One media request remains asserted and coherent until the shell returns
    // a matching completion. LBA and block count use 256-byte units.
    logic        media_request_valid;
    logic        media_request_write;
    logic [31:0] media_request_id;
    logic [31:0] media_request_lba;
    logic [15:0] media_request_block_count;
    logic [31:0] media_request_generation;
    logic [7:0]  media_write_data;
    logic        media_write_data_valid;
    logic        media_write_data_last;
    logic        media_write_data_ready;
    logic        media_state_mounted;
    logic        media_state_dirty;
    logic        media_state_eject_pending;
    logic        media_mount;
    logic        media_eject;
    logic        media_complete;
    logic [7:0]  media_slot;
    logic [7:0]  media_kind;
    logic        media_read_only;
    logic [31:0] media_image_size;
    logic [31:0] media_generation;
    logic [31:0] media_complete_request_id;
    logic [31:0] media_complete_status;
    logic [15:0] media_complete_block_count;
    logic [31:0] media_complete_generation;

    modport shell (
        output clk, reset, input core_id, ready,
        input video_r, video_g, video_b, video_de, video_hsync, video_vsync,
        input video_valid,
        input audio_l, audio_r, audio_valid,
        output input_code, input_value, input_valid, input input_ready, load_ready,
        output load_data, load_valid, load_first, load_last, load_kind,
        load_offset, load_total, input load_error,
        input memory_addr, memory_write, memory_wdata, memory_wstrb,
        memory_valid, output memory_ready, memory_rdata, memory_rvalid,
        memory_fault,
        input media_request_valid, media_request_write, media_request_id,
        media_request_lba, media_request_block_count,
        media_request_generation, media_write_data,
        media_write_data_valid, media_write_data_last,
        output media_write_data_ready,
        input media_state_mounted, media_state_dirty,
        media_state_eject_pending,
        output media_mount, media_eject, media_complete, media_slot,
        media_kind, media_read_only, media_image_size, media_generation,
        media_complete_request_id, media_complete_status,
        media_complete_block_count, media_complete_generation
    );

    modport core (
        input clk, reset, output core_id, ready,
        output video_r, video_g, video_b, video_de, video_hsync, video_vsync,
        output video_valid,
        output audio_l, audio_r, audio_valid,
        input input_code, input_value, input_valid, output input_ready, load_ready,
        input load_data, load_valid, load_first, load_last, load_kind,
        load_offset, load_total, output load_error,
        output memory_addr, memory_write, memory_wdata, memory_wstrb,
        memory_valid, input memory_ready, memory_rdata, memory_rvalid,
        memory_fault,
        output media_request_valid, media_request_write, media_request_id,
        media_request_lba, media_request_block_count,
        media_request_generation, media_write_data,
        media_write_data_valid, media_write_data_last,
        input media_write_data_ready,
        output media_state_mounted, media_state_dirty,
        media_state_eject_pending,
        input media_mount, media_eject, media_complete, media_slot,
        media_kind, media_read_only, media_image_size, media_generation,
        media_complete_request_id, media_complete_status,
        media_complete_block_count, media_complete_generation
    );
endinterface
