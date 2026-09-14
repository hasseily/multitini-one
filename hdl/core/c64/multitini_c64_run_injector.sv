// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Types RUN<Return> after a PRG injection. The default cadence matches the
// pinned MiSTer implementation at the PAL/NTSC core clock.
module multitini_c64_run_injector #(
    parameter integer EVENT_DELAY_CYCLES = 1280000
) (
    input  logic        clk,
    input  logic        reset,
    input  logic        pause_request,
    input  logic        start,
    input  logic [10:0] host_ps2_key,
    output logic [10:0] ps2_key,
    output logic        busy
);
    localparam integer COUNTER_WIDTH = $clog2(EVENT_DELAY_CYCLES + 1);

    logic [COUNTER_WIDTH-1:0] counter;
    logic [3:0] phase;
    logic toggle;

    function automatic logic [7:0] phase_code(input logic [3:0] value);
        case (value)
            1, 2: phase_code = 8'h2d; // R
            3, 4: phase_code = 8'h3c; // U
            5, 6: phase_code = 8'h31; // N
            7, 8: phase_code = 8'h5a; // Return
            default: phase_code = 8'h00;
        endcase
    endfunction

    always_ff @(posedge clk) begin
        if (reset) begin
            counter <= '0;
            phase <= 4'd0;
            toggle <= 1'b0;
            busy <= 1'b0;
            ps2_key <= 11'd0;
        end else if (start) begin
            counter <= '0;
            phase <= 4'd1;
            toggle <= host_ps2_key[10];
            busy <= 1'b1;
            // Clear any held code without changing the event toggle.
            ps2_key <= {host_ps2_key[10], 10'd0};
        end else if (busy) begin
            // Menu pause must retain the pending key and its remaining delay;
            // otherwise RUN could be typed while the C64 cannot consume it.
            if (!pause_request) begin
                if (counter == EVENT_DELAY_CYCLES - 1) begin
                    counter <= '0;
                    toggle <= ~toggle;
                    ps2_key[10] <= ~toggle;
                    ps2_key[9] <= phase[0];
                    ps2_key[8] <= 1'b0;
                    ps2_key[7:0] <= phase_code(phase);
                    if (phase == 4'd8) begin
                        phase <= 4'd0;
                        busy <= 1'b0;
                    end else begin
                        phase <= phase + 4'd1;
                    end
                end else begin
                    counter <= counter + 1'd1;
                end
            end
        end else begin
            ps2_key <= host_ps2_key;
        end
    end
endmodule
