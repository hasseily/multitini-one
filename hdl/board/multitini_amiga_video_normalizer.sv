// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Standard PAL ECS 640x256 active image, bob doubled to 640x512. The native
// Denise pipeline places its RGB window at hires samples 206..845 after
// HSYNC. PAL rows 44..299 are lines 41..296 after long-field VSYNC, or one
// line later after a short interlace field's mid-line VSYNC. The retained
// NTSC path centers 480 rows; the machine profile currently exposes PAL only.
module multitini_amiga_video_normalizer(
    input wire clk,reset,ready,chip_tick,hsync_n,vsync_n,ntsc,hblank,vblank,
    input wire field_short,
    input wire [23:0] rgb,
    output reg [11:0] scan_x,scan_y,output reg [15:0] rgb565,
    output reg frame_start,output reg valid,
    output reg [31:0] malformed_frames
);
    localparam integer WIDTH=640,STRIDE=640,HEIGHT=512,CAPTURE_X=206;
    (* ram_style="block" *) reg [15:0] lines[0:2*WIDTH-1];
    reg [1:0] full;
    reg write_bank,read_bank,hs_q,vs_q,half,active,pending_frame;
    reg field_short_q;
    reg [9:0] native_x,output_x;
    reg [8:0] native_y;
    reg [9:0] output_y;
    reg [8:0] rows[0:1];
    reg [15:0] read_pixel;
    reg issued;reg [11:0] issued_x,issued_y;
    wire padding_line=ntsc && (output_y<16 || output_y>=496);
    wire output_issue=active && output_y<HEIGHT &&
        (padding_line || (full[read_bank] && rows[read_bank]==output_y[9:1]));
    wire [8:0] first_line=ntsc ? 9'd12 : 9'd41+{8'd0,field_short_q};
    wire [8:0] source_row=native_y-first_line;
    wire [8:0] canvas_row=source_row+(ntsc ? 9'd8 : 9'd0);
    wire source_line=native_y>=first_line && source_row<(ntsc ? 240 : 256);
    always @(posedge clk)begin
        frame_start<=0;valid<=issued;issued<=output_issue;
        scan_x<=issued ? issued_x : 12'hfff;scan_y<=issued ? issued_y : 12'hfff;rgb565<=read_pixel;
        issued_x<={2'd0,output_x};issued_y<={2'd0,output_y};
        if(output_issue)read_pixel<=output_x<WIDTH && !padding_line ? lines[(read_bank ? WIDTH : 0)+output_x] : 16'd0;
        if(reset || !ready)begin
            full<=0;write_bank<=0;read_bank<=0;hs_q<=1;vs_q<=1;half<=0;active<=0;pending_frame<=0;
            field_short_q<=0;
            native_x<=0;native_y<=0;output_x<=0;output_y<=0;issued<=0;valid<=0;
            scan_x<=12'hfff;scan_y<=12'hfff;rgb565<=0;frame_start<=0;
            if(reset)malformed_frames<=0;
        end else begin
            if(chip_tick)begin
                hs_q<=hsync_n;vs_q<=vsync_n;
                if(vs_q && !vsync_n)begin
                    native_y<=0;
                    field_short_q<=field_short;
                    // The final doubled source row can finish just after the
                    // next field's VSYNC. Drain it during the new top border.
                    if(active && (output_y!=HEIGHT || issued) && (output_issue || issued))pending_frame<=1;
                    else begin
                        if(active && output_y!=HEIGHT)malformed_frames<=malformed_frames+1'b1;
                        full<=0;write_bank<=0;read_bank<=0;pending_frame<=0;
                        output_y<=0;output_x<=0;active<=1;frame_start<=1;issued<=0;
                    end
                end else if(hs_q && !hsync_n)begin native_y<=native_y+1'b1;end
                if(hs_q && !hsync_n)begin native_x<=0;half<=0;end
                else begin
                    half<=!half;
                    if(half)begin
                        native_x<=native_x+1'b1;
                        if(source_line && native_x>=CAPTURE_X && native_x<CAPTURE_X+WIDTH)begin
                            lines[(write_bank ? WIDTH : 0)+(native_x-CAPTURE_X)]<=
                                (hblank || vblank) ? 16'd0 : {rgb[23:19],rgb[15:10],rgb[7:3]};
                            if(native_x==CAPTURE_X+WIDTH-1)begin
                                if(full[write_bank])begin active<=0;malformed_frames<=malformed_frames+1'b1;end
                                else begin full[write_bank]<=1;rows[write_bank]<=canvas_row;write_bank<=!write_bank;end
                            end
                        end
                    end
                end
            end
            if(output_issue)begin
                if(output_x==STRIDE-1)begin
                    output_x<=0;output_y<=output_y+1'b1;
                    if(output_y[0] && !padding_line)begin full[read_bank]<=0;read_bank<=!read_bank;end
                end else output_x<=output_x+1'b1;
            end
            if(pending_frame && ((output_y==HEIGHT && !issued) || native_y>=first_line))begin
                if(output_y!=HEIGHT)malformed_frames<=malformed_frames+1'b1;
                full<=0;write_bank<=0;read_bank<=0;pending_frame<=0;
                output_y<=0;output_x<=0;active<=1;frame_start<=1;issued<=0;
            end
        end
    end
endmodule
