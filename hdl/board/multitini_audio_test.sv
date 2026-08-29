// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module multitini_audio_test (
    input  logic control_clk,
    input  logic control_resetn,
    input  logic audio_mclk,
    input  logic enable_async,
    input  logic mute_async,
    output logic i2s_lrck,
    output logic i2s_sdata,
    output logic i2s_bclk,
    output logic spdif_out
);
    logic [1:0] audio_bclk_div = 2'b00;
    logic audio_mclk_resetn;
    logic audio_bclk_resetn;
    logic enable_bclk;
    logic mute_bclk;
    logic [31:0] sample_tick_acc;
    logic [31:0] tone_phase;
    logic signed [23:0] sample_l;
    logic signed [23:0] sample_r;
    wire [32:0] sample_tick_next = {1'b0, sample_tick_acc} + 33'd1546188;

    reset_sync reset_sync_audio_mclk (
        .clk(audio_mclk), .arst_n(control_resetn), .srst_n(audio_mclk_resetn)
    );

    always_ff @(posedge audio_mclk or negedge audio_mclk_resetn) begin
        if (!audio_mclk_resetn)
            audio_bclk_div <= 2'b00;
        else
            audio_bclk_div <= audio_bclk_div + 2'b01;
    end
    assign i2s_bclk = audio_bclk_div[1];

    reset_sync reset_sync_audio_bclk (
        .clk(i2s_bclk), .arst_n(audio_mclk_resetn), .srst_n(audio_bclk_resetn)
    );
    cdc_bit_sync sync_audio_enable (
        .clk(i2s_bclk), .resetn(audio_bclk_resetn),
        .din(enable_async), .dout(enable_bclk)
    );
    cdc_bit_sync sync_audio_mute (
        .clk(i2s_bclk), .resetn(audio_bclk_resetn),
        .din(mute_async), .dout(mute_bclk)
    );

    audio_i2s_tone i2s_tone (
        .bclk_in(i2s_bclk), .resetn(audio_bclk_resetn),
        .enable(enable_bclk), .mute(mute_bclk),
        .tone_step(32'd78741067), .amplitude(24'h200000),
        .i2s_bclk(), .i2s_lrck(i2s_lrck), .i2s_sdata(i2s_sdata)
    );

    always_ff @(posedge control_clk) begin
        if (!control_resetn) begin
            sample_tick_acc <= 32'd0;
            tone_phase <= 32'd0;
            sample_l <= 24'sd0;
            sample_r <= 24'sd0;
        end else begin
            sample_tick_acc <= sample_tick_next[31:0];
            if (sample_tick_next[32]) begin
                tone_phase <= tone_phase + 32'd78741067;
                sample_l <= tone_phase[31] ? -24'sh200000 : 24'sh200000;
                sample_r <= tone_phase[31] ? -24'sh200000 : 24'sh200000;
            end
        end
    end

    audio_spdif_pcm_tx spdif_tx (
        .clk(control_clk), .resetn(control_resetn),
        .enable(enable_async), .mute(mute_async),
        .sample_l(sample_l), .sample_r(sample_r), .spdif_out(spdif_out)
    );

endmodule
