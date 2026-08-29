// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// The 33.554432 MHz machine clock is exactly eight times the normal CPU rate.
// Holding phase while stalled prevents any machine clock-enable edge from
// escaping while an external-memory request is incomplete.
module multitini_gameboy_clock_enable (
    input  logic clk,
    input  logic reset,
    input  logic stall,
    output logic ce_cpu,
    output logic ce_cpu_n,
    output logic ce_cpu_2x,
    output logic [2:0] phase
);
    always_ff @(posedge clk) begin
        ce_cpu    <= 1'b0;
        ce_cpu_n  <= 1'b0;
        ce_cpu_2x <= 1'b0;
        if (reset) begin
            phase <= 3'd0;
        end else if (!stall) begin
            ce_cpu    <= phase == 3'd0;
            ce_cpu_n  <= phase == 3'd4;
            ce_cpu_2x <= phase == 3'd0 || phase == 3'd4;
            phase     <= phase + 3'd1;
        end
    end
endmodule
