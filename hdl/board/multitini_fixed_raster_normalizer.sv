// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Announce each origin before pixel (0,0), so capture can arm first.
// Invalid or missing coordinates abort the raster until the next origin.
// Padded widths require STRIDE-WIDTH+1 blank source clocks per row.
module multitini_fixed_raster_normalizer #(
    parameter integer WIDTH=256, HEIGHT=240, COORD_BITS=8
)(
    input wire clk,resetn,ready,pixel_ce,
    input wire [COORD_BITS-1:0] pixel_x,pixel_y,
    input wire [7:0] red,green,blue,
    output reg [11:0] scan_x,scan_y,
    output reg [15:0] rgb565,
    output reg frame_start,
    output reg [31:0] malformed_frames
);
    localparam integer STRIDE=((WIDTH+63)/64)*64;
    localparam [COORD_BITS-1:0] LAST_X=COORD_BITS'(WIDTH-1), LAST_Y=COORD_BITS'(HEIGHT-1);
    reg active,bad,pending,complete;
    reg [COORD_BITS-1:0] expected_x,expected_y;
    reg [COORD_BITS-1:0] pending_x,pending_y;
    reg [15:0] pending_rgb;
    reg [11:0] padding_x;
    reg [COORD_BITS-1:0] padding_y;
    wire padding=STRIDE!=WIDTH && padding_x!=0;
    wire origin=pixel_ce && pixel_x==0 && pixel_y==0;
    wire ordered=!complete && !padding && pixel_x==expected_x && pixel_y==expected_y;
    always @(posedge clk)begin
        frame_start<=0;
        scan_x<=12'hfff;scan_y<=12'hfff;
        if(pending)begin
            scan_x<=12'(pending_x);scan_y<=12'(pending_y);rgb565<=pending_rgb;
        end else if(padding && !bad)begin
            scan_x<=padding_x;scan_y<=12'(padding_y);rgb565<=0;
            padding_x<=padding_x==12'(STRIDE-1) ? 0 : padding_x+1'b1;
        end
        pending<=0;
        if(!resetn || !ready)begin
            active<=0;bad<=0;pending<=0;complete<=0;expected_x<=0;expected_y<=0;
            padding_x<=0;padding_y<=0;
            scan_x<=12'hfff;scan_y<=12'hfff;rgb565<=0;
            pending_x<=0;pending_y<=0;pending_rgb<=0;frame_start<=0;
            if(!resetn)malformed_frames<=0;
        end else if(pixel_ce && (origin || (active && !bad)))begin
            if(origin || ordered)begin
                if(origin)begin
                    if(active && (bad || !complete || padding))malformed_frames<=malformed_frames+1'b1;
                    active<=1;bad<=0;frame_start<=1;
                    scan_x<=12'hfff;scan_y<=12'hfff;
                    padding_x<=0;
                end
                pending<=1;pending_x<=pixel_x;pending_y<=pixel_y;
                pending_rgb<={red[7:3],green[7:2],blue[7:3]};
                expected_x<=pixel_x==LAST_X ? 0 : pixel_x+1'b1;
                expected_y<=pixel_x==LAST_X ? pixel_y+1'b1 : pixel_y;
                complete<=pixel_x==LAST_X && pixel_y==LAST_Y;
                if(STRIDE!=WIDTH && pixel_x==LAST_X)begin
                    padding_x<=12'(WIDTH);padding_y<=pixel_y;
                end
            end else begin
                bad<=1;
                // Reject a new row that arrives while its predecessor still
                // needs padding. The next origin resets the partial capture.
                if(padding)begin scan_x<=12'hfff;scan_y<=12'hfff;end
            end
        end
    end
endmodule
