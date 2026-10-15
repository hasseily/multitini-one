// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_video_normalizer;
    reg clk=0;always #5 clk=~clk;
    reg resetn=0,ready=0,pixel_ce=0;
    reg [9:0] x=0;
    reg [8:0] y=0;
    reg [7:0] r=0,g=0,b=0;
    wire [11:0] out_x,out_y;
    wire [15:0] rgb;
    wire start_frame;
    wire [31:0] malformed;
    integer emitted=0,frames=0;
    reg checking=0;
    reg [7:0] expected_r,expected_g,expected_b;
    reg [15:0] expected_rgb;
    multitini_apple_iigs_video_normalizer dut(
        .clk(clk),.resetn(resetn),.ready(ready),.pixel_ce(pixel_ce),.pixel_x(x),.pixel_y(y),
        .red(r),.green(g),.blue(b),.scan_x(out_x),.scan_y(out_y),.rgb565(rgb),
        .frame_start(start_frame),.malformed_frames(malformed));
    always @(negedge clk)begin
        if(checking && start_frame)begin
            if(frames!=0 && emitted!=256000)$fatal(1,"incomplete frame: %0d",emitted);
            emitted=0;frames=frames+1;
        end
        if(checking && ready && out_x<640 && out_y<400)begin
            if(out_x!==12'(emitted%640) || out_y!==12'(emitted/640))
                $fatal(1,"coordinate mismatch pixel %0d got %0d,%0d",emitted,out_x,out_y);
            expected_r=8'(44+out_x);
            expected_g=8'(256+(out_y/2));
            expected_b=expected_r^expected_g;
            expected_rgb={expected_r[7:3],expected_g[7:2],expected_b[7:3]};
            if(rgb!==expected_rgb)$fatal(1,"pixel mismatch at %0d,%0d got%h expected%h",out_x,out_y,rgb,expected_rgb);
            emitted=emitted+1;
        end
    end
    task sample(input integer sx,sy,input bit skip);
        begin
            @(negedge clk);#1;x=10'(sx);y=9'(sy);
            r=8'(sx);g=8'(sy);b=8'(sx^sy);pixel_ce=!skip;
            @(negedge clk);#1;pixel_ce=0;
            repeat(2)@(negedge clk);
        end
    endtask
    task rows(input integer first,last,input bit corrupt);
        begin
            for(integer sy=first;sy<=last;sy=sy+1)
                for(integer sx=0;sx<912;sx=sx+1)
                    sample(sx,sy,corrupt && sy==260 && sx==160);
        end
    endtask
    initial begin
        repeat(5)@(negedge clk);resetn=1;ready=1;checking=1;
        rows(250,456,0); // NTSC top border and full active image.
        repeat(1500)@(negedge clk);
        if(emitted!=256000 || malformed!=0)$fatal(1,"NTSC frame did not complete");
        rows(200,456,0); // PAL has fifty extra blank rows; crop stays exact.
        repeat(1500)@(negedge clk);
        if(emitted!=256000 || malformed!=0 || frames!=2)$fatal(1,"PAL frame did not complete");
        checking=0;
        rows(256,265,1);
        rows(256,256,0);
        if(malformed!=1)$fatal(1,"malformed field was not rejected: %0d",malformed);
        ready=0;repeat(5)@(negedge clk);
        if(out_x!=12'hfff || out_y!=12'hfff)$fatal(1,"lost readiness continued emitting");
        $display("PASS IIgs native video: 512000 RGB565 pixels, NTSC/PAL crop, exact row duplication, malformed and readiness abort");
        $finish;
    end
    initial begin #20000000;$fatal(1,"IIgs video timeout");end
endmodule
