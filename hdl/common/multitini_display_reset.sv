`timescale 1ns / 1ps
// SPDX-License-Identifier: GPL-3.0-only
// Clock loss asserts reset immediately. A registered control-domain hold
// request crosses as data, never as combinational logic on an async reset pin.
module multitini_display_reset (
    input  wire clk,
    input  wire clock_resetn,
    input  wire hold_request,
    output reg  resetn = 1'b0
);
    (* ASYNC_REG = "TRUE" *) reg sync_meta = 1'b1;
    (* ASYNC_REG = "TRUE" *) reg sync_ff = 1'b1;
    always @(posedge clk or negedge clock_resetn) begin
        if (!clock_resetn) begin
            sync_meta <= 1'b1;
            sync_ff <= 1'b1;
            resetn <= 1'b0;
        end else begin
            sync_meta <= hold_request;
            sync_ff <= sync_meta;
            resetn <= !sync_ff;
        end
    end
endmodule
