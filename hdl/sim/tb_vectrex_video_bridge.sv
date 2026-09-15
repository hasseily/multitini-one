// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_vectrex_video_bridge;
    reg clk=0, resetn=0, ready=0, vblank=0;
    reg [11:0] x=0,y=0;
    wire [18:0] address;
    reg [7:0] intensity=0;
    wire active;
    wire [15:0] rgb;
    wire [11:0] viewport_x,viewport_y,viewport_width,viewport_height;
    integer row,col,frame,expected_address,checks=0;
    reg [7:0] expected_intensity;
    always #5 clk=~clk;
    // Independent synchronous memory with a distinctive address pattern.
    always @(posedge clk) intensity <= (address ^ (address>>8)) & 255;
    multitini_vectrex_video_bridge dut(
        .pixel_clk(clk),.pixel_resetn(resetn),.source_ready(ready),
        .scan_x(x),.scan_y(y),.vblank_start(vblank),.pixel_address(address),
        .pixel_intensity(intensity),.machine_active(active),.machine_rgb565(rgb),
        .viewport_x(viewport_x),.viewport_y(viewport_y),
        .viewport_width(viewport_width),.viewport_height(viewport_height)
    );
    initial begin
        repeat(3) @(negedge clk);
        resetn=1;ready=1;
        if(viewport_x!==555 || viewport_y!==0 || viewport_width!==810 || viewport_height!==1080)
            $fatal(1,"Fixed Vectrex viewport geometry mismatch");
        // Two complete scans prove vertical phase restart as well as every
        // duplicated row/column, both portrait edges and the blanking area.
        for(frame=0;frame<2;frame=frame+1) begin
            for(row=0;row<1082;row=row+1) begin
                for(col=0;col<1380;col=col+1) begin
                    x=col;y=row;vblank=(row==1080 && col==0);
                    #1;
                    if(col>=555 && col<1365 && row<1080) begin
                        expected_address=(row*2/3)*540+((col-555)*2/3);
                        expected_intensity=(expected_address^(expected_address>>8))&255;
                        if(rgb!=={expected_intensity[7:3],expected_intensity[7:2],expected_intensity[7:3]})
                            $fatal(1,"Pixel mismatch frame=%0d x=%0d y=%0d address=%0d got=%h",frame,col,row,expected_address,rgb);
                        checks=checks+1;
                    end else if(rgb!==0 && col!=0)
                        $fatal(1,"Nonblack portrait margin at %0d,%0d",col,row);
                    @(negedge clk);
                end
            end
            if(!active) $fatal(1,"Ready not latched at vertical blank");
        end
        ready=0;repeat(4) @(negedge clk);vblank=1;
        @(negedge clk);if(active) $fatal(1,"Ready loss not applied at blank");
        $display("PASS Vectrex DVI bridge: %0d pixels, scale phases, edges and frame restart",checks);
        $finish;
    end
endmodule
