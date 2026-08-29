// SPDX-License-Identifier: GPL-3.0-only
// Generated from spec/c64_interface_v0.json. Do not edit.
`timescale 1ns / 1ps
package multitini_c64_defs_pkg;
    localparam logic [31:0] MT_C64_CORE_ID = 32'h43363420;

    localparam integer MT_C64_PAL_HZ = 31527954;
    localparam integer MT_C64_NTSC_HZ = 32727266;
    localparam integer MT_C64_AUDIO_SAMPLE_HZ = 48000;

    localparam logic [7:0] MT_LOAD_C64_BASIC = 8'h01;
    localparam logic [7:0] MT_LOAD_C64_KERNAL = 8'h02;
    localparam logic [7:0] MT_LOAD_C64_CHARACTER = 8'h03;
    localparam logic [7:0] MT_LOAD_C64_1541_ROM = 8'h04;
    localparam logic [7:0] MT_LOAD_C64_PRG = 8'h10;
    localparam logic [7:0] MT_LOAD_C64_CRT = 8'h11;
    localparam logic [7:0] MT_LOAD_C64_D64 = 8'h12;
    localparam logic [7:0] MT_LOAD_C64_G64 = 8'h13;

    localparam integer MT_SIZE_C64_BASIC = 8192;
    localparam integer MT_SIZE_C64_KERNAL = 8192;
    localparam integer MT_SIZE_C64_CHARACTER = 4096;
    localparam integer MT_SIZE_C64_1541_ROM = 16384;

    localparam logic [7:0] MT_INPUT_PS2_SET2 = 8'h00;
    localparam logic [7:0] MT_INPUT_JOYSTICK1 = 8'h01;
    localparam logic [7:0] MT_INPUT_JOYSTICK2 = 8'h02;
endpackage
