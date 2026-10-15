// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Preserve the complete 640x200 IIgs image, including the centered legacy
// 560x192 image, and double each source row for its 640x400 display aspect.
// VGC RGB and coordinates arrive together at 14.318 MHz enables. Two line
// stores let the 57.273 MHz stream emit both rows before the next input line.
// PAL extends blanking only; the same V=256..455 crop covers both standards.
module multitini_apple_iigs_video_normalizer (
    input wire clk,resetn,ready,
    input wire pixel_ce,
    input wire [9:0] pixel_x,
    input wire [8:0] pixel_y,
    input wire [7:0] red,green,blue,
    output reg [11:0] scan_x,scan_y,
    output reg [15:0] rgb565,
    output reg frame_start,
    output reg [31:0] malformed_frames
);
    (* ram_style="block" *) reg [15:0] lines [0:1279];
    reg [1:0] full;
    reg write_bank,read_bank,field_active,bad;
    reg [9:0] input_count,output_x;
    reg [7:0] input_row,rows[0:1];
    reg [8:0] output_y;
    reg issued;
    reg [11:0] issued_x,issued_y;
    reg [15:0] read_pixel;
    wire start_field=pixel_ce && pixel_x==0 && pixel_y==256;
    wire input_active=pixel_ce && pixel_x>=44 && pixel_x<684 && pixel_y>=256 && pixel_y<456;
    wire issue=field_active && !bad && output_y<400 &&
        full[read_bank] && rows[read_bank]==output_y[8:1];
    always @(posedge clk)begin
        frame_start<=0;
        scan_x<=issued ? issued_x : 12'hfff;
        scan_y<=issued ? issued_y : 12'hfff;
        rgb565<=read_pixel;
        issued<=issue;
        issued_x<={2'd0,output_x};issued_y<={3'd0,output_y};
        if(issue)read_pixel<=lines[(read_bank ? 640 : 0)+output_x];
        if(!resetn || !ready)begin
            full<=0;write_bank<=0;read_bank<=0;field_active<=0;bad<=0;
            input_count<=0;input_row<=0;output_x<=0;output_y<=0;
            issued<=0;scan_x<=12'hfff;scan_y<=12'hfff;rgb565<=0;frame_start<=0;
            if(!resetn)malformed_frames<=0;
        end else if(start_field)begin
            if(field_active && (bad || output_y!=400))malformed_frames<=malformed_frames+1'b1;
            full<=0;write_bank<=0;read_bank<=0;field_active<=1;bad<=0;
            input_count<=0;input_row<=0;output_x<=0;output_y<=0;
            issued<=0;scan_x<=12'hfff;scan_y<=12'hfff;frame_start<=1;
        end else begin
            if(input_active && field_active)begin
                if(full[write_bank] || input_count>=640 || pixel_y[7:0]!=input_row ||
                   pixel_x!=44+input_count)bad<=1;
                else lines[(write_bank ? 640 : 0)+input_count]<={red[7:3],green[7:2],blue[7:3]};
                if(pixel_x==683)begin
                    if(input_count!=639)bad<=1;
                    else begin full[write_bank]<=1;rows[write_bank]<=input_row;write_bank<=!write_bank;end
                    input_count<=0;input_row<=input_row+1'b1;
                end else input_count<=input_count+1'b1;
            end
            if(issue)begin
                if(output_x==639)begin
                    output_x<=0;output_y<=output_y+1'b1;
                    if(output_y[0])begin full[read_bank]<=0;read_bank<=!read_bank;end
                end else output_x<=output_x+1'b1;
            end
        end
    end
endmodule
