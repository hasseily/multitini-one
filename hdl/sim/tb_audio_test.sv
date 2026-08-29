// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_audio_test;
    logic control_clk = 1'b0;
    logic control_resetn = 1'b0;
    logic audio_mclk = 1'b0;
    logic enable = 1'b0;
    logic mute = 1'b1;
    logic lrck;
    logic sdata;
    logic bclk;
    logic spdif;
    integer lrck_edges = 0;

    always #4 control_clk = !control_clk;
    always #41 audio_mclk = !audio_mclk;
    always @(lrck) if (control_resetn) lrck_edges = lrck_edges + 1;

    multitini_audio_test dut (
        .control_clk(control_clk), .control_resetn(control_resetn),
        .audio_mclk(audio_mclk), .enable_async(enable), .mute_async(mute),
        .i2s_lrck(lrck), .i2s_sdata(sdata), .i2s_bclk(bclk),
        .spdif_out(spdif)
    );

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    initial begin
        repeat (4) @(negedge audio_mclk);
        control_resetn = 1'b1;
        enable = 1'b1;
        repeat (140) @(negedge bclk);
        #1;
        check_value(lrck_edges >= 4, "48 kHz frame clock advances");
        check_value(!$isunknown({lrck, sdata, bclk, spdif}),
               "audio outputs are known");

        enable = 1'b0;
        repeat (8) @(negedge bclk);
        #1;
        check_value(!lrck && !sdata && !spdif, "disabled audio is quiet");
        $display("PASS: audio diagnostic path");
        $finish;
    end
endmodule
