// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Retain upstream's compatibility rule for FPGA64 Smart CPU turbo: suspend it
// during serial-disk traffic and for a settling interval after the IEC bus
// becomes idle. The emulated drive itself always keeps authentic timing.
module multitini_c64_cpu_turbo_guard #(
    parameter integer CORE_CLOCK_HZ = 31527954,
    parameter integer IEC_HOLDOFF_CYCLES = (CORE_CLOCK_HZ + 1) / 2
) (
    input  logic clk,
    input  logic reset,
    input  logic turbo_request,
    input  logic c64_iec_atn,
    input  logic c64_iec_data,
    input  logic c64_iec_clk,
    input  logic drive_iec_data,
    input  logic drive_iec_clk,
    output logic turbo_active
);
    logic [24:0] holdoff_cycles;
    logic previous_c64_iec_atn;
    logic previous_c64_iec_data;
    logic previous_c64_iec_clk;
    logic previous_drive_iec_data;
    logic previous_drive_iec_clk;
    logic iec_transition;
    logic iec_lines_released;

    assign iec_transition =
        c64_iec_atn != previous_c64_iec_atn ||
        c64_iec_data != previous_c64_iec_data ||
        c64_iec_clk != previous_c64_iec_clk ||
        drive_iec_data != previous_drive_iec_data ||
        drive_iec_clk != previous_drive_iec_clk;
    assign iec_lines_released = c64_iec_atn && c64_iec_data &&
                                c64_iec_clk && drive_iec_data &&
                                drive_iec_clk;
    assign turbo_active = turbo_request && iec_lines_released &&
                          !iec_transition && holdoff_cycles == 25'd0;

    always_ff @(posedge clk) begin
        if (reset) begin
            previous_c64_iec_atn    <= 1'b1;
            previous_c64_iec_data   <= 1'b1;
            previous_c64_iec_clk    <= 1'b1;
            previous_drive_iec_data <= 1'b1;
            previous_drive_iec_clk  <= 1'b1;
            holdoff_cycles <= 25'd0;
        end else begin
            previous_c64_iec_atn    <= c64_iec_atn;
            previous_c64_iec_data   <= c64_iec_data;
            previous_c64_iec_clk    <= c64_iec_clk;
            previous_drive_iec_data <= drive_iec_data;
            previous_drive_iec_clk  <= drive_iec_clk;
            if (!iec_lines_released || iec_transition)
                holdoff_cycles <= IEC_HOLDOFF_CYCLES;
            else if (holdoff_cycles != 25'd0)
                holdoff_cycles <= holdoff_cycles - 25'd1;
        end
    end
endmodule
