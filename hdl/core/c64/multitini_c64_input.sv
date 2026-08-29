// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module multitini_c64_input (
    input  logic        clk,
    input  logic        reset,
    input  logic [15:0] input_code,
    input  logic [15:0] input_value,
    input  logic        input_valid,
    output logic        input_ready,
    output logic [10:0] ps2_key,
    output logic [6:0]  joy_a,
    output logic [6:0]  joy_b
);
    import multitini_c64_defs_pkg::*;

    assign input_ready = !reset;

    always_ff @(posedge clk) begin
        if (reset) begin
            ps2_key <= 11'd0;
            joy_a   <= 7'd0;
            joy_b   <= 7'd0;
        end else if (input_valid && input_ready) begin
            case (input_code[15:8])
                MT_INPUT_PS2_SET2: begin
                    ps2_key[7:0] <= input_code[7:0];
                    ps2_key[8]   <= input_value[1];
                    ps2_key[9]   <= input_value[0];
                    ps2_key[10]  <= ~ps2_key[10];
                end
                MT_INPUT_JOYSTICK1: begin
                    if (input_code[7:0] < 8'd7)
                        joy_a[input_code[2:0]] <= input_value[0];
                end
                MT_INPUT_JOYSTICK2: begin
                    if (input_code[7:0] < 8'd7)
                        joy_b[input_code[2:0]] <= input_value[0];
                end
                default: begin end
            endcase
        end
    end
endmodule
