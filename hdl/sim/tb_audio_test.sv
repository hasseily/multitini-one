// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_audio_test;
    logic machine_clk = 1'b0;
    logic machine_resetn = 1'b0;
    logic machine_ready = 1'b0;
    logic signed [23:0] machine_audio_l = 24'sd0;
    logic signed [23:0] machine_audio_r = 24'sd0;
    logic machine_audio_valid = 1'b0;

    logic control_clk = 1'b0;
    logic control_resetn = 1'b0;
    logic audio_mclk = 1'b0;
    logic test_tone = 1'b0;
    logic mute = 1'b1;
    logic lrck;
    logic sdata;
    logic bclk;
    logic spdif;

    logic pair_enable = 1'b0;
    logic pair_mute = 1'b0;
    logic signed [23:0] pair_l = 24'sd0;
    logic signed [23:0] pair_r = 24'sd0;
    logic pair_spdif;
    integer pair_left_events = 0;
    integer pair_right_events = 0;
    logic [31:0] pair_last_left = 32'd0;
    logic [31:0] pair_last_right = 32'd0;
    integer lrck_edges = 0;

    always #7.0 machine_clk = !machine_clk;
    always #3.75 control_clk = !control_clk;
    always #40.69 audio_mclk = !audio_mclk;
    always @(lrck) if (control_resetn) lrck_edges = lrck_edges + 1;

    multitini_audio_test dut (
        .machine_clk(machine_clk), .machine_resetn(machine_resetn),
        .machine_ready(machine_ready),
        .machine_audio_l(machine_audio_l),
        .machine_audio_r(machine_audio_r),
        .machine_audio_valid(machine_audio_valid),
        .control_clk(control_clk), .control_resetn(control_resetn),
        .audio_mclk(audio_mclk), .test_tone_async(test_tone),
        .mute_async(mute), .i2s_lrck(lrck), .i2s_sdata(sdata),
        .i2s_bclk(bclk), .spdif_out(spdif)
    );

    // A second transmitter isolates S/PDIF rate and stereo-pair behavior from
    // the integration CDC while still exercising the exact board module.
    multitini_audio_spdif_pcm_tx pair_tx (
        .clk(control_clk), .resetn(control_resetn),
        .enable(pair_enable), .mute(pair_mute),
        .sample_l(pair_l), .sample_r(pair_r), .spdif_out(pair_spdif)
    );

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic submit_machine_sample(
        input logic signed [23:0] left,
        input logic signed [23:0] right
    );
        @(negedge machine_clk);
        machine_audio_l = left;
        machine_audio_r = right;
        machine_audio_valid = 1'b1;
        @(negedge machine_clk);
        machine_audio_valid = 1'b0;
    endtask

    task automatic read_i2s_frame(
        output logic [23:0] left,
        output logic [23:0] right
    );
        integer bit_index;
        begin
            left = 24'd0;
            right = 24'd0;
            @(negedge lrck);
            for (bit_index = 0; bit_index < 24; bit_index = bit_index + 1) begin
                @(negedge bclk);
                #1;
                left = {left[22:0], sdata};
            end
            @(posedge lrck);
            for (bit_index = 0; bit_index < 24; bit_index = bit_index + 1) begin
                @(negedge bclk);
                #1;
                right = {right[22:0], sdata};
            end
        end
    endtask

    task automatic expect_i2s_frame(
        input logic [23:0] expected_left,
        input logic [23:0] expected_right,
        input string message
    );
        logic [23:0] actual_left;
        logic [23:0] actual_right;
        integer attempt;
        logic matched;
        begin
            matched = 1'b0;
            for (attempt = 0; attempt < 8; attempt = attempt + 1) begin
                read_i2s_frame(actual_left, actual_right);
                if (actual_left == expected_left &&
                    actual_right == expected_right) begin
                    matched = 1'b1;
                    attempt = 8;
                end
            end
            if (!matched) begin
                $display("FAIL: %s expected=%06x/%06x actual=%06x/%06x",
                         message, expected_left, expected_right,
                         actual_left, actual_right);
                $fatal(1);
            end
        end
    endtask

    task automatic expect_i2s_tone;
        logic [23:0] actual_left;
        logic [23:0] actual_right;
        integer attempt;
        logic matched;
        begin
            matched = 1'b0;
            for (attempt = 0; attempt < 12; attempt = attempt + 1) begin
                read_i2s_frame(actual_left, actual_right);
                if (actual_left == actual_right &&
                    (actual_left == 24'h200000 ||
                     actual_left == 24'hE00000)) begin
                    matched = 1'b1;
                    attempt = 12;
                end
            end
            check_value(matched, "diagnostic tone reaches both I2S channels");
        end
    endtask

    // Both continuously enabled I2S serializers must never lose word phase.
    always @(negedge bclk) begin
        #1;
        if (dut.audio_bclk_resetn)
            check_value(dut.tone_i2s_lrck == dut.pcm_i2s_lrck,
                        "tone and PCM I2S frame phases match");
    end

    // Source selection is allowed to change only on the first bit-clock edge
    // of a complete stereo frame.
    always @(dut.source_test_bclk) begin
        #1;
        if (dut.audio_bclk_resetn)
            check_value(dut.audio_frame_bit == 6'd1 && !lrck,
                        "I2S source selection changes at frame boundary");
    end

    always @(pair_tx.channel_right) begin
        #1;
        if (control_resetn && pair_enable) begin
            if (pair_tx.channel_right) begin
                pair_last_right = pair_tx.subframe;
                pair_right_events = pair_right_events + 1;
            end else begin
                pair_last_left = pair_tx.subframe;
                pair_left_events = pair_left_events + 1;
            end
        end
    end

    initial begin
        logic [23:0] frame_left;
        logic [23:0] frame_right;
        integer start_frames;
        integer start_right_events;
        integer elapsed_frames;

        fork
            begin
                #5000000;
                $display("FAIL: audio test timeout");
                $fatal(1);
            end
        join_none

        repeat (4) @(negedge audio_mclk);
        machine_resetn = 1'b1;
        control_resetn = 1'b1;
        repeat (140) @(negedge bclk);
        #1;
        check_value(lrck_edges >= 4, "48 kHz I2S frame clock advances");
        check_value(!$isunknown({lrck, sdata, bclk, spdif}),
                    "audio outputs are known");
        expect_i2s_frame(24'd0, 24'd0,
                         "muted unavailable machine emits silence");

        machine_ready = 1'b1;
        submit_machine_sample(24'h123456, 24'hEDCBA9);
        mute = 1'b0;
        wait (dut.live_enable_bclk && dut.live_enable_control);
        expect_i2s_frame(24'h123456, 24'hEDCBA9,
                         "coherent live signed stereo PCM");
        check_value(dut.spdif_sample_l == 24'h123456 &&
                    dut.spdif_sample_r == 24'hEDCBA9,
                    "coherent live stereo reaches S/PDIF domain");

        submit_machine_sample(24'h800000, 24'h7FFFFF);
        expect_i2s_frame(24'h800000, 24'h7FFFFF,
                         "full-scale signed endpoints do not wrap");

        mute = 1'b1;
        start_frames = lrck_edges;
        expect_i2s_frame(24'd0, 24'd0, "mute sends zero PCM");
        check_value(lrck_edges > start_frames,
                    "mute leaves BCLK and LRCK running");

        test_tone = 1'b1;
        mute = 1'b0;
        expect_i2s_tone();
        check_value(!dut.spdif_mute, "diagnostic S/PDIF carrier is unmuted");

        test_tone = 1'b0;
        expect_i2s_frame(24'h800000, 24'h7FFFFF,
                         "live PCM resumes on a whole frame");

        // A ready drop must suppress the held nonzero sample. Waiting while
        // unavailable also lets the coherent zero/invalid payload propagate.
        machine_ready = 1'b0;
        expect_i2s_frame(24'd0, 24'd0, "core reset/load forces silence");
        repeat (160) @(negedge bclk);
        machine_ready = 1'b1;
        repeat (3) begin
            read_i2s_frame(frame_left, frame_right);
            check_value(frame_left == 24'd0 && frame_right == 24'd0,
                        "stale sample does not replay after ready rises");
        end
        submit_machine_sample(24'h010203, 24'hF0E0D0);
        expect_i2s_frame(24'h010203, 24'hF0E0D0,
                         "fresh post-reset sample rearms live audio");

        // Verify local S/PDIF stereo capture. Change R immediately after the
        // left subframe was loaded; the following right subframe must retain
        // the original pair's R value and valid even parity.
        pair_l = 24'h13579B;
        pair_r = 24'h2468AC;
        pair_enable = 1'b1;
        wait (pair_left_events >= 1);
        start_right_events = pair_right_events;
        pair_r = 24'hBADBAD;
        wait (pair_right_events > start_right_events);
        check_value(pair_last_left[27:4] == 24'h13579B,
                    "S/PDIF left sample formatting");
        check_value(pair_last_right[27:4] == 24'h2468AC,
                    "S/PDIF right sample is latched with left");
        check_value(^pair_last_left[31:4] == 1'b0 &&
                    ^pair_last_right[31:4] == 1'b0,
                    "S/PDIF subframe parity is even");

        // Mute also changes only at a stereo-pair boundary. Assert it after
        // loading a left subframe: its already captured right must complete,
        // and the next pair must be entirely zero.
        pair_l = 24'h112233;
        pair_r = 24'h445566;
        start_frames = pair_left_events;
        wait (pair_left_events > start_frames);
        pair_mute = 1'b1;
        start_right_events = pair_right_events;
        wait (pair_right_events > start_right_events);
        check_value(pair_last_right[27:4] == 24'h445566,
                    "S/PDIF mute completes the captured stereo pair");
        start_frames = pair_left_events;
        wait (pair_left_events > start_frames);
        check_value(pair_last_left[27:4] == 24'd0,
                    "S/PDIF mute zeroes the next left subframe");
        start_right_events = pair_right_events;
        wait (pair_right_events > start_right_events);
        check_value(pair_last_right[27:4] == 24'd0,
                    "S/PDIF mute zeroes the next right subframe");
        pair_mute = 1'b0;

        // One millisecond at 133.333 MHz contains 48 nominal stereo frames.
        start_frames = pair_left_events;
        repeat (133333) @(posedge control_clk);
        elapsed_frames = pair_left_events - start_frames;
        check_value(elapsed_frames >= 47 && elapsed_frames <= 49,
                    "S/PDIF cadence is 48 kHz at 133.333 MHz");

        pair_mute = 1'b1;
        start_frames = pair_left_events;
        wait (pair_left_events > start_frames);
        start_right_events = pair_right_events;
        wait (pair_right_events > start_right_events);
        check_value(pair_last_right[27:4] == 24'd0,
                    "S/PDIF mute zeroes sample data");
        pair_enable = 1'b0;
        repeat (4) @(posedge control_clk);
        check_value(pair_spdif == 1'b0, "disabled S/PDIF output is quiet");

        $display("PASS: live PCM, diagnostic tone, mute, reset, and S/PDIF");
        $finish;
    end
endmodule
