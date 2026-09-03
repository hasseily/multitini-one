// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Focused scheduling regression for the dual-SID output latch. The real SID
// filter reuses one arithmetic pipeline for the left and right instances. Its
// result changes at intermediate states and becomes valid one clock after
// state 7. These small stubs preserve that timing without pulling the large
// waveform tables into a simulator that cannot compile their upstream
// declaration order.

module sid_voice (
    input clock,
    input ce_1m,
    input reset,
    input mode,
    input [15:0] freq,
    input [11:0] pw,
    input [7:0] control,
    input [7:0] att_dec,
    input [7:0] sus_rel,
    input osc_msb_in,
    input [7:0] _st_out,
    input [7:0] p_t_out,
    input [7:0] ps__out,
    input [7:0] pst_out,
    output [11:0] acc_t,
    output osc_msb_out,
    output [21:0] voice_out,
    output [7:0] osc_out,
    output [7:0] env_out
);
    assign acc_t = 12'd0;
    assign osc_msb_out = 1'b0;
    assign voice_out = 22'd0;
    assign osc_out = 8'd0;
    assign env_out = 8'd0;
endmodule

module sid_tables #(
    parameter MULTI_FILTERS = 1
) (
    input clock,
    input mode,
    input [11:0] acc_t,
    output reg [7:0] _st_out,
    output reg [7:0] p_t_out,
    output reg [7:0] ps__out,
    output reg [7:0] pst_out,
    input [1:0] cfg,
    input [10:0] Fc,
    input [12:0] Fc_offset,
    output [15:0] F0,
    input ld_clk,
    input [11:0] ld_addr,
    input [15:0] ld_data,
    input ld_wr
);
    assign F0 = 16'd0;
    always @* begin
        _st_out = 8'd0;
        p_t_out = 8'd0;
        ps__out = 8'd0;
        pst_out = 8'd0;
    end
endmodule

module sid_filter (
    input clk,
    input [2:0] state,
    input mode,
    input [15:0] F0,
    input [7:0] Res_Filt,
    input [7:0] Mode_Vol,
    input signed [21:0] voice1,
    input signed [21:0] voice2,
    input signed [21:0] voice3,
    input signed [21:0] ext_in,
    output reg [17:0] audio
);
    initial audio = 18'd0;

    // State 2 represents an unrelated intermediate MAC result. State 7
    // launches the audio MAC; its nonblocking result is visible after that
    // edge, so a consumer on the same edge must wait one more clock.
    always @(posedge clk) begin
        if (state == 3'd2)
            audio <= 18'h05555;
        else if (state == 3'd7)
            audio <= 18'h12345;
    end
endmodule

module tb_sid_output_schedule;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic ce_1m = 1'b0;
    logic [1:0] cs = 2'b00;
    logic [17:0] audio_l;
    logic [17:0] audio_r;
    integer cycle = 0;
    integer mismatches = 0;

    always #5 clk = !clk;

    sid_top dut (
        .reset(reset), .clk(clk), .ce_1m(ce_1m),
        .cs(cs), .we(1'b0), .addr(5'd0), .data_in(8'd0), .data_out(),
        .fc_offset_l(13'd0), .pot_x_l(8'hff), .pot_y_l(8'hff),
        .ext_in_l(18'd0), .audio_l(audio_l),
        .fc_offset_r(13'd0), .pot_x_r(8'hff), .pot_y_r(8'hff),
        .ext_in_r(18'd0), .audio_r(audio_r),
        .filter_en(2'b11), .mode(2'b00), .cfg(4'b0000),
        .ld_clk(clk), .ld_addr(12'd0), .ld_data(16'd0), .ld_wr(1'b0)
    );

    always @(negedge clk) begin
        cycle = cycle + 1;
        ce_1m = (cycle[4:0] == 5'd31);

        if (!reset && !$isunknown({audio_l, audio_r}) &&
            audio_l != audio_r) begin
            mismatches = mismatches + 1;
            $display("SID mono mismatch cycle=%0d state=%0d left=%05x right=%05x",
                     cycle, dut.state, audio_l, audio_r);
        end
    end

    initial begin
        repeat (4) @(negedge clk);
        reset = 1'b0;

        repeat (128) @(negedge clk);
        if (mismatches != 0) begin
            $display("FAIL: mono dual-SID output exposed %0d transient mismatches",
                     mismatches);
            $fatal(1);
        end else if (audio_l != 18'h12345 || audio_r != 18'h12345) begin
            $display("FAIL: final SID pair did not settle: %05x/%05x",
                     audio_l, audio_r);
            $fatal(1);
        end else begin
            $display("PASS: mono dual-SID output updates as a coherent pair");
            $finish;
        end
    end
endmodule
