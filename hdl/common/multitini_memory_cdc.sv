// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Carries the common one-outstanding line-memory protocol between unrelated
// core and memory clocks. Reset aborts any transaction in flight.
module multitini_memory_cdc (
    input  logic         core_clk,
    input  logic         core_resetn,
    input  logic [23:0]  core_request_addr,
    input  logic         core_request_write,
    input  logic [63:0]  core_request_wdata,
    input  logic [7:0]   core_request_wstrb,
    input  logic         core_request_valid,
    output logic         core_request_ready,
    output logic [63:0]  core_response_rdata,
    output logic         core_response_valid,

    input  logic         memory_clk,
    input  logic         memory_resetn,
    output logic [23:0]  memory_request_addr,
    output logic         memory_request_write,
    output logic [63:0]  memory_request_wdata,
    output logic [7:0]   memory_request_wstrb,
    output logic         memory_request_valid,
    input  logic         memory_request_ready,
    input  logic [63:0]  memory_response_rdata,
    input  logic         memory_response_valid,
    output wire          core_busy,
    output wire          memory_busy
);
    localparam integer REQUEST_WIDTH = 97;

    logic [REQUEST_WIDTH-1:0] request_source;
    logic [REQUEST_WIDTH-1:0] request_destination;
    logic request_source_ready;
    logic request_destination_ready;
    logic core_outstanding;

    logic [63:0] response_hold;
    logic response_source_valid;
    logic response_source_ready;
    logic memory_outstanding;

    assign core_busy=core_outstanding || core_request_valid;
    assign memory_busy=memory_outstanding || memory_request_valid || response_source_valid;

    assign request_source = {core_request_addr, core_request_write,
                             core_request_wdata, core_request_wstrb};
    assign core_request_ready = request_source_ready && !core_outstanding;
    assign request_destination_ready = memory_request_ready &&
                                       !memory_outstanding;
    assign {memory_request_addr, memory_request_write,
            memory_request_wdata, memory_request_wstrb} = request_destination;

    multitini_async_mailbox #(.WIDTH(REQUEST_WIDTH)) request_mailbox_i (
        .src_clk(core_clk), .src_resetn(core_resetn),
        .src_data(request_source),
        .src_valid(core_request_valid && !core_outstanding),
        .src_ready(request_source_ready),
        .dst_clk(memory_clk), .dst_resetn(memory_resetn),
        .dst_data(request_destination), .dst_valid(memory_request_valid),
        .dst_ready(request_destination_ready)
    );

    multitini_async_mailbox #(.WIDTH(64)) response_mailbox_i (
        .src_clk(memory_clk), .src_resetn(memory_resetn),
        .src_data(response_hold), .src_valid(response_source_valid),
        .src_ready(response_source_ready),
        .dst_clk(core_clk), .dst_resetn(core_resetn),
        .dst_data(core_response_rdata), .dst_valid(core_response_valid),
        .dst_ready(1'b1)
    );

    always_ff @(posedge core_clk) begin
        if (!core_resetn) begin
            core_outstanding <= 1'b0;
        end else begin
            if (core_request_valid && core_request_ready)
                core_outstanding <= 1'b1;
            if (core_response_valid)
                core_outstanding <= 1'b0;
        end
    end

    always_ff @(posedge memory_clk) begin
        if (!memory_resetn) begin
            memory_outstanding <= 1'b0;
            response_source_valid <= 1'b0;
            response_hold <= 64'd0;
        end else begin
            if (memory_request_valid && request_destination_ready)
                memory_outstanding <= 1'b1;
            if (memory_response_valid && memory_outstanding &&
                !response_source_valid) begin
                response_hold <= memory_response_rdata;
                response_source_valid <= 1'b1;
                memory_outstanding <= 1'b0;
            end
            if (response_source_valid && response_source_ready)
                response_source_valid <= 1'b0;
        end
    end
endmodule
