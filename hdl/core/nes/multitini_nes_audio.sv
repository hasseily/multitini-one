// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Unsigned NES mixer to the shared signed24-bit PCM stream. Audio time follows
// the service clock, independently of cache stalls or machine tick catch-up.
module multitini_nes_audio #(
    parameter integer CORE_HZ = 42954544,
    parameter integer AUDIO_SAMPLE_HZ = 48000
) (
    input wire clk,
    input wire reset,
    input wire machine_reset,
    input wire mute,
    input wire [15:0] sample,
    output logic signed [23:0] audio_l,
    output wire signed [23:0] audio_r,
    output logic audio_valid
);
    logic [31:0] audio_phase;
    logic signed [31:0] audio_dc;
    wire sample_ce = audio_phase >= CORE_HZ-AUDIO_SAMPLE_HZ;
    wire signed [31:0] sample_q8 = $signed({1'b0,sample,8'd0});
    wire signed [31:0] difference = sample_q8-audio_dc;

    // Match the pinned MiSTer sys/iir_filter.v 48 kHz DC blocker: pole511/512,
    // feedforward gain1023/1024, approximately14.94Hz cutoff. Express it as
    // a running DC estimate; update only once per output audio sample.
    wire signed [31:0] blocked = difference-(difference>>>10);
    assign audio_r = audio_l;
    always @(posedge clk) begin
        audio_valid <= 0;
        if(reset) begin
            audio_phase <= 0;
            audio_dc <= 32'h00ffff00;
            audio_l <= 0;
        end else begin
            if(machine_reset) audio_dc <= 32'h00ffff00;
            if(sample_ce) begin
                audio_phase <= audio_phase-(CORE_HZ-AUDIO_SAMPLE_HZ);
                audio_valid <= 1;
                if(machine_reset||mute) audio_l <= 0;
                else begin
                    audio_dc <= audio_dc+(difference>>>9);
                    // A full unsigned16-bit mixer excursion needs17signed
                    // bits. Q7 fits it safely in the shared signed24-bit PCM.
                    audio_l <= blocked>>>1;
                end
            end else audio_phase <= audio_phase+AUDIO_SAMPLE_HZ;
        end
    end
endmodule
