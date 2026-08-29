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

// Behavioral model for portable tests only. Vivado supplies the real XPM.
module xpm_cdc_handshake #(
    parameter integer DEST_EXT_HSK = 1,
    parameter integer DEST_SYNC_FF = 4,
    parameter integer INIT_SYNC_FF = 0,
    parameter integer SIM_ASSERT_CHK = 1,
    parameter integer SRC_SYNC_FF = 4,
    parameter integer WIDTH = 32
) (
    input  logic             dest_ack,
    input  logic             dest_clk,
    output logic [WIDTH-1:0] dest_out,
    output logic             dest_req,
    input  logic             src_clk,
    input  logic [WIDTH-1:0] src_in,
    output logic             src_rcv,
    input  logic             src_send
);
    logic [WIDTH-1:0] payload = '0;
    logic request_toggle = 1'b0;
    logic acknowledge_toggle = 1'b0;
    logic src_send_d = 1'b0;
    logic source_outstanding = 1'b0;
    logic request_meta = 1'b0;
    logic request_sync = 1'b0;
    logic acknowledge_meta = 1'b0;
    logic acknowledge_sync = 1'b0;

    assign dest_req = request_sync != acknowledge_toggle;
    assign dest_out = payload;
    assign src_rcv = source_outstanding &&
                     acknowledge_sync == request_toggle;

    always_ff @(posedge src_clk) begin
        src_send_d <= src_send;
        acknowledge_meta <= acknowledge_toggle;
        acknowledge_sync <= acknowledge_meta;
        if (src_send && !src_send_d) begin
            payload <= src_in;
            request_toggle <= ~request_toggle;
            source_outstanding <= 1'b1;
        end
        if (source_outstanding && !src_send &&
            acknowledge_sync == request_toggle)
            source_outstanding <= 1'b0;
    end

    always_ff @(posedge dest_clk) begin
        request_meta <= request_toggle;
        request_sync <= request_meta;
        if (dest_req && (DEST_EXT_HSK == 0 || dest_ack))
            acknowledge_toggle <= request_sync;
    end

    wire unused_parameters = (DEST_SYNC_FF == 0) ^ (INIT_SYNC_FF != 0) ^
                             (SIM_ASSERT_CHK == 0) ^ (SRC_SYNC_FF == 0);
endmodule
