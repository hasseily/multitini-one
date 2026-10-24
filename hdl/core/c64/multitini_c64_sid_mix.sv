// SPDX-License-Identifier: GPL-3.0-only
// Wide signed mixer. Point mode is combinational; average mode publishes the
// previous complete sample interval after a bounded iterative division.
module multitini_c64_sid_mix (
    input wire clk,reset,ce_1m,sample_tick,map_enable,
    input wire [143:0] chip_audio,input wire [255:0] chip_params,
    input wire [31:0] output_config,
    input wire signed [17:0] ua_audio_l,ua_audio_r,
    output wire signed [23:0] audio_l,audio_r,output wire clip
);
    reg signed [33:0] dc[0:7];
    reg [2:0] dc_index=0;reg dc_running=0;
    wire signed [17:0] dc_input=chip_audio[dc_index*18+:18];
    wire signed [34:0] dc_target=output_config[17] ? $signed({dc_input,16'b0}) : 35'sd0;
    wire signed [34:0] dc_difference=dc_target-$signed(dc[dc_index]);
    integer n;
    always @(posedge clk) begin
        if(reset || !map_enable) begin
            dc_running<=0;dc_index<=0;
            for(n=0;n<8;n=n+1) dc[n]<=0;
        end else begin
            if(ce_1m) begin dc_running<=1;dc_index<=0;end
            else if(dc_running) begin
                dc[dc_index]<=dc[dc_index]+(dc_difference>>>15);
                if(dc_index==7) dc_running<=0; else dc_index<=dc_index+1'b1;
            end
        end
    end
    function automatic signed [17:0] clamp18(input signed [35:0] x);
        if(x>131071) clamp18=18'sh1ffff;
        else if(x< -131072) clamp18=18'sh20000;
        else clamp18=x[17:0];
    endfunction
    function automatic signed [23:0] clamp24(input signed [42:0] x);
        if(x>8388607) clamp24=24'sh7fffff;
        else if(x< -8388608) clamp24=24'sh800000;
        else clamp24=x[23:0];
    endfunction
    wire signed [17:0] clean[0:7];
    wire signed [26:0] prod_l[0:7],prod_r[0:7];
    for(genvar k=0;k<8;k=k+1) begin
        wire signed [34:0] estimate=($signed(dc[k])+35'sd32768)>>>16;
        assign clean[k]=clamp18($signed(chip_audio[k*18+:18])-estimate);
        assign prod_l[k]=clean[k]*$signed({1'b0,chip_params[k*32+:8]});
        assign prod_r[k]=clean[k]*$signed({1'b0,chip_params[k*32+8+:8]});
    end
    wire signed [26:0] ua_l=ua_audio_l*$signed({1'b0,output_config[7:0]});
    wire signed [26:0] ua_r=ua_audio_r*$signed({1'b0,output_config[15:8]});
    reg signed [31:0] sum_l,sum_r;
    always @* begin
        sum_l=ua_l;sum_r=ua_r;
        for(integer k=0;k<8;k=k+1) begin sum_l=sum_l+prod_l[k];sum_r=sum_r+prod_r[k];end
    end
    wire signed [31:0] scaled_l=sum_l>>>(1+output_config[19:18]);
    wire signed [31:0] scaled_r=sum_r>>>(1+output_config[19:18]);
    wire point_clip=scaled_l>8388607 || scaled_l< -8388608 || scaled_r>8388607 || scaled_r< -8388608;
    // 4095 native clocks at the largest legal mix fit signed 43 bits. Normal
    // intervals are below 700 clocks. Saturate the sample count on tick loss.
    reg signed [42:0] acc_l=0,acc_r=0;
    reg [11:0] count=0;
    reg [42:0] quotient_l=0,quotient_r=0;
    reg [12:0] remainder_l=0,remainder_r=0;
    reg [11:0] divisor=1;reg sign_l=0,sign_r=0;
    reg [5:0] steps=0;
    reg signed [23:0] average_l=0,average_r=0;reg average_clip=0;
    wire [12:0] next_rem_l={remainder_l[11:0],quotient_l[42]};
    wire [12:0] next_rem_r={remainder_r[11:0],quotient_r[42]};
    wire [42:0] next_q_l={quotient_l[41:0],next_rem_l>=divisor};
    wire [42:0] next_q_r={quotient_r[41:0],next_rem_r>=divisor};
    wire signed [42:0] result_l=sign_l ? -$signed(next_q_l) : $signed(next_q_l);
    wire signed [42:0] result_r=sign_r ? -$signed(next_q_r) : $signed(next_q_r);
    always @(posedge clk) begin
        if(reset || !map_enable || !output_config[16]) begin
            acc_l<=0;acc_r<=0;count<=0;steps<=0;average_l<=0;average_r<=0;average_clip<=0;
        end else begin
            if(count!=4095) begin acc_l<=acc_l+scaled_l;acc_r<=acc_r+scaled_r;count<=count+1'b1;end
            if(sample_tick) begin
                if(count!=0 && steps==0) begin
                    quotient_l<=acc_l[42] ? -acc_l : acc_l;quotient_r<=acc_r[42] ? -acc_r : acc_r;
                    sign_l<=acc_l[42];sign_r<=acc_r[42];divisor<=count;
                    remainder_l<=0;remainder_r<=0;steps<=43;
                end
                acc_l<=scaled_l;acc_r<=scaled_r;count<=1;
            end else if(steps!=0) begin
                quotient_l<=next_q_l;quotient_r<=next_q_r;
                remainder_l<=next_rem_l>=divisor ? next_rem_l-divisor : next_rem_l;
                remainder_r<=next_rem_r>=divisor ? next_rem_r-divisor : next_rem_r;
                steps<=steps-1'b1;
                if(steps==1) begin
                    average_l<=clamp24(result_l);average_r<=clamp24(result_r);
                    average_clip<=result_l>8388607 || result_l< -8388608 || result_r>8388607 || result_r< -8388608;
                end
            end
        end
    end
    assign audio_l=output_config[16] ? average_l : clamp24(scaled_l);
    assign audio_r=output_config[16] ? average_r : clamp24(scaled_r);
    assign clip=output_config[16] ? average_clip : point_clip;
endmodule
