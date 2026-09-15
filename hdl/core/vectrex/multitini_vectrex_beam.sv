// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Vectrex analogue state expressed as fixed-point integrator coordinates.
// One native 6809 cycle is 64 fabric clocks at 96 MHz. Q6 coordinates gain
// one DAC unit per CPU cycle. Control settling and beam blanking are separate:
// zero/ramp/DAC reach the integrators after ANALOG_DELAY_CLOCKS; CB2 blanks
// immediately. This is original machine logic, not an imported raster core.
module multitini_vectrex_beam #(
    parameter integer ANALOG_DELAY_CLOCKS = 816
) (
    input wire clk, reset, pause,
    input wire sample_tick,
    input wire [7:0] porta, portb,
    input wire ca2, cb2,
    output reg signed [23:0] x, y,
    output reg [7:0] intensity,
    output reg blank,
    output reg point_valid,
    output reg signed [7:0] dac_audio
);
    localparam integer DELAY_WIDTH = $clog2(ANALOG_DELAY_CLOCKS + 1);
    // Circular read-before-write delay avoids a large resettable shift chain.
    // One registered read adds the final clock of delay. priming excludes
    // contents left by a previous reset without resetting the memory itself.
    (* ram_style="block" *) reg [16:0] delayed [0:ANALOG_DELAY_CLOCKS-2];
    reg [DELAY_WIDTH-1:0] delay_pointer;
    reg [16:0] controls;
    reg [DELAY_WIDTH-1:0] priming;
    wire signed [7:0] dac_x = controls[7:0];
    wire [7:0] pb = controls[15:8];
    reg signed [7:0] held_y, reference;
    reg [6:0] held_z;
    wire signed [8:0] dx = {dac_x[7],dac_x} - {reference[7],reference};
    wire signed [8:0] dy = {held_y[7],held_y} - {reference[7],reference};
    reg previous_cb2;

    always @(posedge clk) begin
        if (!pause) begin
            controls <= delayed[delay_pointer];
            delayed[delay_pointer] <= {ca2,portb,porta};
            delay_pointer <= delay_pointer==ANALOG_DELAY_CLOCKS-2 ? 0 : delay_pointer+1'b1;
        end
        point_valid <= 1'b0;
        if (reset) begin
            priming <= 0;
            delay_pointer <= 0;
            x <= 0; y <= 0;
            held_y <= 0; reference <= 0; held_z <= 0;
            intensity <= 0; blank <= 1'b1;
            previous_cb2 <= 1'b0;
            dac_audio <= 0;
        end else if (!pause) begin
            previous_cb2 <= cb2;
            if (priming < ANALOG_DELAY_CLOCKS) begin
                priming <= priming + 1'b1;
            end else begin
                if (!pb[0]) begin
                    case (pb[2:1])
                        2'd0: held_y <= dac_x;
                        2'd1: reference <= dac_x;
                        2'd2: held_z <= dac_x[7] ? 7'd0 : dac_x[6:0];
                        2'd3: dac_audio <= dac_x;
                    endcase
                end
                if (!controls[16]) begin
                    x <= 0; y <= 0;
                end else if (!pb[7]) begin
                    x <= x + {{15{dx[8]}},dx};
                    y <= y + {{15{dy[8]}},dy};
                end
                intensity <= {held_z,held_z[6]};
                blank <= !cb2;
                // Sample regularly and on blank edges so short dots and
                // disconnected strokes do not acquire connecting lines.
                point_valid <= sample_tick || cb2 != previous_cb2;
            end
        end
    end
endmodule
