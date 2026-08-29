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
// Match xpm_cdc_gray's binary-to-Gray source register, destination synchronizer,
// and Gray-to-binary output so async-ring tests exercise legal pointer motion.
module xpm_cdc_gray #(
    parameter integer DEST_SYNC_FF = 4,
    parameter integer INIT_SYNC_FF = 0,
    parameter integer REG_OUTPUT = 0,
    parameter integer SIM_ASSERT_CHK = 0,
    parameter integer SIM_LOSSLESS_GRAY_CHK = 0,
    parameter integer VERSION = 0,
    parameter integer WIDTH = 2
) (
    input  logic             src_clk,
    input  logic [WIDTH-1:0] src_in_bin,
    input  logic             dest_clk,
    output logic [WIDTH-1:0] dest_out_bin
);
    logic [WIDTH-1:0] source_gray = '0;
    logic [WIDTH-1:0] synchronizer [0:DEST_SYNC_FF-1];
    logic [WIDTH-1:0] decoded_binary;
    logic [WIDTH-1:0] output_register = '0;
    integer init_stage;
    integer sync_stage;
    integer bit_index;

    initial begin
        for (init_stage = 0; init_stage < DEST_SYNC_FF;
             init_stage = init_stage + 1)
            synchronizer[init_stage] = '0;
    end

    assign dest_out_bin = REG_OUTPUT ? output_register : decoded_binary;

    always_ff @(posedge src_clk)
        source_gray <= src_in_bin ^ {1'b0, src_in_bin[WIDTH-1:1]};

    always_ff @(posedge dest_clk) begin
        synchronizer[0] <= source_gray;
        for (sync_stage = 1; sync_stage < DEST_SYNC_FF;
             sync_stage = sync_stage + 1)
            synchronizer[sync_stage] <= synchronizer[sync_stage-1];
        output_register <= decoded_binary;
    end

    always_comb begin
        decoded_binary[WIDTH-1] = synchronizer[DEST_SYNC_FF-1][WIDTH-1];
        for (bit_index = WIDTH - 2; bit_index >= 0; bit_index = bit_index - 1)
            decoded_binary[bit_index] =
                decoded_binary[bit_index+1] ^
                synchronizer[DEST_SYNC_FF-1][bit_index];
    end

    wire unused_parameters = (INIT_SYNC_FF == 0) ^
                             (SIM_ASSERT_CHK == 0) ^
                             (SIM_LOSSLESS_GRAY_CHK == 0) ^
                             (VERSION == 0);
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
