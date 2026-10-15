// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Preserve all 256x240 native pixels. Announce the frame one clock before
// pixel (0,0), so the common capture service can arm without losing a pixel.
// Invalid or missing coordinates abort the raster until the next origin.
module multitini_nes_video_normalizer (
    input wire clk,resetn,ready,pixel_ce,
    input wire [7:0] pixel_x,pixel_y,
    input wire [7:0] red,green,blue,
    output reg [11:0] scan_x,scan_y,
    output reg [15:0] rgb565,
    output reg frame_start,
    output reg [31:0] malformed_frames
);
    reg active,bad,pending;
    reg [15:0] count;
    reg [7:0] pending_x,pending_y;
    reg [15:0] pending_rgb;
    wire origin=pixel_ce && pixel_x==0 && pixel_y==0;
    wire ordered=pixel_y<240 && {pixel_y,pixel_x}==count;
    always @(posedge clk)begin
        frame_start<=0;
        scan_x<=pending ? {4'd0,pending_x} : 12'hfff;
        scan_y<=pending ? {4'd0,pending_y} : 12'hfff;
        rgb565<=pending_rgb;
        pending<=0;
        if(!resetn || !ready)begin
            active<=0;bad<=0;pending<=0;count<=0;
            scan_x<=12'hfff;scan_y<=12'hfff;rgb565<=0;
            pending_x<=0;pending_y<=0;pending_rgb<=0;frame_start<=0;
            if(!resetn)malformed_frames<=0;
        end else if(origin)begin
            if(active && (bad || count!=16'd61440))
                malformed_frames<=malformed_frames+1'b1;
            active<=1;bad<=0;count<=1;frame_start<=1;
            scan_x<=12'hfff;scan_y<=12'hfff;
            pending<=1;pending_x<=0;pending_y<=0;
            pending_rgb<={red[7:3],green[7:2],blue[7:3]};
        end else if(pixel_ce && active && !bad)begin
            if(!ordered || count>=16'd61440)begin
                bad<=1;
            end else begin
                count<=count+1'b1;
                pending<=1;pending_x<=pixel_x;pending_y<=pixel_y;
                pending_rgb<={red[7:3],green[7:2],blue[7:3]};
            end
        end
    end
endmodule
