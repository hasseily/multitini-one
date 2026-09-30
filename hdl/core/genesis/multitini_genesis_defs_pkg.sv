// SPDX-License-Identifier: GPL-3.0-only
// Generated from spec/genesis_interface_v0.json. Do not edit.
`timescale 1ns / 1ps
package multitini_genesis_defs_pkg;
    localparam logic [31:0] MT_GENESIS_CORE_ID = 32'h4D442020;
    localparam integer MT_GENESIS_CORE_NTSC_HZ = 107386000;
    localparam integer MT_GENESIS_CORE_PAL_HZ = 106406000;
    localparam integer MT_GENESIS_AUDIO_SAMPLE_HZ = 48000;
    localparam logic [7:0] MT_LOAD_GENESIS_CART = 8'h30;
    localparam logic [7:0] MT_LOAD_GENESIS_SAVE = 8'h31;
    localparam logic [7:0] MT_LOAD_GENESIS_CONFIG = 8'h32;
    localparam integer MT_SIZE_GENESIS_CART_MIN = 512;
    localparam integer MT_SIZE_GENESIS_CART_MAX = 8388608;
    localparam integer MT_SIZE_GENESIS_SAVE = 65536;
    localparam integer MT_SIZE_GENESIS_CONFIG = 16;
    localparam logic [7:0] MT_INPUT_GENESIS_GAMEPAD = 8'h30;
    localparam logic [7:0] MT_INPUT_GENESIS = MT_INPUT_GENESIS_GAMEPAD;
    // BUTTON constants are bit indexes; bare names are pressed-button masks.
    localparam integer MT_GENESIS_BUTTON_RIGHT = 0;
    localparam logic [11:0] MT_GENESIS_RIGHT = 12'h001;
    localparam integer MT_GENESIS_BUTTON_LEFT = 1;
    localparam logic [11:0] MT_GENESIS_LEFT = 12'h002;
    localparam integer MT_GENESIS_BUTTON_DOWN = 2;
    localparam logic [11:0] MT_GENESIS_DOWN = 12'h004;
    localparam integer MT_GENESIS_BUTTON_UP = 3;
    localparam logic [11:0] MT_GENESIS_UP = 12'h008;
    localparam integer MT_GENESIS_BUTTON_A = 4;
    localparam logic [11:0] MT_GENESIS_A = 12'h010;
    localparam integer MT_GENESIS_BUTTON_B = 5;
    localparam logic [11:0] MT_GENESIS_B = 12'h020;
    localparam integer MT_GENESIS_BUTTON_C = 6;
    localparam logic [11:0] MT_GENESIS_C = 12'h040;
    localparam integer MT_GENESIS_BUTTON_START = 7;
    localparam logic [11:0] MT_GENESIS_START = 12'h080;
    localparam integer MT_GENESIS_BUTTON_MODE = 8;
    localparam logic [11:0] MT_GENESIS_MODE = 12'h100;
    localparam integer MT_GENESIS_BUTTON_X = 9;
    localparam logic [11:0] MT_GENESIS_X = 12'h200;
    localparam integer MT_GENESIS_BUTTON_Y = 10;
    localparam logic [11:0] MT_GENESIS_Y = 12'h400;
    localparam integer MT_GENESIS_BUTTON_Z = 11;
    localparam logic [11:0] MT_GENESIS_Z = 12'h800;
    localparam logic [7:0] MT_GENESIS_REGION_AUTO = 8'd0;
    localparam logic [7:0] MT_GENESIS_REGION_US = 8'd1;
    localparam logic [7:0] MT_GENESIS_REGION_EU = 8'd2;
    localparam logic [7:0] MT_GENESIS_REGION_JP = 8'd3;
    localparam logic [31:0] MT_GENESIS_CONFIG_MAGIC = 32'h3130444D;
    localparam logic [31:0] MT_GENESIS_CONFIG_REGION_OFFSET = 32'h00000004;
    localparam logic [31:0] MT_GENESIS_CONFIG_SIX_BUTTON_OFFSET = 32'h00000005;
    localparam logic [31:0] MT_GENESIS_CONFIG_FM_CHIP_OFFSET = 32'h00000006;
    localparam logic [31:0] MT_GENESIS_CONFIG_LOWPASS_OFFSET = 32'h00000007;
    localparam logic [31:0] MT_GENESIS_CONFIG_RESERVED_OFFSET = 32'h00000008;
    localparam logic [31:0] MT_GENESIS_CONFIG_SIX_BUTTON_DEFAULT = 32'h00000003;
    localparam logic [31:0] MT_GENESIS_CONFIG_FM_CHIP_DEFAULT = 32'h00000000;
    localparam logic [31:0] MT_GENESIS_CONFIG_LOWPASS_DEFAULT = 32'h00000001;
endpackage
