// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Ultimate-compatible sample state, with a mode-independent physical period.
// Position and all address arithmetic wrap at 24 bits, including odd lengths.
module multitini_c64_ua_voice #(
    parameter integer PERIOD_OFFSET_HALF_TICKS=3
) (
    input wire clk, reset, paused, half_tick,
    input wire [7:0] control,
    input wire [23:0] start_address, sample_length, repeat_a, repeat_b,
    input wire [15:0] rate,
    input wire signed [15:0] next_sample,
    input wire sample_ready,
    output reg fetch,
    output reg [23:0] fetch_address, predict_address,
    output wire sixteen_bit,
    output reg predict_valid,
    output wire [17:0] deadline,
    output wire playing,
    output reg signed [15:0] sample,
    output reg irq_set, underrun
);
    localparam [1:0] IDLE=0, PLAY=1, FINISHED=2;
    reg [1:0] state;
    reg [23:0] position;
    reg [17:0] remaining;
    wire [17:0] period={1'b0,rate,1'b0}+PERIOD_OFFSET_HALF_TICKS;
    wire gate=control[0], repeat_enable=control[1];
    assign sixteen_bit=|control[5:4];
    wire [23:0] stride=sixteen_bit ? (control[6]?24'd4:24'd2) : (control[6]?24'd2:24'd1);
    wire boundary=state==PLAY && half_tick && remaining<=1;
    wire stop_now=state==PLAY && !gate && !repeat_enable;
    reg [23:0] selected_position, following_position;
    reg end_now;
    assign playing=state==PLAY;
    assign deadline=remaining;

    always @* begin
        fetch=0; fetch_address=0; predict_address=0; predict_valid=0;
        selected_position=position; following_position=position; end_now=0;
        if(position==repeat_b && gate && repeat_enable) selected_position=repeat_a;
        else if(position==sample_length) end_now=1;
        if(state==IDLE && gate) begin
            selected_position=0; end_now=0;
            fetch=1;
        end else if(boundary && !end_now && !stop_now) fetch=1;
        if(reset || paused) fetch=0;
        fetch_address=start_address+selected_position;
        following_position=fetch ? selected_position+stride : position;
        if(following_position==repeat_b && gate && repeat_enable) begin
            predict_address=start_address+repeat_a; predict_valid=1;
        end else if(following_position!=sample_length) begin
            predict_address=start_address+following_position; predict_valid=1;
        end
        predict_valid=predict_valid && (state==PLAY || fetch) && !stop_now && !reset;
    end

    always @(posedge clk) begin
        irq_set<=0; underrun<=0;
        if(reset) begin
            state<=IDLE;position<=0;remaining<=0;sample<=0;
        end else if(!paused) begin
            if(state==IDLE && gate) begin
                state<=PLAY; sample<=0; remaining<=period;
            end else if(state==FINISHED && !gate) state<=IDLE;
            else if(state==PLAY) begin
                if(half_tick) begin
                    if(remaining>1) remaining<=remaining-1'b1;
                    else begin
                        if(sample_ready) sample<=next_sample;
                        else underrun<=1;
                        remaining<=period;
                        if(end_now) begin state<=FINISHED; irq_set<=control[2]; end
                    end
                end
                if(stop_now) state<=IDLE;
            end
            if(fetch) position<=selected_position+stride;
        end
    end
endmodule
