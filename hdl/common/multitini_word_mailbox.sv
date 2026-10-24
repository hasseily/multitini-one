// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Coherent latest-value crossing. The mailbox owns each submitted snapshot
// until acknowledged. A destination reset causes a fresh snapshot to be sent.
module multitini_word_mailbox #(
    parameter integer WIDTH = 32,
    parameter logic [WIDTH-1:0] RESET_VALUE = '0
) (
    input logic src_clk, src_resetn,
    input logic [WIDTH-1:0] value,
    input logic dst_clk, dst_resetn,
    output logic [WIDTH-1:0] snapshot
);
    logic [WIDTH-1:0] sent, destination;
    logic ready, valid, force_send;
    wire destination_running;
    wire changed = (force_send || value != sent) && destination_running;
    cdc_bit_sync destination_reset_sync (
        .clk(src_clk), .resetn(src_resetn), .din(dst_resetn),
        .dout(destination_running)
    );
    multitini_async_mailbox #(.WIDTH(WIDTH), .DRAIN_ON_RESET(1'b1)) mailbox_i (
        .src_clk(src_clk), .src_resetn(src_resetn),
        .src_data(value), .src_valid(changed), .src_ready(ready),
        .dst_clk(dst_clk), .dst_resetn(dst_resetn),
        .dst_data(destination), .dst_valid(valid), .dst_ready(1'b1)
    );
    always_ff @(posedge src_clk) begin
        if (!src_resetn) begin
            sent <= RESET_VALUE;
            force_send <= 1;
        end else if (!destination_running) force_send <= 1;
        else if (changed && ready) begin
            sent <= value;
            force_send <= 0;
        end
    end
    always_ff @(posedge dst_clk) begin
        if (!dst_resetn) snapshot <= RESET_VALUE;
        else if (valid) snapshot <= destination;
    end
endmodule
