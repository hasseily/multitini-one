// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Shared physical audio output. Machine PCM is held as the latest coherent
// stereo sample in each output clock domain. The independent 48 kHz clocks
// therefore perform a bounded zero-order-hold rate conversion without an
// async FIFO that would eventually overrun or underrun from oscillator drift.
// AUDIO_TEST_ENABLE selects the known 880 Hz diagnostic source; otherwise the
// ready machine's live PCM is selected. AUDIO_MUTE dominates both sources.
module multitini_audio_test (
    input  logic               machine_clk,
    input  logic               machine_resetn,
    input  logic               machine_ready,
    input  logic signed [23:0] machine_audio_l,
    input  logic signed [23:0] machine_audio_r,
    input  logic               machine_audio_valid,

    input  logic               control_clk,
    input  logic               control_resetn,
    input  logic               audio_mclk,
    input  logic               test_tone_async,
    input  logic               mute_async,
    output logic               i2s_lrck,
    output logic               i2s_sdata,
    output logic               i2s_bclk,
    output logic               spdif_out
);
    logic [1:0] audio_bclk_div = 2'b00;
    logic audio_mclk_resetn;
    logic audio_bclk_resetn;
    logic test_tone_bclk;
    logic mute_bclk;
    logic machine_ready_launch;
    logic machine_ready_bclk;
    logic machine_ready_control;

    logic [48:0] machine_audio_source;
    logic        machine_audio_resend;
    logic [48:0] machine_audio_bclk;
    logic [48:0] machine_audio_control;
    logic        live_enable_bclk;
    logic        live_enable_control;

    logic        pcm_i2s_lrck;
    logic        pcm_i2s_sdata;
    logic        tone_i2s_lrck;
    logic        tone_i2s_sdata;
    logic        source_test_bclk;
    logic [5:0]  audio_frame_bit;

    logic [31:0] sample_tick_acc;
    logic [31:0] tone_phase;
    logic signed [23:0] tone_sample_l;
    logic signed [23:0] tone_sample_r;
    logic signed [23:0] spdif_sample_l;
    logic signed [23:0] spdif_sample_r;
    logic               spdif_mute;

    wire [32:0] sample_tick_next =
        {1'b0, sample_tick_acc} + 33'd1546188;

    // Register the sample and its validity together before starting either
    // handshake. Clearing the valid bit while the core is unavailable keeps
    // a held pre-reset sample from replaying after a ROM load or soft reset.
    always_ff @(posedge machine_clk or negedge machine_resetn) begin
        if (!machine_resetn) begin
            machine_ready_launch <= 1'b0;
            machine_audio_source <= 49'd0;
            machine_audio_resend <= 1'b0;
        end else begin
            machine_audio_resend <= 1'b0;
            if (!machine_ready) begin
                // Do not rearm after reset/load until a fresh sample arrives.
                // This also launches the combinational ready condition through
                // a machine-domain register before either CDC synchronizer.
                machine_ready_launch <= 1'b0;
                machine_audio_source <= 49'd0;
            end else if (machine_audio_valid) begin
                machine_ready_launch <= 1'b1;
                machine_audio_source <= {
                    1'b1, machine_audio_l, machine_audio_r
                };
                machine_audio_resend <= 1'b1;
            end
        end
    end

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
    cdc_bit_sync sync_audio_test (
        .clk(i2s_bclk), .resetn(audio_bclk_resetn),
        .din(test_tone_async), .dout(test_tone_bclk)
    );
    cdc_bit_sync sync_audio_mute (
        .clk(i2s_bclk), .resetn(audio_bclk_resetn),
        .din(mute_async), .dout(mute_bclk)
    );
    cdc_bit_sync sync_machine_ready_bclk (
        .clk(i2s_bclk), .resetn(audio_bclk_resetn),
        .din(machine_ready_launch), .dout(machine_ready_bclk)
    );
    cdc_bit_sync sync_machine_ready_control (
        .clk(control_clk), .resetn(control_resetn),
        .din(machine_ready_launch), .dout(machine_ready_control)
    );

    // The XPM handshakes keep all 49 bits coherent. Re-sending every source
    // event also transfers identical consecutive PCM values.
    cdc_handshake_reg #(.WIDTH(49)) machine_audio_to_bclk (
        .wr_clk(machine_clk), .wr_rstn(machine_resetn),
        .din(machine_audio_source), .resend(machine_audio_resend),
        .rd_clk(i2s_bclk), .rd_rstn(audio_bclk_resetn),
        .dout(machine_audio_bclk)
    );
    cdc_handshake_reg #(.WIDTH(49)) machine_audio_to_control (
        .wr_clk(machine_clk), .wr_rstn(machine_resetn),
        .din(machine_audio_source), .resend(machine_audio_resend),
        .rd_clk(control_clk), .rd_rstn(control_resetn),
        .dout(machine_audio_control)
    );

    assign live_enable_bclk = machine_ready_bclk && machine_audio_bclk[48];
    assign live_enable_control =
        machine_ready_control && machine_audio_control[48];

    // Both serializers free-run from the same reset and BCLK. Select only at
    // the I2S frame boundary, where both SDATA streams carry the required
    // one-bit delay zero. LRCK always comes from the PCM serializer.
    always_ff @(negedge i2s_bclk or negedge audio_bclk_resetn) begin
        if (!audio_bclk_resetn) begin
            audio_frame_bit <= 6'd0;
            source_test_bclk <= 1'b0;
        end else begin
            if (audio_frame_bit == 6'd0)
                source_test_bclk <= test_tone_bclk;
            audio_frame_bit <= audio_frame_bit + 6'd1;
        end
    end

    audio_i2s_pcm_tx i2s_pcm (
        .bclk_in(i2s_bclk), .resetn(audio_bclk_resetn),
        .enable(1'b1), .mute(mute_bclk || !live_enable_bclk),
        .sample_l(machine_audio_bclk[47:24]),
        .sample_r(machine_audio_bclk[23:0]),
        .i2s_bclk(), .i2s_lrck(pcm_i2s_lrck),
        .i2s_sdata(pcm_i2s_sdata)
    );

    audio_i2s_tone i2s_tone (
        .bclk_in(i2s_bclk), .resetn(audio_bclk_resetn),
        .enable(1'b1), .mute(mute_bclk),
        .tone_step(32'd78741067), .amplitude(24'h200000),
        .i2s_bclk(), .i2s_lrck(tone_i2s_lrck),
        .i2s_sdata(tone_i2s_sdata)
    );

    assign i2s_lrck = pcm_i2s_lrck;
    assign i2s_sdata = source_test_bclk ?
                       tone_i2s_sdata : pcm_i2s_sdata;

    // Generate the diagnostic PCM at the exact nominal 48 kHz sample rate
    // used by the 133.333333 MHz control clock. Live PCM is already held in
    // this domain by the coherent handshake above.
    always_ff @(posedge control_clk) begin
        if (!control_resetn) begin
            sample_tick_acc <= 32'd0;
            tone_phase <= 32'd0;
            tone_sample_l <= 24'sd0;
            tone_sample_r <= 24'sd0;
        end else begin
            sample_tick_acc <= sample_tick_next[31:0];
            if (sample_tick_next[32]) begin
                tone_phase <= tone_phase + 32'd78741067;
                tone_sample_l <= tone_phase[31] ?
                                 -24'sh200000 : 24'sh200000;
                tone_sample_r <= tone_phase[31] ?
                                 -24'sh200000 : 24'sh200000;
            end
        end
    end

    always_comb begin
        if (test_tone_async) begin
            spdif_sample_l = tone_sample_l;
            spdif_sample_r = tone_sample_r;
        end else begin
            spdif_sample_l = machine_audio_control[47:24];
            spdif_sample_r = machine_audio_control[23:0];
        end
        spdif_mute = mute_async ||
                     (!test_tone_async && !live_enable_control);
    end

    multitini_audio_spdif_pcm_tx spdif_tx (
        .clk(control_clk), .resetn(control_resetn),
        .enable(1'b1), .mute(spdif_mute),
        .sample_l(spdif_sample_l), .sample_r(spdif_sample_r),
        .spdif_out(spdif_out)
    );

    // Since both serializers share their clock/reset and run continuously,
    // their LRCK phases must remain identical. Keep the second LRCK visible to
    // lint and to the focused testbench without using it at the board pin.
    wire unused_tone_lrck = tone_i2s_lrck;
endmodule
