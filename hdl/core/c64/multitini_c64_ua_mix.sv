// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Capture all seven contributions on every native clock. A saturated pipeline
// preserves voice order and aligns each contribution to the same input clock.
// The boxcar integrates every resulting mix; no eight-clock resampling jitter.
module multitini_c64_ua_mix (
    input wire clk, reset, audio_tick,
    input wire [111:0] samples,
    input wire [41:0] volumes,
    input wire [27:0] pans,
    output reg signed [17:0] audio_l, audio_r,
    output reg clipped
);
    reg signed [20:0] pipeline_l[0:6],pipeline_r[0:6];
    wire signed [20:0] mix_l=pipeline_l[6],mix_r=pipeline_r[6];
    wire signed [17:0] mixed_l=mix_l>>>3,mixed_r=mix_r>>>3;
    wire [6:0] stage_clip;
    reg signed [27:0] sum_l,sum_r;
    reg [9:0] count;
    reg signed [17:0] normalized_l,normalized_r;
    reg [26:0] dividend_l,dividend_r,quotient_l,quotient_r;
    reg [10:0] remainder_l,remainder_r;
    reg [9:0] divisor;
    reg sign_l,sign_r;
    reg [4:0] divide_left;
    wire signed [27:0] total_l=sum_l+{{10{mixed_l[17]}},mixed_l};
    wire signed [27:0] total_r=sum_r+{{10{mixed_r[17]}},mixed_r};
    wire [9:0] total_count=count+10'd1;
    wire [10:0] trial_l={remainder_l[9:0],dividend_l[26]};
    wire [10:0] trial_r={remainder_r[9:0],dividend_r[26]};
    wire bit_l=trial_l>={1'b0,divisor},bit_r=trial_r>={1'b0,divisor};
    wire [10:0] rest_l=bit_l ? trial_l-divisor : trial_l;
    wire [10:0] rest_r=bit_r ? trial_r-divisor : trial_r;
    wire [26:0] result_l={quotient_l[25:0],bit_l},result_r={quotient_r[25:0],bit_r};
    function automatic signed [20:0] sat21(input signed [21:0] value);
        begin
            if(value>22'sd1048575)sat21=21'sd1048575;
            else if(value< -22'sd1048576)sat21=-21'sd1048576;
            else sat21=value[20:0];
        end
    endfunction
    genvar g;
    generate for(g=0;g<7;g=g+1)begin: voice_mix
        wire signed [15:0] source_sample=samples[g*16+:16];
        wire [5:0] volume=volumes[g*6+:6];wire [3:0] pan=pans[g*4+:4];
        wire signed [22:0] product=source_sample*$signed({1'b0,volume});
        wire signed [16:0] scaled=product>>>5;
        wire [2:0] left_pan=pan[3] ? 4'd15-pan : 3'd7;
        wire [2:0] right_pan=pan[3] ? 3'd7 : pan[2:0];
        reg signed [20:0] delayed_l[0:g],delayed_r[0:g];
        wire signed [20:0] previous_l,previous_r;
        if(g==0)begin assign previous_l=0;assign previous_r=0;end
        else begin assign previous_l=pipeline_l[g-1];assign previous_r=pipeline_r[g-1];end
        wire signed [21:0] next_l={previous_l[20],previous_l}+{delayed_l[g][20],delayed_l[g]};
        wire signed [21:0] next_r={previous_r[20],previous_r}+{delayed_r[g][20],delayed_r[g]};
        assign stage_clip[g]=next_l>22'sd1048575 || next_l< -22'sd1048576 ||
                             next_r>22'sd1048575 || next_r< -22'sd1048576;
        integer d;
        always @(posedge clk)begin
            if(reset)begin
                pipeline_l[g]<=0;pipeline_r[g]<=0;
                for(d=0;d<=g;d=d+1)begin delayed_l[d]<=0;delayed_r[d]<=0;end
            end else begin
                delayed_l[0]<=scaled*$signed({1'b0,left_pan});
                delayed_r[0]<=scaled*$signed({1'b0,right_pan});
                for(d=1;d<=g;d=d+1)begin delayed_l[d]<=delayed_l[d-1];delayed_r[d]<=delayed_r[d-1];end
                pipeline_l[g]<=sat21(next_l);pipeline_r[g]<=sat21(next_r);
            end
        end
    end endgenerate
    always @(posedge clk)begin
        clipped<=0;
        if(reset)begin
            sum_l<=0;sum_r<=0;count<=0;audio_l<=0;audio_r<=0;normalized_l<=0;normalized_r<=0;
            dividend_l<=0;dividend_r<=0;quotient_l<=0;quotient_r<=0;
            remainder_l<=0;remainder_r<=0;divisor<=0;sign_l<=0;sign_r<=0;divide_left<=0;
        end else begin
            clipped<=|stage_clip;
            sum_l<=total_l;sum_r<=total_r;count<=total_count;
            if(divide_left!=0)begin
                dividend_l<={dividend_l[25:0],1'b0};dividend_r<={dividend_r[25:0],1'b0};
                remainder_l<=rest_l;remainder_r<=rest_r;
                quotient_l<=result_l;quotient_r<=result_r;divide_left<=divide_left-1'b1;
                if(divide_left==1)begin
                    // Preserve arithmetic-shift rounding of negative fractions.
                    normalized_l<=sign_l ? -$signed(result_l)-(rest_l!=0) : $signed(result_l);
                    normalized_r<=sign_r ? -$signed(result_r)-(rest_r!=0) : $signed(result_r);
                end
            end
            if(audio_tick)begin
                audio_l<=normalized_l;audio_r<=normalized_r;sum_l<=0;sum_r<=0;count<=0;
                divisor<=total_count;sign_l<=total_l[27];sign_r<=total_r[27];
                dividend_l<=total_l[27] ? -total_l : total_l;
                dividend_r<=total_r[27] ? -total_r : total_r;
                quotient_l<=0;quotient_r<=0;remainder_l<=0;remainder_r<=0;divide_left<=27;
            end
        end
    end
endmodule
