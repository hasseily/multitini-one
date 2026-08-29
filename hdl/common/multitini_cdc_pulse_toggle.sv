// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Sparse event crossing for reset-coordinated clock domains. Source events
// must be separated by at least three destination clocks so toggles cannot
// cancel before observation.
module multitini_cdc_pulse_toggle (
    input  logic src_clk,
    input  logic src_resetn,
    input  logic src_pulse,
    input  logic dst_clk,
    input  logic dst_resetn,
    output logic dst_pulse
);
    logic src_toggle;
    (* ASYNC_REG = "TRUE" *) logic [2:0] dst_sync;

    always_ff @(posedge src_clk) begin
        if (!src_resetn)
            src_toggle <= 1'b0;
        else if (src_pulse)
            src_toggle <= ~src_toggle;
    end

    always_ff @(posedge dst_clk) begin
        if (!dst_resetn)
            dst_sync <= 3'b000;
        else
            dst_sync <= {dst_sync[1:0], src_toggle};
    end

    assign dst_pulse = dst_sync[2] ^ dst_sync[1];
endmodule
