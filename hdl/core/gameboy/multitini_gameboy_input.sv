// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module multitini_gameboy_input (
    input  logic        clk,
    input  logic        reset,
    input  logic [15:0] input_code,
    input  logic [15:0] input_value,
    input  logic        input_valid,
    output logic        input_ready,
    input  logic        p14,
    input  logic        p15,
    output logic [3:0]  joy_din,
    output logic [7:0]  buttons
);
    import multitini_gameboy_defs_pkg::*;

    assign input_ready = !reset;

    always_comb begin
        joy_din = (~{buttons[MT_GB_BUTTON_DOWN],
                     buttons[MT_GB_BUTTON_UP],
                     buttons[MT_GB_BUTTON_LEFT],
                     buttons[MT_GB_BUTTON_RIGHT]} | {4{p14}}) &
                  (~{buttons[MT_GB_BUTTON_START],
                     buttons[MT_GB_BUTTON_SELECT],
                     buttons[MT_GB_BUTTON_B],
                     buttons[MT_GB_BUTTON_A]} | {4{p15}});
    end

    always_ff @(posedge clk) begin
        if (reset) begin
            buttons <= 8'd0;
        end else if (input_valid && input_ready &&
                     input_code[15:8] == MT_INPUT_GB_GAMEPAD &&
                     input_code[7:0] < 8'd8) begin
            buttons[input_code[2:0]] <= input_value[0];
        end
    end
endmodule
