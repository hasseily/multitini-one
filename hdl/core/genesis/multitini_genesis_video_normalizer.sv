// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Convert each complete MD field to a 320x240 stream. H32 pixels expand 5:4;
// 224-line fields have eight black rows above/below. Interlace is bobbed one
// field at a time. Two line stores decouple the chip pixel enable from DDR.
// Output coordinates are outside the surface while idle, so the common
// native capture never samples a held coordinate as an extra pixel.
module multitini_genesis_video_normalizer (
    input wire clk,resetn,ready,
    input wire pixel_ce,de,vsync,
    input wire [7:0] red,green,blue,
    input wire mode_240,
    output reg [11:0] scan_x,scan_y,
    output reg [15:0] rgb565,
    output reg frame_start,
    output reg [31:0] dropped_fields
);
    (* ram_style="block" *) reg [15:0] lines[0:639];
    reg [1:0] full;
    reg [8:0] lengths[0:1];
    reg [8:0] rows[0:1];
    reg write_bank,read_bank,vsync_old,de_old,field_active,bad;
    reg [8:0] input_x,input_y,output_x,output_y,source_x;
    reg [2:0] repeat_phase;
    reg [8:0] height;
    wire [8:0] top_row=height==240 ? 0 : 8;
    wire blank_row=output_y<top_row || output_y>=top_row+height;
    wire line_ready=full[read_bank] && rows[read_bank]==output_y-top_row;
    wire left_column_blank=lengths[read_bank]==248 || lengths[read_bank]==312;
    wire blank_pixel=blank_row || (left_column_blank && source_x<8);
    wire [8:0] read_x=left_column_blank ? source_x-9'd8 : source_x;
    wire issue=field_active && !bad && output_y<240 && (blank_row || line_ready);
    reg issued;
    reg [11:0] issued_x,issued_y;
    reg [15:0] read_pixel;
    reg issued_blank;
    always @(posedge clk) begin
        frame_start<=0;
        scan_x<=issued ? issued_x : 12'hfff;
        scan_y<=issued ? issued_y : 12'hfff;
        rgb565<=issued_blank ? 16'd0 : read_pixel;
        issued<=issue;
        issued_x<={3'd0,output_x};issued_y<={3'd0,output_y};issued_blank<=blank_pixel;
        if(issue && !blank_pixel)read_pixel<=lines[(read_bank ? 320 : 0)+read_x];
        if(!resetn || !ready)begin
            full<=0;write_bank<=0;read_bank<=0;vsync_old<=0;de_old<=0;
            field_active<=0;bad<=0;input_x<=0;input_y<=0;output_x<=0;output_y<=0;
            source_x<=0;repeat_phase<=0;height<=224;issued<=0;
            scan_x<=12'hfff;scan_y<=12'hfff;rgb565<=0;frame_start<=0;
            if(!resetn)dropped_fields<=0;
        end else begin
            vsync_old<=vsync;
            if(vsync && !vsync_old)begin
                if(field_active && (bad || output_y!=240))dropped_fields<=dropped_fields+1'b1;
                full<=0;write_bank<=0;read_bank<=0;field_active<=1;bad<=0;
                input_x<=0;input_y<=0;output_x<=0;output_y<=0;source_x<=0;repeat_phase<=0;
                height<=mode_240 ? 240 : 224;de_old<=0;issued<=0;
                scan_x<=12'hfff;scan_y<=12'hfff;frame_start<=1;
            end else begin
                if(pixel_ce)begin
                    de_old<=de;
                    if(de && field_active && input_y<height)begin
                        if(full[write_bank] || input_x>=320)bad<=1;
                        else lines[(write_bank ? 320 : 0)+input_x]<={red[7:3],green[7:2],blue[7:3]};
                        input_x<=input_x+1'b1;
                    end
                    if(!de && de_old && field_active)begin
                        if(input_y<height)begin
                            if(input_x!=248 && input_x!=256 && input_x!=312 && input_x!=320)bad<=1;
                            else begin
                                full[write_bank]<=1;lengths[write_bank]<=input_x;
                                rows[write_bank]<=input_y;write_bank<=!write_bank;
                            end
                            input_y<=input_y+1'b1;
                        end
                        input_x<=0;
                    end
                end
                if(issue)begin
                    if(output_x==319)begin
                        output_x<=0;output_y<=output_y+1'b1;source_x<=0;repeat_phase<=0;
                        if(!blank_row)begin full[read_bank]<=0;read_bank<=!read_bank;end
                    end else begin
                        output_x<=output_x+1'b1;
                        if(lengths[read_bank]<=256)begin
                            // floor(x*4/5): the first source column is repeated.
                            if(repeat_phase!=0)source_x<=source_x+1'b1;
                            repeat_phase<=repeat_phase==4 ? 0 : repeat_phase+1'b1;
                        end else source_x<=source_x+1'b1;
                    end
                end
            end
        end
    end
endmodule
