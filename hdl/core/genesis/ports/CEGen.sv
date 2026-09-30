// SPDX-License-Identifier: GPL-3.0-only
// Original fractional clock-enable accumulator for the MiSTer audio helper.
// The falling edge aligns the pulse before its rising-edge consumers.
module CEGen (
    input wire CLK,
    input wire RST_N,
    input wire [31:0] IN_CLK,
    input wire [31:0] OUT_CLK,
    output reg CE
);
    reg [31:0] phase;
    wire [32:0] next_phase = {1'b0, phase} + {1'b0, OUT_CLK};
    always @(negedge CLK or negedge RST_N) begin
        if (!RST_N) begin
            phase <= 0;
            CE <= 0;
        end else if (next_phase >= {1'b0, IN_CLK}) begin
            phase <= next_phase - {1'b0, IN_CLK};
            CE <= 1;
        end else begin
            phase <= next_phase[31:0];
            CE <= 0;
        end
    end
endmodule
