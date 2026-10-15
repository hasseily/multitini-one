// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_nes_video_normalizer;
    reg clk=0,resetn=0,ready=0,ce=0;
    always #5 clk=~clk;
    reg [7:0] x=0,y=0,r=0,g=0,b=0;
    wire [11:0] ox,oy;
    wire [15:0] rgb;
    wire start;
    wire [31:0] malformed;
    integer pixels=0,frames=0,expected=0;
    multitini_nes_video_normalizer dut(.clk(clk),.resetn(resetn),.ready(ready),
        .pixel_ce(ce),.pixel_x(x),.pixel_y(y),.red(r),.green(g),.blue(b),
        .scan_x(ox),.scan_y(oy),.rgb565(rgb),.frame_start(start),.malformed_frames(malformed));
    function automatic [15:0] pattern(input integer xx,yy);
        pattern={5'(xx),6'(yy),5'(xx^yy)};
    endfunction
    always @(negedge clk)if(resetn && ready)begin
        if(start)begin
            if(ox!=4095 || oy!=4095)$fatal(1,"frame boundary overlaps first pixel");
            expected=0;frames=frames+1;
        end
        if(ox<256 && oy<240)begin
            if({oy[7:0],ox[7:0]}!=expected || rgb!==pattern(ox,oy))
                $fatal(1,"native pixel changed at %0d,%0d expected%0d rgb%h",ox,oy,expected,rgb);
            pixels=pixels+1;expected=expected+1;
        end
    end
    task pixel(input integer xx,yy);
        begin
            @(negedge clk);ce=1;x=8'(xx);y=8'(yy);
            r={5'(xx),3'd0};g={6'(yy),2'd0};b={5'(xx^yy),3'd0};
            @(negedge clk);ce=0;
            repeat((xx%3)+1)@(negedge clk);
        end
    endtask
    task full_frame;
        begin for(integer yy=0;yy<240;yy=yy+1)for(integer xx=0;xx<256;xx=xx+1)pixel(xx,yy);end
    endtask
    integer saved;
    initial begin
        repeat(4)@(negedge clk);resetn=1;ready=1;
        // Ignore startup halfway through a raster.
        pixel(40,10);pixel(41,10);
        if(pixels || frames)$fatal(1,"mid-raster startup emitted pixels");
        full_frame();full_frame();
        if(pixels!=122880 || malformed)$fatal(1,"lost complete native frame");
        pixel(0,0);pixel(1,0);saved=pixels;
        pixel(3,0);pixel(4,0);
        if(pixels!=saved)$fatal(1,"out-of-order raster was not suppressed");
        full_frame();
        if(malformed!=1)$fatal(1,"malformed raster not counted");
        pixel(0,0);pixel(1,0);
        pixel(0,0);
        if(malformed!=2)$fatal(1,"truncated raster not counted");
        ready=0;repeat(3)@(negedge clk);ready=1;
        pixel(200,100);saved=pixels;pixel(201,100);
        if(pixels!=saved)$fatal(1,"readiness reset retained partial frame");
        full_frame();
        if(expected!=61440)$fatal(1,"failed to recover full raster");
        $display("PASS NES native video: %0d exact RGB565 pixels, all 256 columns/240 rows, sparse enables, ordering, truncation, readiness recovery",pixels);
        $finish;
    end
    initial begin #20000000;$fatal(1,"NES native video timeout");end
endmodule
