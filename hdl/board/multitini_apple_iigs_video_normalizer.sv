// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Preserve the 640x200 IIgs image with 42 native pixels of border on each
// side and 24 native rows above/below it. Duplicate rows for 724x496 output.
// VGC RGB and coordinates arrive together at 14.318 MHz enables. Two line
// stores let the 57.273 MHz stream emit both rows before the next input line.
// NTSC's top border wraps 511 -> 250; PAL's nearest 24 rows are 232..255.
// Emit the 44 DDR stride-padding positions after every 724-pixel image row.
module multitini_apple_iigs_video_normalizer (
    input wire clk,resetn,ready,
    input wire pixel_ce,pal,
    input wire [9:0] pixel_x,
    input wire [8:0] pixel_y,
    input wire [7:0] red,green,blue,
    output reg [11:0] scan_x,scan_y,
    output reg [15:0] rgb565,
    output reg frame_start,
    output reg [31:0] malformed_frames
);
    localparam integer WIDTH=724,STRIDE=768,HEIGHT=496;
    (* ram_style="block" *) reg [15:0] lines [0:1447];
    reg [1:0] full;
    reg write_bank,read_bank,field_active,field_pal,bad;
    reg [9:0] input_count,output_x;
    reg [7:0] input_row,rows[0:1];
    reg [8:0] output_y;
    reg issued;
    reg [11:0] issued_x,issued_y;
    reg [15:0] read_pixel;
    wire start_field=pixel_ce && pixel_x==0 && pixel_y==(pal ? 9'd232 : 9'd494);
    wire input_active=pixel_ce && pixel_x>=2 && pixel_x<726 &&
        (field_pal ? (pixel_y>=232 && pixel_y<480) :
                     (pixel_y>=494 || (pixel_y>=250 && pixel_y<480)));
    wire [8:0] expected_y=field_pal ? 9'd232+{1'b0,input_row} :
        input_row<18 ? 9'd494+{1'b0,input_row} : 9'd232+{1'b0,input_row};
    wire issue=field_active && !bad && pal==field_pal && output_y<HEIGHT &&
        full[read_bank] && rows[read_bank]==output_y[8:1];
    always @(posedge clk)begin
        frame_start<=0;
        scan_x<=issued ? issued_x : 12'hfff;
        scan_y<=issued ? issued_y : 12'hfff;
        rgb565<=read_pixel;
        issued<=issue;
        issued_x<={2'd0,output_x};issued_y<={3'd0,output_y};
        if(issue)read_pixel<=output_x<WIDTH ? lines[(read_bank ? WIDTH : 0)+output_x] : 16'd0;
        if(!resetn || !ready)begin
            full<=0;write_bank<=0;read_bank<=0;field_active<=0;field_pal<=0;bad<=0;
            input_count<=0;input_row<=0;output_x<=0;output_y<=0;
            issued<=0;scan_x<=12'hfff;scan_y<=12'hfff;rgb565<=0;frame_start<=0;
            if(!resetn)malformed_frames<=0;
        end else if(start_field)begin
            if(field_active && (bad || output_y!=HEIGHT))malformed_frames<=malformed_frames+1'b1;
            full<=0;write_bank<=0;read_bank<=0;field_active<=1;bad<=0;
            field_pal<=pal;
            input_count<=0;input_row<=0;output_x<=0;output_y<=0;
            issued<=0;scan_x<=12'hfff;scan_y<=12'hfff;frame_start<=1;
        end else begin
            if(field_active && output_y!=HEIGHT && pal!=field_pal)bad<=1;
            if(input_active && field_active)begin
                if(full[write_bank] || input_count>=WIDTH || pixel_y!=expected_y ||
                   pixel_x!=2+input_count)bad<=1;
                else lines[(write_bank ? WIDTH : 0)+input_count]<={red[7:3],green[7:2],blue[7:3]};
                if(pixel_x==725)begin
                    if(input_count!=WIDTH-1)bad<=1;
                    else begin full[write_bank]<=1;rows[write_bank]<=input_row;write_bank<=!write_bank;end
                    input_count<=0;input_row<=input_row+1'b1;
                end else input_count<=input_count+1'b1;
            end
            if(issue)begin
                if(output_x==STRIDE-1)begin
                    output_x<=0;output_y<=output_y+1'b1;
                    if(output_y[0])begin full[read_bank]<=0;read_bank<=!read_bank;end
                end else output_x<=output_x+1'b1;
            end
        end
    end
endmodule
