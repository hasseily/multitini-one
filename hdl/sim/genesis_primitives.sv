// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Portable BUFGCE clock-gating model. CE is captured while the input clock is
// low, so a memory stall cannot truncate a chip clock pulse. Production uses
// the Xilinx primitive; Vivado timing owns its enable setup/hold requirements.
module BUFGCE #(
    parameter CE_TYPE = "SYNC",
    parameter IS_CE_INVERTED = 1'b0,
    parameter IS_I_INVERTED = 1'b0
) (
    input wire I,
    input wire CE,
    output wire O
);
    wire input_clock = I ^ IS_I_INVERTED;
    wire enable = CE ^ IS_CE_INVERTED;
    reg held_enable = 1'b0;
    always @* begin
        if (!input_clock) held_enable = enable;
    end
    assign O = input_clock & held_enable;
endmodule

module BUFG(input wire I, output wire O);
    assign O = I;
endmodule
