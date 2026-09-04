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
    localparam logic [7:0] MT_LOAD_C64_CRT_DETACH = 8'h14;
    localparam logic [7:0] MT_LOAD_C64_PRG_STAGE = 8'h15;
    localparam logic [7:0] MT_LOAD_C64_PRG_RUN = 8'h16;

    localparam integer MT_SIZE_C64_BASIC = 8192;
    localparam integer MT_SIZE_C64_KERNAL = 8192;
    localparam integer MT_SIZE_C64_CHARACTER = 4096;
    localparam integer MT_SIZE_C64_1541_ROM = 16384;
    localparam integer MT_SIZE_C64_PRG_MIN = 3;
    localparam integer MT_SIZE_C64_PRG_MAX = 65538;
    localparam integer MT_SIZE_C64_CRT_MIN = 80;
    localparam integer MT_SIZE_C64_CRT_MAX = 2101312;

    localparam logic [7:0] MT_CRT_NORMAL = 8'd0;
    localparam logic [7:0] MT_CRT_SIMONS_BASIC = 8'd4;
    localparam logic [7:0] MT_CRT_OCEAN_TYPE_1 = 8'd5;
    localparam logic [7:0] MT_CRT_FUN_PLAY = 8'd7;
    localparam logic [7:0] MT_CRT_SUPER_GAMES = 8'd8;
    localparam logic [7:0] MT_CRT_EPYX_FASTLOAD = 8'd10;
    localparam logic [7:0] MT_CRT_FINAL_CARTRIDGE_1_2 = 8'd13;
    localparam logic [7:0] MT_CRT_C64_GAME_SYSTEM = 8'd15;
    localparam logic [7:0] MT_CRT_DINAMIC = 8'd17;
    localparam logic [7:0] MT_CRT_ZAXXON = 8'd18;
    localparam logic [7:0] MT_CRT_MAGIC_DESK = 8'd19;
    localparam logic [7:0] MT_CRT_COMAL_80 = 8'd21;
    localparam logic [7:0] MT_CRT_WATERLOO_BASIC = 8'd22;
    localparam logic [7:0] MT_CRT_MIKRO_ASSEMBLER = 8'd28;
    localparam logic [7:0] MT_CRT_PROPHET64 = 8'd43;
    localparam logic [7:0] MT_CRT_RGCD = 8'd57;
    localparam logic [7:0] MT_CRT_MAGIC_DESK_2 = 8'd85;

    localparam logic [7:0] MT_INPUT_PS2_SET2 = 8'h00;
    localparam logic [7:0] MT_INPUT_JOYSTICK1 = 8'h01;
    localparam logic [7:0] MT_INPUT_JOYSTICK2 = 8'h02;
endpackage
