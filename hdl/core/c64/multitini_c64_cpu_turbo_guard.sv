// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Retain upstream's compatibility rule for FPGA64 Smart CPU turbo: suspend it
// during serial-disk traffic and for a settling interval after the IEC bus
// becomes idle. Strobes on the optional parallel drive cable are treated the
// same way, because a fast ROM pair need not hold IEC CLK low while it moves
// bytes over the cable. The emulated drive itself always keeps authentic
// timing.
module multitini_c64_cpu_turbo_guard #(
    parameter integer CORE_CLOCK_HZ = 31527954,
    parameter integer IEC_HOLDOFF_CYCLES = (CORE_CLOCK_HZ + 1) / 2,
    parameter integer IEC_HOLDOFF_100MS_CYCLES = (CORE_CLOCK_HZ + 9) / 10,
    parameter integer IEC_HOLDOFF_50MS_CYCLES = (CORE_CLOCK_HZ + 19) / 20,
    parameter integer IEC_HOLDOFF_20MS_CYCLES = (CORE_CLOCK_HZ + 49) / 50,
    parameter integer STATIC_HOLD_CYCLES = 2 * CORE_CLOCK_HZ
) (
    input  logic clk,
    input  logic reset,
    input  logic turbo_request,
    input  logic c64_iec_atn,
    input  logic c64_iec_data,
    input  logic c64_iec_clk,
    input  logic drive_iec_data,
    input  logic drive_iec_clk,
    input  logic c64_par_stb,
    input  logic drive_par_stb,
    input logic [1:0] holdoff_select,
    input logic static_hold_release,
    output logic turbo_active,
    output logic [1:0] block_cause
);
    logic [24:0] holdoff_cycles;
    logic [31:0] static_cycles;
    logic [24:0] selected_holdoff;
    logic previous_c64_iec_atn;
    logic previous_c64_iec_data;
    logic previous_c64_iec_clk;
    logic previous_drive_iec_data;
    logic previous_drive_iec_clk;
    logic previous_c64_par_stb;
    logic previous_drive_par_stb;
    logic iec_transition;
    logic iec_lines_released;

    assign iec_transition =
        c64_iec_atn != previous_c64_iec_atn ||
        c64_iec_data != previous_c64_iec_data ||
        c64_iec_clk != previous_c64_iec_clk ||
        drive_iec_data != previous_drive_iec_data ||
        drive_iec_clk != previous_drive_iec_clk ||
        c64_par_stb != previous_c64_par_stb ||
        drive_par_stb != previous_drive_par_stb;
    assign iec_lines_released = c64_iec_atn && c64_iec_data &&
                                c64_iec_clk && drive_iec_data &&
                                drive_iec_clk && c64_par_stb &&
                                drive_par_stb;
    // Unconnected optional ports retain the original 500 ms policy.
    always_comb begin
        case(holdoff_select)
            2'd1:selected_holdoff=IEC_HOLDOFF_100MS_CYCLES;
            2'd2:selected_holdoff=IEC_HOLDOFF_50MS_CYCLES;
            2'd3:selected_holdoff=IEC_HOLDOFF_20MS_CYCLES;
            default:selected_holdoff=IEC_HOLDOFF_CYCLES;
        endcase
    end
    wire static_released=(static_hold_release===1'b1) && static_cycles>=STATIC_HOLD_CYCLES;
    assign turbo_active = turbo_request && (iec_lines_released || static_released) &&
                          !iec_transition && holdoff_cycles == 25'd0;
    always_comb begin
        block_cause=0;
        if(turbo_request) begin
            if(!static_released && !(c64_iec_atn && c64_iec_data && c64_iec_clk && drive_iec_data && drive_iec_clk)) block_cause=1;
            else if(!static_released && !(c64_par_stb && drive_par_stb)) block_cause=2;
            else if(iec_transition || holdoff_cycles!=0) block_cause=3;
        end
    end

    always_ff @(posedge clk) begin
        if (reset) begin
            previous_c64_iec_atn    <= 1'b1;
            previous_c64_iec_data   <= 1'b1;
            previous_c64_iec_clk    <= 1'b1;
            previous_drive_iec_data <= 1'b1;
            previous_drive_iec_clk  <= 1'b1;
            previous_c64_par_stb    <= 1'b1;
            previous_drive_par_stb  <= 1'b1;
            holdoff_cycles <= 25'd0;
            static_cycles<=0;
        end else begin
            previous_c64_iec_atn    <= c64_iec_atn;
            previous_c64_iec_data   <= c64_iec_data;
            previous_c64_iec_clk    <= c64_iec_clk;
            previous_drive_iec_data <= drive_iec_data;
            previous_drive_iec_clk  <= drive_iec_clk;
            previous_c64_par_stb    <= c64_par_stb;
            previous_drive_par_stb  <= drive_par_stb;
            if(iec_transition) static_cycles<=0;
            else if(static_cycles<STATIC_HOLD_CYCLES) static_cycles<=static_cycles+1'b1;
            if (iec_transition || (!iec_lines_released && !static_released))
                holdoff_cycles <= selected_holdoff;
            else if(static_released) holdoff_cycles<=0;
            else if (holdoff_cycles != 25'd0)
                holdoff_cycles <= holdoff_cycles - 25'd1;
        end
    end
endmodule
