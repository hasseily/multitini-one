// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Calibrated Appletini ONE Blur/Glow port; source is recorded in provenance.
// Caller supplies post-persistence RGB888 in BBGGRR order. Four raw line RAMs
// supply H3/H5 blur and a fixed H3/V3 glow. No spatial result enters history.
// Dot bleed runs in the cached scanout scaler. Clocks and nine-clock latency
// are unchanged: (WIDTH+2)*(HEIGHT+2) samples plus nine pipeline clocks.
module multitini_video_frame_filter #(
    parameter bit DYNAMIC_DISPLAY=1'b0,
    parameter integer WIDTH=540, HEIGHT=720,
    parameter integer SCALE_NUM=3, SCALE_DEN=2
)(
    input wire [3:0] display_scale_num,display_scale_den,
    input wire clk,resetn,
    input wire start,
    input wire [7:0] options,
    input wire in_valid,
    output wire in_ready,
    input wire [23:0] in_rgb,
    output reg out_valid,
    input wire out_ready,
    output reg [15:0] out_rgb565,
    output reg busy,done
);
    // PHO2: normalized Blur blended at 1/8, 1/4 and 1/2; fixed Glow unchanged.
    // Legacy display/scale ports remain for the shared engine interface.
    // synthesis translate_off
    initial begin
        if(SCALE_NUM<1 || SCALE_NUM>64 || SCALE_DEN<1 || SCALE_DEN>64)
            $fatal(1,"Blur scale parameters must be in the range 1..64");
    end
    // synthesis translate_on
    reg [1:0] blur,bloom;
    reg [9:0] front_x,front_y;
    reg front_finished;
    reg [23:0] last_input;
    wire advance=!out_valid || out_ready;
    wire needs_input=front_x<WIDTH && front_y<HEIGHT;
    wire issue=busy && !front_finished && advance && (!needs_input || in_valid);
    assign in_ready=resetn && busy && !front_finished && advance && needs_input;

    // Read old rows at x, then overwrite the oldest bank at x one advancing
    // clock later. The next RAM read is x+1 (or none at the right edge), so
    // correctness does not depend on cross-port same-address collision mode.
    (* ram_style="block" *) reg [23:0] line0[0:WIDTH-1];
    (* ram_style="block" *) reg [23:0] line1[0:WIDTH-1];
    (* ram_style="block" *) reg [23:0] line2[0:WIDTH-1];
    (* ram_style="block" *) reg [23:0] line3[0:WIDTH-1];
    reg [23:0] bank_q[0:3];
    reg s0_valid;
    reg [9:0] s0_x,s0_y;
    reg [23:0] s0_rgb;
    reg [1:0] s0_bank[0:4];
    reg [4:0] s0_current;
    always @(posedge clk) begin
        if(resetn && issue && front_x<WIDTH)begin
            bank_q[0]<=line0[front_x];bank_q[1]<=line1[front_x];
            bank_q[2]<=line2[front_x];bank_q[3]<=line3[front_x];
        end
        if(resetn && advance && s0_valid && s0_x<WIDTH && s0_y<HEIGHT)begin
            case(s0_y[1:0])
                0:line0[s0_x]<=s0_rgb;
                1:line1[s0_x]<=s0_rgb;
                2:line2[s0_x]<=s0_rgb;
                3:line3[s0_x]<=s0_rgb;
            endcase
        end
    end

    reg s1_valid;
    reg [9:0] s1_x,s1_y;
    reg [23:0] column[0:4];
    reg [23:0] window[0:4][0:4];
    reg s2_valid,s2_last;
    reg [8:0] h_near[0:4][0:2];
    reg [7:0] h_center[0:4][0:2],h_blur[0:4][0:2];
    reg s3_valid,s3_last;
    reg [7:0] h3[0:4][0:2],raw3[0:2],center3[0:4][0:2];
    reg s4_valid,s4_last;
    reg [7:0] bloom_h3[0:2][0:2],center4[0:4][0:2],raw4[0:2];
    reg s5_valid,s5_last;
    reg [8:0] v3_near[0:2];
    reg [8:0] blur_v_outer[0:2],blur_v_near[0:2];
    reg [7:0] v3_center[0:2],blur_v_center[0:2],raw5[0:2];
    reg s6_valid,s6_last;
    reg [7:0] vertical3[0:2],blur_vertical[0:2],blur_center6[0:2];
    reg [7:0] raw6[0:2];
    reg s7_valid,s7_last;
    reg [7:0] bloom3[0:2],normalized_vertical[0:2],raw7[0:2];
    reg out_last;

    function automatic [7:0] three_tap(input [8:0] near_sum,input [7:0] center);
        reg [9:0] sum;
        begin sum={1'b0,near_sum}+{1'b0,center,1'b0};three_tap=sum[9:2];end
    endfunction
    function automatic [7:0] five_tap(input [8:0] outer_sum,near_sum,input [7:0] center);
        reg [10:0] sum;
        begin
            sum={2'b0,outer_sum}+{1'b0,near_sum,1'b0}+{2'b0,center,1'b0};
            five_tap=sum[10:3];
        end
    endfunction
    function automatic [7:0] rounded_three_tap(input [8:0] near_sum,input [7:0] center);
        reg [9:0] sum;
        begin sum={1'b0,near_sum}+{1'b0,center,1'b0}+10'd2;rounded_three_tap=sum[9:2];end
    endfunction
    function automatic [7:0] blur_horizontal_five(input [7:0] a,b,c,d,e);
        reg [10:0] sum;
        begin
            sum={3'b0,a}+{2'b0,b,1'b0}+{2'b0,c,1'b0}+{2'b0,d,1'b0}+{3'b0,e}+11'd4;
            blur_horizontal_five=sum[10:3];
        end
    endfunction
    function automatic [7:0] blur_vertical_tent(input [7:0] up,mid,dn);
        begin blur_vertical_tent=rounded_three_tap({1'b0,up}+{1'b0,dn},mid);end
    endfunction
    function automatic [7:0] weaken_blur(input [7:0] raw,filtered,input [1:0] mode);
        reg [10:0] sum;
        begin
            case(mode)
                0:weaken_blur=raw;
                1:begin sum={raw,3'b0}-{3'b0,raw}+{3'b0,filtered}+11'd4;weaken_blur=sum[10:3];end
                2:begin sum={1'b0,raw,2'b0}-{3'b0,raw}+{3'b0,filtered}+11'd2;weaken_blur=sum[9:2];end
                3:begin sum={3'b0,raw}+{3'b0,filtered}+11'd1;weaken_blur=sum[8:1];end
            endcase
        end
    endfunction
    function automatic [7:0] result_channel(input [7:0] base,v3,input [1:0] bloom_mode);
        reg [7:0] halo;
        reg [8:0] sum;
        begin
            case(bloom_mode)
                0:halo=0;
                1:halo=v3>>3;
                2:halo=v3>>2;
                default:halo=v3>>1;
            endcase
            sum={1'b0,base}+{1'b0,halo};
            result_channel=sum[8] ? 8'hff : sum[7:0];
        end
    endfunction
    wire [7:0] result_r=result_channel(normalized_vertical[0],bloom3[0],bloom);
    wire [7:0] result_g=result_channel(normalized_vertical[1],bloom3[1],bloom);
    wire [7:0] result_b=result_channel(normalized_vertical[2],bloom3[2],bloom);
    integer row,col,channel;
    reg [7:0] b,c,d;
    always @(posedge clk)begin
        if(!resetn)begin
            busy<=0;done<=0;out_valid<=0;out_last<=0;out_rgb565<=0;
            front_x<=0;front_y<=0;front_finished<=0;blur<=0;bloom<=0;last_input<=0;
            s0_valid<=0;s1_valid<=0;s2_valid<=0;s3_valid<=0;s4_valid<=0;s5_valid<=0;s6_valid<=0;s7_valid<=0;
            s2_last<=0;s3_last<=0;s4_last<=0;s5_last<=0;s6_last<=0;s7_last<=0;
        end else begin
            done<=0;
            if(out_valid && out_ready && out_last)begin busy<=0;done<=1;end
            if(start && !busy)begin
                busy<=1;blur<=options[3:2];bloom<=options[5:4];
                front_x<=0;front_y<=0;front_finished<=0;
                s0_valid<=0;s1_valid<=0;s2_valid<=0;s3_valid<=0;s4_valid<=0;s5_valid<=0;s6_valid<=0;s7_valid<=0;
                out_valid<=0;out_last<=0;
            end else if(advance)begin
                s0_valid<=issue;
                if(issue)begin
                    s0_x<=front_x;s0_y<=front_y;
                    s0_rgb<=needs_input ? in_rgb : last_input;
                    if(needs_input)last_input<=in_rgb;
                    for(row=0;row<5;row=row+1)begin
                        s0_current[row]<=front_y==0 || (row==4 && front_y<HEIGHT);
                        if(front_y<4-row)s0_bank[row]<=0;
                        else if(front_y>=HEIGHT+4-row)s0_bank[row]<=(HEIGHT-1)%4;
                        else s0_bank[row]<=front_y[1:0]+row;
                    end
                    if(front_x==WIDTH+1)begin
                        front_x<=0;
                        if(front_y==HEIGHT+1)front_finished<=1;
                        else front_y<=front_y+1'b1;
                    end else front_x<=front_x+1'b1;
                end

                s1_valid<=s0_valid;
                if(s0_valid)begin
                    s1_x<=s0_x;s1_y<=s0_y;
                    // Right-edge tokens retain the complete previous column.
                    if(s0_x<WIDTH)
                        for(row=0;row<5;row=row+1)
                            column[row]<=s0_current[row] ? s0_rgb : bank_q[s0_bank[row]];
                end

                s2_valid<=s1_valid && s1_x>=2 && s1_y>=2;
                s2_last<=s1_x==WIDTH+1 && s1_y==HEIGHT+1;
                if(s1_valid)begin
                    for(row=0;row<5;row=row+1)begin
                        for(col=0;col<4;col=col+1)
                            window[row][col]<=s1_x==0 ? column[row] : window[row][col+1];
                        window[row][4]<=column[row];
                        for(channel=0;channel<3;channel=channel+1)begin
                            b=s1_x==0 ? column[row][channel*8+:8] : window[row][2][channel*8+:8];
                            c=s1_x==0 ? column[row][channel*8+:8] : window[row][3][channel*8+:8];
                            d=s1_x==0 ? column[row][channel*8+:8] : window[row][4][channel*8+:8];
                            h_near[row][channel]<={1'b0,b}+{1'b0,d};
                            h_center[row][channel]<=c;
                            if(blur==0 || WIDTH<4)h_blur[row][channel]<=c;
                            else if(blur==3)h_blur[row][channel]<=
                                blur_horizontal_five(window[row][1][channel*8+:8],b,c,d,
                                    column[row][channel*8+:8]);
                            else h_blur[row][channel]<=blur_vertical_tent(b,c,d);
                        end
                    end
                end

                s3_valid<=s2_valid;s3_last<=s2_last;
                if(s2_valid)begin
                    for(channel=0;channel<3;channel=channel+1)begin
                        raw3[channel]<=h_center[2][channel];
                        for(row=0;row<5;row=row+1)begin
                            h3[row][channel]<=three_tap(h_near[row][channel],h_center[row][channel]);
                            center3[row][channel]<=h_blur[row][channel];
                        end
                    end
                end

                s4_valid<=s3_valid;s4_last<=s3_last;
                if(s3_valid)for(channel=0;channel<3;channel=channel+1)begin
                    for(row=0;row<5;row=row+1)begin
                        center4[row][channel]<=center3[row][channel];
                    end
                    for(row=0;row<3;row=row+1)bloom_h3[row][channel]<=h3[row+1][channel];
                    raw4[channel]<=raw3[channel];
                end

                s5_valid<=s4_valid;s5_last<=s4_last;
                if(s4_valid)for(channel=0;channel<3;channel=channel+1)begin
                    v3_near[channel]<={1'b0,bloom_h3[0][channel]}+{1'b0,bloom_h3[2][channel]};
                    v3_center[channel]<=bloom_h3[1][channel];
                    blur_v_outer[channel]<={1'b0,center4[0][channel]}+{1'b0,center4[4][channel]};
                    blur_v_near[channel]<={1'b0,center4[1][channel]}+{1'b0,center4[3][channel]};
                    blur_v_center[channel]<=center4[2][channel];
                    raw5[channel]<=raw4[channel];
                end
                s6_valid<=s5_valid;s6_last<=s5_last;
                if(s5_valid)for(channel=0;channel<3;channel=channel+1)begin
                    vertical3[channel]<=three_tap(v3_near[channel],v3_center[channel]);
                    blur_vertical[channel]<=rounded_three_tap(blur_v_near[channel],blur_v_center[channel]);
                    blur_center6[channel]<=blur_v_center[channel];
                    raw6[channel]<=raw5[channel];
                end
                s7_valid<=s6_valid;s7_last<=s6_last;
                if(s6_valid)for(channel=0;channel<3;channel=channel+1)begin
                    bloom3[channel]<=vertical3[channel];
                    normalized_vertical[channel]<=weaken_blur(raw6[channel],
                        blur<2 ? blur_center6[channel] : blur_vertical[channel],blur);
                    raw7[channel]<=raw6[channel];
                end
                out_valid<=s7_valid;out_last<=s7_last;
                if(s7_valid)out_rgb565<={result_r[7:3],result_g[7:2],result_b[7:3]};
            end
        end
    end
endmodule
