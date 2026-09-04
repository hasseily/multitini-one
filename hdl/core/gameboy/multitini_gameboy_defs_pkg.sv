// SPDX-License-Identifier: GPL-3.0-only
// Generated from spec/gameboy_interface_v0.json. Do not edit.
`timescale 1ns / 1ps
package multitini_gameboy_defs_pkg;
    localparam logic [31:0] MT_GBC_CORE_ID = 32'h47424320;

    localparam integer MT_GBC_CORE_HZ = 33554432;
    localparam integer MT_GBC_CPU_HZ = 4194304;
    localparam integer MT_GBC_AUDIO_SAMPLE_HZ = 48000;

    localparam logic [7:0] MT_LOAD_GBC_BOOT = 8'h20;
    localparam logic [7:0] MT_LOAD_GB_CART = 8'h21;
    localparam logic [7:0] MT_LOAD_GB_SAVE = 8'h22;

    localparam integer MT_SIZE_GBC_BOOT = 2304;
    localparam integer MT_SIZE_GB_CART_MIN = 32768;
    localparam integer MT_SIZE_GB_CART_MAX = 8388608;
    localparam integer MT_SIZE_GB_CART_RAM_MAX = 131072;

    localparam logic [7:0] MT_INPUT_GB_GAMEPAD = 8'h10;

    localparam integer MT_GB_BUTTON_RIGHT = 0;
    localparam integer MT_GB_BUTTON_LEFT = 1;
    localparam integer MT_GB_BUTTON_DOWN = 2;
    localparam integer MT_GB_BUTTON_UP = 3;
    localparam integer MT_GB_BUTTON_A = 4;
    localparam integer MT_GB_BUTTON_B = 5;
    localparam integer MT_GB_BUTTON_SELECT = 6;
    localparam integer MT_GB_BUTTON_START = 7;

    localparam logic [23:0] MT_GB_MEMORY_ROM_BASE = 24'h000000;
    localparam logic [23:0] MT_GB_MEMORY_CART_RAM_BASE = 24'h800000;
endpackage
