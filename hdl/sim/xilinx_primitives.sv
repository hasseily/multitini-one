// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module ODDR #(
    parameter DDR_CLK_EDGE = "OPPOSITE_EDGE",
    parameter INIT = 1'b0,
    parameter SRTYPE = "SYNC"
) (
    output logic Q,
    input  logic C,
    input  logic CE,
    input  logic D1,
    input  logic D2,
    input  logic R,
    input  logic S
);
    initial Q = INIT;

    always @(posedge C) begin
        if (R) Q <= 1'b0;
        else if (S) Q <= 1'b1;
        else if (CE) Q <= D1;
    end

    always @(negedge C) begin
        if (R) Q <= 1'b0;
        else if (S) Q <= 1'b1;
        else if (CE) Q <= D2;
    end

    wire unused_parameters = (DDR_CLK_EDGE == "SAME_EDGE") ^
                             (SRTYPE == "ASYNC");
endmodule
