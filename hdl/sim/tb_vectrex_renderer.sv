// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_vectrex_renderer;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,pause=0,line_valid=0;
    reg signed [15:0] line_x0=0,line_y0=0,line_x1=0,line_y1=0;
    reg [7:0] line_intensity=0,pixel_address=0;
    wire line_ready,initialized;
    wire [31:0] drawn_lines,overflow_count;
    wire [7:0] pixel_intensity;
    multitini_vectrex_renderer #(.WIDTH(16),.HEIGHT(16),.CLOCK_HZ(1000000),
        .DECAY_HZ(0),.QUEUE_DEPTH(4)) dut(.pixel_clk(clk),.*);
    reg [7:0] expected[0:255];
    task stroke(input integer x0,y0,x1,y1,input [7:0] z);
        @(negedge clk);
        while(!line_ready)@(negedge clk);
        line_x0=x0;line_y0=y0;line_x1=x1;line_y1=y1;line_intensity=z;line_valid=1;
        @(negedge clk);line_valid=0;
    endtask
    task finish_lines;
        wait(dut.queued==0 && dut.state==1);
        repeat(2)@(posedge clk);
    endtask
    task inspect;
        for(integer i=0;i<256;i=i+1)begin
            @(negedge clk);pixel_address=i;
            @(posedge clk);#1;
            if(pixel_intensity!==expected[i])
                $fatal(1,"Raster pixel (%0d,%0d) expected %0d got %0d",i%16,i/16,expected[i],pixel_intensity);
        end
    endtask
    initial begin
        for(integer i=0;i<256;i=i+1)expected[i]=0;
        repeat(3)@(posedge clk);@(negedge clk);reset=0;
        wait(initialized);inspect();
        stroke(1,2,5,2,80);for(integer i=1;i<=5;i=i+1)expected[2*16+i]=80;
        stroke(5,7,5,2,120);for(integer i=2;i<=7;i=i+1)expected[i*16+5]=120;
        stroke(9,9,13,13,180);for(integer i=9;i<=13;i=i+1)expected[i*16+i]=180;
        stroke(13,9,9,13,200);for(integer i=9;i<=13;i=i+1)expected[i*16+22-i]=200;
        stroke(7,4,7,4,255);expected[4*16+7]=255;
        stroke(1,2,5,2,40); // Retrace must preserve the brighter phosphor.
        stroke(-4,15,3,15,100);for(integer i=0;i<=3;i=i+1)expected[15*16+i]=100;
        stroke(-9,3,-2,8,255); // Same-side rejection must not alias address 0.
        stroke(14,1,14,5,0);  // Zero-intensity command is discarded.
        finish_lines();inspect();
        if(drawn_lines!==7 || overflow_count!==0)$fatal(1,"Line completion/rejection accounting");
        @(negedge clk);pause=1;
        repeat(4)stroke(0,0,0,0,1);
        if(line_ready)$fatal(1,"Queue did not apply back pressure while paused");
        @(negedge clk);line_valid=1;
        @(negedge clk);line_valid=0;
        if(overflow_count!==1)$fatal(1,"Overflow counter did not record a rejected command");
        @(negedge clk);pause=0;
        finish_lines();expected[0]=1;inspect();
        if(drawn_lines!==11)$fatal(1,"Paused queue lost accepted commands");
        $display("PASS: Vectrex raster endpoints, both diagonals, reverse vertical, dots, clipping, retrace and back pressure");
        $finish;
    end
    initial begin #200000;$fatal(1,"Renderer test timeout");end
endmodule
