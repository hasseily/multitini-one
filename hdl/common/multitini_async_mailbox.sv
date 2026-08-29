// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// One-entry coherent mailbox for sparse control traffic between unrelated
// clocks. Xilinx's handshake macro carries the coherent multi-bit payload and
// exposes an acknowledgement only after the destination accepts it.
module multitini_async_mailbox #(
    parameter integer WIDTH = 32
) (
    input  logic             src_clk,
    input  logic             src_resetn,
    input  logic [WIDTH-1:0] src_data,
    input  logic             src_valid,
    output logic             src_ready,

    input  logic             dst_clk,
    input  logic             dst_resetn,
    output logic [WIDTH-1:0] dst_data,
    output logic             dst_valid,
    input  logic             dst_ready
);
    logic [WIDTH-1:0] payload_hold;
    logic [WIDTH-1:0] destination_data;
    logic             source_send;
    logic             source_received;
    logic             source_busy;
    logic             destination_request;
    logic             destination_ack;

    assign src_ready = src_resetn && !source_busy && !source_received;
    assign dst_valid = dst_resetn && destination_request;
    assign dst_data = destination_data;
    assign destination_ack = dst_valid && dst_ready;

    always_ff @(posedge src_clk) begin
        if (!src_resetn) begin
            payload_hold  <= '0;
            source_send    <= 1'b0;
            source_busy    <= 1'b0;
        end else begin
            if (!source_busy) begin
                source_send <= 1'b0;
            end
            if (src_valid && src_ready) begin
                payload_hold   <= src_data;
                source_send    <= 1'b1;
                source_busy    <= 1'b1;
            end else if (source_busy && source_received) begin
                source_send <= 1'b0;
                source_busy <= 1'b0;
            end
        end
    end

    xpm_cdc_handshake #(
        .DEST_EXT_HSK(1),
        .DEST_SYNC_FF(4),
        .INIT_SYNC_FF(0),
        .SIM_ASSERT_CHK(1),
        .SRC_SYNC_FF(4),
        .WIDTH(WIDTH)
    ) handshake_i (
        .dest_ack(destination_ack),
        .dest_clk(dst_clk),
        .dest_out(destination_data),
        .dest_req(destination_request),
        .src_clk(src_clk),
        .src_in(payload_hold),
        .src_rcv(source_received),
        .src_send(source_send)
    );
endmodule
