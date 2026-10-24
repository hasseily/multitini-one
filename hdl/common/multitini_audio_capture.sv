// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Control-domain 48 kHz capture, before output mute and test-tone selection.
// A prefetched head keeps AXI reads single-cycle while the sample store maps
// to one RAMB18. On overflow preserve existing samples and mark the take bad.
module multitini_audio_capture (
    input logic clk, resetn,
    input logic debug_enable, run, clear,
    input logic sample_tick,
    input logic signed [23:0] sample_l, sample_r,
    input logic pop,
    output logic [31:0] data,
    output logic [31:0] status
);
    (* ram_style = "block" *) logic [31:0] samples [0:511];
    logic [8:0] write_ptr, read_ptr;
    logic [9:0] count;
    logic overflow;
    logic [31:0] memory_head, bypass_head;
    logic head_bypass;
    wire [31:0] head = head_bypass ? bypass_head : memory_head;
    wire take = pop && debug_enable && count != 0;
    wire put = debug_enable && run && sample_tick &&
               (count != 512 || take);
    wire [31:0] sample_word = {sample_l[23:8], sample_r[23:8]};
    assign data = debug_enable && count != 0 ? head : 32'd0;
    assign status = {19'd0, 1'b1, debug_enable, overflow, count};
    always_ff @(posedge clk) begin
        if (put) samples[write_ptr] <= sample_word;
        // Explicit same-address bypass makes FIFO behaviour independent of
        // inferred BRAM read-during-write semantics.
        if (take && count > 1) memory_head <= samples[read_ptr + 9'd1];
    end
    always_ff @(posedge clk) begin
        // Keep the RAM output register separate from the same-address
        // bypass. A data mux on that register prevents RAMB18 inference.
        if (take && count > 1) head_bypass <= 0;
        else if (put && (count == 0 || (take && count == 1))) begin
            head_bypass <= 1;
            bypass_head <= sample_word;
        end
        if (!resetn || clear || !debug_enable) begin
            write_ptr <= 0;
            read_ptr <= 0;
            count <= 0;
            overflow <= 0;
        end else begin
            if (take) read_ptr <= read_ptr + 9'd1;
            if (put) write_ptr <= write_ptr + 9'd1;
            case ({put,take})
                2'b10: count <= count + 10'd1;
                2'b01: count <= count - 10'd1;
                default: ;
            endcase
            if (run && sample_tick && !put) overflow <= 1;
        end
    end
endmodule
