// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Draft ABI 0.1 core-facing signal bundle. The C64 adapter may narrow this
// before ABI 1.0. Board pins never appear in this interface.
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

    modport shell (
        output clk, reset, input core_id, ready,
        input video_r, video_g, video_b, video_de, video_hsync, video_vsync,
        input video_valid,
        input audio_l, audio_r, audio_valid,
        output input_code, input_value, input_valid, input input_ready, load_ready,
        output load_data, load_valid, load_first, load_last, load_kind,
        load_offset, load_total, input load_error
    );

    modport core (
        input clk, reset, output core_id, ready,
        output video_r, video_g, video_b, video_de, video_hsync, video_vsync,
        output video_valid,
        output audio_l, audio_r, audio_valid,
        input input_code, input_value, input_valid, output input_ready, load_ready,
        input load_data, load_valid, load_first, load_last, load_kind,
        load_offset, load_total, output load_error
    );
endinterface
