// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Native-domain endpoints for coherent host request/response mailboxes.
// A loader wins a simultaneous peek. Failure is acknowledged, never retried
// as a RAM access after the host's originally observed pause has ended.
module multitini_c64_host (
    input logic clk, reset,
    input logic [31:0] peek_word, reu_word,
    input logic debug_enable, paused, loader_write,
    output logic peek_request,
    output logic [15:0] peek_address,
    input logic peek_valid,
    input logic [7:0] peek_data,
    output logic [31:0] peek_status,
    output logic invalidate,
    input logic invalidate_done, reu_busy, ua_held,
    output logic [31:0] reu_status
);
    logic [3:0] peek_seen, peek_counter, invalidate_seen, invalidate_counter;
    logic peek_pending, invalidate_pending;
    logic [3:0] invalidate_completed;
    assign reu_status = {17'd0, ua_held, 1'b0, !reu_busy,
                        invalidate_completed, 8'd0};
    always_ff @(posedge clk) begin
        peek_request <= 0;
        invalidate <= 0;
        if (reset) begin
            peek_seen <= 0;
            peek_counter <= 0;
            peek_pending <= 0;
            peek_address <= 0;
            peek_status <= 0;
            invalidate_seen <= 0;
            invalidate_counter <= 0;
            invalidate_completed <= 0;
            invalidate_pending <= 0;
        end else begin
            if (peek_word[19:16] != peek_seen && !peek_pending) begin
                peek_seen <= peek_word[19:16];
                peek_counter <= peek_word[19:16];
                peek_address <= peek_word[15:0];
                if (!debug_enable || !paused || loader_write)
                    peek_status <= {11'd0,1'b1,peek_word[19:16],16'd0};
                else begin
                    peek_request <= 1;
                    peek_pending <= 1;
                end
            end
            // The memory samples the registered request on the next edge;
            // its synchronous response is visible one edge after that.
            if (peek_pending && !peek_request) begin
                peek_pending <= 0;
                peek_status <= {11'd0,!peek_valid,peek_counter,8'd0,
                                peek_valid ? peek_data : 8'd0};
            end
            // The host retries a timed-out clear with a new counter. A cache
            // invalidate is idempotent, so issue that retry even while the
            // earlier request awaits completion. Do not attribute an old
            // same-edge completion to the newly submitted counter.
            if (reu_word[3:0] != invalidate_seen) begin
                invalidate_seen <= reu_word[3:0];
                invalidate_counter <= reu_word[3:0];
                invalidate <= 1;
                invalidate_pending <= 1;
            end else if (invalidate_pending && invalidate_done) begin
                invalidate_completed <= invalidate_counter;
                invalidate_pending <= 0;
            end
        end
    end
endmodule
