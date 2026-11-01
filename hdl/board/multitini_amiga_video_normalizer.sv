// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Two complete line banks preserve native RGB without stalling the machine.
// Bob emits each source row twice. Weave emits the long-field even rows,
// followed by short-field odd rows; the DDR writer places them in raster order.
// The calibrated Denise pipeline maps DIW H129 to HSYNC-relative sample206,
// and native V44 to line41 (long field) /42 (short field). Overscan follows
// programmed DIW metadata, never a guessed centered crop.
module multitini_amiga_video_normalizer(
    input wire clk,reset,ready,chip_tick,hsync_n,vsync_n,ntsc,hblank,vblank,
    input wire field_short,native_lace,
    input wire [1:0] capture_mode,
    input wire [8:0] diw_hstart,
    input wire [10:0] diw_vstart,
    input wire [23:0] rgb,
    output reg [11:0] scan_x,scan_y,output reg [15:0] rgb565,
    output reg frame_start,valid,frame_alt,frame_weave,
    output reg [1:0] frame_profile, // service-domain physical0/1/2; latched per frame
    output reg [31:0] malformed_frames
);
    localparam integer MAX_WIDTH=1280;
    (* ram_style="block" *) reg [15:0] lines[0:2*MAX_WIDTH-1];
    reg [1:0] full;
    reg write_bank,read_bank,hs_q,vs_q,half,active,pending_frame;
    reg field_short_q,origin_latched,requested_alt,requested_weave;
    reg [1:0] requested_profile;
    reg [10:0] native_x,native_full_x,output_x,capture_x;
    reg [8:0] native_y,first_line;
    reg [9:0] stream_y;
    reg [8:0] rows[0:1];
    reg row_field[0:1];
    reg [15:0] read_pixel;
    reg issued;reg [11:0] issued_x,issued_y;
    wire [10:0] width=frame_profile==2 ? 11'd1280:frame_alt ? 11'd724:11'd640;
    wire [10:0] stride=frame_profile==2 ? 11'd1280:frame_alt ? 11'd768:11'd640;
    wire [9:0] height=frame_alt ? 10'd566:10'd512;
    wire [8:0] field_rows=frame_alt ? 9'd283:9'd256;
    wire second_half=stream_y>={1'b0,field_rows};
    wire [8:0] output_row=frame_weave ? (second_half ? stream_y-field_rows:stream_y) : stream_y[9:1];
    wire padding_line=ntsc && (stream_y<16 || stream_y>=496);
    wire output_issue=active && stream_y<height &&
        (padding_line || (full[read_bank] && rows[read_bank]==output_row &&
                         (!frame_weave || row_field[read_bank]==second_half)));
    wire frame_complete=stream_y==height && !issued;
    wire [8:0] source_row=native_y-first_line;
    wire [8:0] canvas_row=source_row+(ntsc ? 9'd8:9'd0);
    wire source_line=native_y>=first_line && source_row<(ntsc ? 240:field_rows);
    wire want_alt=capture_mode==2 && !ntsc;
    wire want_wide=capture_mode==3 && !ntsc;
    wire [1:0] want_profile=want_wide ? 2'd2:want_alt ? 2'd1:2'd0;
    wire [10:0] sample_x=frame_profile==2 ? native_full_x:native_x;
    // Native interlace always forms a progressive pair, including manual
    // AmigaOS ScreenMode changes while the default physical profile is active.
    wire want_weave=native_lace && !ntsc;
    // Smaller/nonlaced application windows get a bounded border rather than
    // waiting for source samples in the following field.
    wire [10:0] live_width=want_wide ? 11'd1280:want_alt ? 11'd724:11'd640;
    wire [8:0] live_rows=want_alt ? 9'd283:9'd256;
    wire [8:0] bounded_h=diw_hstart>=26 && diw_hstart<=479 ? diw_hstart:9'd129;
    // Independent native SHRES probe calibrates DIWH129 to raw tick412.
    wire [11:0] candidate_x=want_wide ? {1'b0,bounded_h,2'b0}-12'd104:
        {2'b0,bounded_h,1'b0}-12'd52;
    wire [10:0] max_x=(want_wide ? 11'd1812:11'd906)-live_width;
    wire [10:0] live_x=capture_mode==0 ? 11'd206:
        candidate_x>{1'b0,max_x} ? max_x:candidate_x[10:0];
    wire [10:0] candidate_v=diw_vstart>=26 && diw_vstart<312 ? diw_vstart:11'd44;
    wire [8:0] max_v=9'd312-live_rows;
    wire [8:0] bounded_v=candidate_v>{2'd0,max_v} ? max_v:candidate_v[8:0];
    wire [8:0] live_first=ntsc ? 9'd12:
        capture_mode==0 ? 9'd41+{8'd0,field_short_q}:
        bounded_v-9'd3+{8'd0,field_short_q};
    wire new_field=chip_tick && vs_q && !vsync_n;
    wire new_frame=new_field && (!want_weave || !field_short);
    always @(posedge clk)begin
        frame_start<=0;valid<=issued;issued<=output_issue;
        scan_x<=issued ? issued_x:12'hfff;scan_y<=issued ? issued_y:12'hfff;rgb565<=read_pixel;
        issued_x<={1'd0,output_x};
        issued_y<=frame_weave ? {2'd0,output_row,1'b0}+{11'd0,second_half}:{2'd0,stream_y};
        if(output_issue)read_pixel<=output_x<width && !padding_line ?
            lines[(read_bank ? MAX_WIDTH:0)+output_x]:16'd0;
        if(reset || !ready)begin
            full<=0;write_bank<=0;read_bank<=0;hs_q<=1;vs_q<=1;half<=0;active<=0;pending_frame<=0;
            field_short_q<=0;origin_latched<=0;requested_alt<=0;requested_weave<=0;requested_profile<=0;
            native_x<=0;native_full_x<=0;native_y<=0;capture_x<=206;first_line<=41;
            output_x<=0;stream_y<=0;issued<=0;valid<=0;
            scan_x<=12'hfff;scan_y<=12'hfff;rgb565<=0;frame_start<=0;frame_alt<=0;frame_weave<=0;frame_profile<=0;
            if(reset)malformed_frames<=0;
        end else begin
            if(chip_tick)begin
                hs_q<=hsync_n;vs_q<=vsync_n;
                if(new_field)begin
                    native_y<=0;field_short_q<=field_short;origin_latched<=0;
                end else if(hs_q && !hsync_n)native_y<=native_y+1'b1;
                if(!origin_latched)begin capture_x<=live_x;first_line<=live_first;end
                if(hs_q && !hsync_n)begin native_x<=0;native_full_x<=0;half<=0;end
                else begin
                    half<=!half;native_full_x<=native_full_x+1'b1;
                    if(half)native_x<=native_x+1'b1;
                    // Fixed SHRES grid samples every consumed native edge;
                    // lower guest resolutions naturally repeat their pixels.
                    if(frame_profile==2 || half)begin
                        if(active && source_line && sample_x>=capture_x && sample_x<capture_x+width)begin
                            origin_latched<=1;
                            // Maximum digital overscan follows Denise's DIW;
                            // its final pixels can extend through analog HBL.
                            lines[(write_bank ? MAX_WIDTH:0)+(sample_x-capture_x)]<=
                                (vblank || (hblank && !frame_alt)) ? 16'd0:{rgb[23:19],rgb[15:10],rgb[7:3]};
                            if(sample_x==capture_x+width-1)begin
                                if(full[write_bank])begin active<=0;malformed_frames<=malformed_frames+1'b1;end
                                else begin
                                    full[write_bank]<=1;rows[write_bank]<=canvas_row;
                                    row_field[write_bank]<=field_short_q;write_bank<=!write_bank;
                                end
                            end
                        end
                    end
                end
            end
            if(output_issue)begin
                if(output_x==stride-1)begin
                    output_x<=0;stream_y<=stream_y+1'b1;
                    if((frame_weave || stream_y[0]) && !padding_line)begin
                        full[read_bank]<=0;read_bank<=!read_bank;
                    end
                end else output_x<=output_x+1'b1;
            end
            if(new_frame)begin
                requested_alt<=want_alt;requested_weave<=want_weave;requested_profile<=want_profile;
                // Finish a final buffered row during the next top border.
                if(active && !frame_complete && (output_issue || issued))pending_frame<=1;
                else begin
                    if(active && !frame_complete)malformed_frames<=malformed_frames+1'b1;
                    full<=0;write_bank<=0;read_bank<=0;pending_frame<=0;
                    output_x<=0;stream_y<=0;active<=1;frame_start<=1;issued<=0;
                    frame_alt<=want_alt;frame_weave<=want_weave;frame_profile<=want_profile;
                end
            end
            if(pending_frame && (frame_complete || native_y>=first_line))begin
                if(!frame_complete)malformed_frames<=malformed_frames+1'b1;
                full<=0;write_bank<=0;read_bank<=0;pending_frame<=0;
                output_x<=0;stream_y<=0;active<=1;frame_start<=1;issued<=0;
                frame_alt<=requested_alt;frame_weave<=requested_weave;frame_profile<=requested_profile;
            end
        end
    end
endmodule
