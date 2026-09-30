// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// A failed media receive may complete before its last byte is consumed. Tag
// each byte so that a queued or late byte from that request is drained, never
// presented as the first byte of a retried SRAM snapshot.
module multitini_genesis_media_stream(
    input wire source_clk,source_resetn,
    input wire [7:0] source_data,
    input wire source_last,source_valid,
    input wire [31:0] source_id,source_generation,
    output wire source_ready,
    input wire control_clk,control_resetn,
    input wire complete,
    input wire [31:0] complete_id,complete_generation,
    output wire [7:0] data,
    output wire last,valid,
    input wire pop
);
    reg completed;
    reg [31:0] completed_id,completed_generation;
    always @(posedge control_clk)begin
        if(!control_resetn)begin completed<=0;completed_id<=0;completed_generation<=0;end
        else if(complete)begin
            completed<=1;completed_id<=complete_id;completed_generation<=complete_generation;
        end
    end
    wire [72:0] received;
    wire received_valid;
    wire [31:0] received_id=received[72:41],received_generation=received[40:9];
    wire discard=(completed && received_id==completed_id && received_generation==completed_generation) ||
        (complete && received_id==complete_id && received_generation==complete_generation);
    assign data=received[7:0],last=received[8],valid=received_valid && !discard;
    multitini_async_mailbox #(.WIDTH(73)) bytes_i(
        .src_clk(source_clk),.src_resetn(source_resetn),
        .src_data({source_id,source_generation,source_last,source_data}),
        .src_valid(source_valid),.src_ready(source_ready),
        .dst_clk(control_clk),.dst_resetn(control_resetn),
        .dst_data(received),.dst_valid(received_valid),.dst_ready(pop || discard));
endmodule
