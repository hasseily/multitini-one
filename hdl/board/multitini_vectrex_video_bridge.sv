// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Fixed portrait 540x720 intensity image at 3:2 scale in the 1080p scan.
// Three-pixel lookahead covers address, synchronous RAM and pixel registers.
// Incremental phases avoid division on the 148.5 MHz scanout path.
module multitini_vectrex_video_bridge (
    input wire pixel_clk, pixel_resetn,
    input wire source_ready,
    input wire [11:0] scan_x, scan_y,
    input wire vblank_start,
    output wire [18:0] pixel_address,
    input wire [7:0] pixel_intensity,
    output reg machine_active,
    output wire [15:0] machine_rgb565,
    output wire [11:0] viewport_x, viewport_y, viewport_width, viewport_height
);
    // RAMB36E1 cascade partners require one physical ADDRB[15] net.
    // Vivado 2025.2 replicated this address flop across a cascade pair,
    // causing REQP-1962. Preserve this bit only; other bits may replicate.
    // UG904 Physical Optimization Constraints documents DONT_TOUCH's effect.
    (* DONT_TOUCH = "TRUE" *) reg pixel_address_bit15;
    reg [14:0] pixel_address_low;
    reg [2:0] pixel_address_high;
    assign pixel_address={pixel_address_high,pixel_address_bit15,pixel_address_low};
    assign viewport_x=12'd555, viewport_y=12'd0;
    assign viewport_width=12'd810, viewport_height=12'd1080;

    wire source_ready_pixel;
    reg read_visible;
    reg [7:0] pixel_sample;
    reg [18:0] row_address;
    reg [1:0] horizontal_phase, vertical_phase;
    cdc_bit_sync ready_sync (
        .clk(pixel_clk),.resetn(pixel_resetn),.din(source_ready),.dout(source_ready_pixel)
    );
    always @(posedge pixel_clk) begin
        if (!pixel_resetn) begin
            machine_active<=0;read_visible<=0;pixel_sample<=0;
            {pixel_address_high,pixel_address_bit15,pixel_address_low}<=0;
            row_address<=0;horizontal_phase<=0;vertical_phase<=0;
        end else begin
            if (vblank_start) machine_active<=source_ready_pixel;
            pixel_sample<=pixel_intensity;
            read_visible<=scan_x>=554 && scan_x<1364 && scan_y<1080;
            if (scan_x==0) begin
                if (scan_y==0 || scan_y>=1080) begin
                    row_address<=0;vertical_phase<=0;
                end else begin
                    if (vertical_phase!=0) row_address<=row_address+19'd540;
                    vertical_phase<=vertical_phase==2 ? 0 : vertical_phase+1'b1;
                end
            end
            if (scan_x==552 && scan_y<1080) begin
                {pixel_address_high,pixel_address_bit15,pixel_address_low}<=row_address;
                horizontal_phase<=0;
            end else if (scan_x>=553 && scan_x<=1361 && scan_y<1080) begin
                if (horizontal_phase!=0)
                    {pixel_address_high,pixel_address_bit15,pixel_address_low}<=pixel_address+1'b1;
                horizontal_phase<=horizontal_phase==2 ? 0 : horizontal_phase+1'b1;
            end else {pixel_address_high,pixel_address_bit15,pixel_address_low}<=0;
        end
    end
    assign machine_rgb565=read_visible ?
        {pixel_sample[7:3],pixel_sample[7:2],pixel_sample[7:3]} : 16'd0;
endmodule
