// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Native-frame spatial filter. The caller supplies already persisted RGB888
// pixels in 0xBBGGRR order; persistence is deliberately outside this feedback-
// free filter. Four raw line RAMs serve both independent three/five-tap paths.
// A flowing job takes (WIDTH+2)*(HEIGHT+2) samples plus seven pipeline clocks.
module multitini_video_frame_filter #(
    parameter integer WIDTH=540, HEIGHT=720
)(
    input wire clk,resetn,
    input wire start,
    input wire [5:0] options,
    input wire in_valid,
    output wire in_ready,
    input wire [23:0] in_rgb,
    output reg out_valid,
    input wire out_ready,
    output reg [15:0] out_rgb565,
    output reg busy,done
);
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
    reg [8:0] h_outer[0:4][0:2],h_near[0:4][0:2];
    reg [7:0] h_center[0:4][0:2];
    reg s3_valid,s3_last;
    reg [7:0] h3[0:2][0:2],h5[0:4][0:2],raw3[0:2];
    reg s4_valid,s4_last;
    reg [8:0] v3_near[0:2],v5_outer[0:2],v5_near[0:2];
    reg [7:0] v3_center[0:2],v5_center[0:2],raw4[0:2],horizontal4[0:2];
    reg s5_valid,s5_last;
    reg [7:0] vertical3[0:2],vertical5[0:2],raw5[0:2],horizontal5[0:2];
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
    function automatic [7:0] result_channel(input [7:0] raw,horizontal,v3,v5,
                                           input [1:0] blur_mode,bloom_mode);
        reg [7:0] base,halo;
        reg [8:0] sum;
        begin
            case(blur_mode)
                0:base=raw;
                1:base=horizontal;
                2:base=v3;
                default:base=v5;
            endcase
            case(bloom_mode)
                0:halo=0;
                1:halo=v3>>3;
                2:halo=v3>>2;
                default:halo=v5>>1;
            endcase
            sum={1'b0,base}+{1'b0,halo};
            result_channel=sum[8] ? 8'hff : sum[7:0];
        end
    endfunction
    wire [7:0] result_r=result_channel(raw5[0],horizontal5[0],vertical3[0],vertical5[0],blur,bloom);
    wire [7:0] result_g=result_channel(raw5[1],horizontal5[1],vertical3[1],vertical5[1],blur,bloom);
    wire [7:0] result_b=result_channel(raw5[2],horizontal5[2],vertical3[2],vertical5[2],blur,bloom);
    integer row,col,channel;
    reg [7:0] a,b,c,d,e;
    always @(posedge clk)begin
        if(!resetn)begin
            busy<=0;done<=0;out_valid<=0;out_last<=0;out_rgb565<=0;
            front_x<=0;front_y<=0;front_finished<=0;blur<=0;bloom<=0;last_input<=0;
            s0_valid<=0;s1_valid<=0;s2_valid<=0;s3_valid<=0;s4_valid<=0;s5_valid<=0;
            s2_last<=0;s3_last<=0;s4_last<=0;s5_last<=0;
        end else begin
            done<=0;
            if(out_valid && out_ready && out_last)begin busy<=0;done<=1;end
            if(start && !busy)begin
                busy<=1;blur<=options[3:2];bloom<=options[5:4];
                front_x<=0;front_y<=0;front_finished<=0;
                s0_valid<=0;s1_valid<=0;s2_valid<=0;s3_valid<=0;s4_valid<=0;s5_valid<=0;
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
                            a=s1_x==0 ? column[row][channel*8+:8] : window[row][1][channel*8+:8];
                            b=s1_x==0 ? column[row][channel*8+:8] : window[row][2][channel*8+:8];
                            c=s1_x==0 ? column[row][channel*8+:8] : window[row][3][channel*8+:8];
                            d=s1_x==0 ? column[row][channel*8+:8] : window[row][4][channel*8+:8];
                            e=column[row][channel*8+:8];
                            h_outer[row][channel]<={1'b0,a}+{1'b0,e};
                            h_near[row][channel]<={1'b0,b}+{1'b0,d};
                            h_center[row][channel]<=c;
                        end
                    end
                end

                s3_valid<=s2_valid;s3_last<=s2_last;
                if(s2_valid)begin
                    for(channel=0;channel<3;channel=channel+1)begin
                        raw3[channel]<=h_center[2][channel];
                        for(row=0;row<5;row=row+1)
                            h5[row][channel]<=five_tap(h_outer[row][channel],h_near[row][channel],h_center[row][channel]);
                        for(row=0;row<3;row=row+1)
                            h3[row][channel]<=three_tap(h_near[row+1][channel],h_center[row+1][channel]);
                    end
                end

                s4_valid<=s3_valid;s4_last<=s3_last;
                if(s3_valid)for(channel=0;channel<3;channel=channel+1)begin
                    v3_near[channel]<={1'b0,h3[0][channel]}+{1'b0,h3[2][channel]};
                    v3_center[channel]<=h3[1][channel];
                    v5_outer[channel]<={1'b0,h5[0][channel]}+{1'b0,h5[4][channel]};
                    v5_near[channel]<={1'b0,h5[1][channel]}+{1'b0,h5[3][channel]};
                    v5_center[channel]<=h5[2][channel];
                    raw4[channel]<=raw3[channel];horizontal4[channel]<=h3[1][channel];
                end

                s5_valid<=s4_valid;s5_last<=s4_last;
                if(s4_valid)for(channel=0;channel<3;channel=channel+1)begin
                    vertical3[channel]<=three_tap(v3_near[channel],v3_center[channel]);
                    vertical5[channel]<=five_tap(v5_outer[channel],v5_near[channel],v5_center[channel]);
                    raw5[channel]<=raw4[channel];horizontal5[channel]<=horizontal4[channel];
                end
                out_valid<=s5_valid;out_last<=s5_last;
                if(s5_valid)out_rgb565<={result_r[7:3],result_g[7:2],result_b[7:3]};
            end
        end
    end
endmodule
