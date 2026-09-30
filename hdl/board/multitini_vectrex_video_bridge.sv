// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Portrait 540x720 intensity image: 3:2 at 1080p, 1:1 in smaller modes.
// Seven-pixel lookahead covers address, synchronous RAM and compositing stages.
// Incremental phases avoid division on the 148.5 MHz scanout path.
module multitini_vectrex_video_bridge #(parameter bit DYNAMIC_DISPLAY=1'b0) (
    input wire [11:0] display_view_x, display_view_y, display_view_width, display_view_height,
    input wire [3:0] display_scale_num, display_scale_den,

    input wire pixel_clk, pixel_resetn,
    input wire source_ready,
    input wire [11:0] scan_x, scan_y,
    input wire vblank_start,
    output wire [18:0] pixel_address,
    input wire [7:0] pixel_intensity,
    input wire [15:0] overlay_rgba,
    input wire overlay_valid,
    input wire [4:0] video_config,
    output reg [9:0] source_x, source_y,
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
    assign viewport_x=DYNAMIC_DISPLAY ? display_view_x : 12'd555;
    assign viewport_y=DYNAMIC_DISPLAY ? display_view_y : 12'd0;
    assign viewport_width=DYNAMIC_DISPLAY ? display_view_width : 12'd810;
    assign viewport_height=DYNAMIC_DISPLAY ? display_view_height : 12'd1080;
    wire unit_scale=DYNAMIC_DISPLAY && display_scale_den==1;
    wire visible_y=scan_y>=viewport_y && scan_y<viewport_y+viewport_height;

    wire source_ready_pixel;
    reg [18:0] row_address;
    reg [1:0] horizontal_phase, vertical_phase;
    reg [7:0] intensity_sample;
    reg [15:0] artwork_sample;
    reg artwork_valid_sample;
    cdc_bit_sync ready_sync (
        .clk(pixel_clk),.resetn(pixel_resetn),.din(source_ready),.dout(source_ready_pixel)
    );
    always @(posedge pixel_clk) begin
        if (!pixel_resetn) begin
            machine_active<=0;source_x<=0;source_y<=0;
            {pixel_address_high,pixel_address_bit15,pixel_address_low}<=0;
            row_address<=0;horizontal_phase<=0;vertical_phase<=0;
            intensity_sample<=0;artwork_sample<=0;artwork_valid_sample<=0;
        end else begin
            // Isolate the large cascaded framebuffer read mux from gain and
            // beam-tint arithmetic. Artwork uses the same additional stage.
            intensity_sample<=pixel_intensity;
            artwork_sample<=overlay_rgba;artwork_valid_sample<=overlay_valid;
            if (vblank_start) machine_active<=source_ready_pixel;
            if (scan_x==0) begin
                if (scan_y<=viewport_y || scan_y>=viewport_y+viewport_height) begin
                    row_address<=0;vertical_phase<=0;source_y<=0;
                end else begin
                    if (unit_scale || vertical_phase!=0) begin row_address<=row_address+19'd540;source_y<=source_y+1'b1;end
                    vertical_phase<=vertical_phase==2 ? 0 : vertical_phase+1'b1;
                end
            end
            if (scan_x==viewport_x-7 && visible_y) begin
                {pixel_address_high,pixel_address_bit15,pixel_address_low}<=row_address;
                horizontal_phase<=0;source_x<=0;
            end else if (scan_x>=viewport_x-6 && scan_x<viewport_x+viewport_width-7 && visible_y) begin
                if (unit_scale || horizontal_phase!=0) begin
                    {pixel_address_high,pixel_address_bit15,pixel_address_low}<=pixel_address+1'b1;
                    source_x<=source_x+1'b1;
                end
                horizontal_phase<=horizontal_phase==2 ? 0 : horizontal_phase+1'b1;
            end else begin {pixel_address_high,pixel_address_bit15,pixel_address_low}<=0;source_x<=0;end
        end
    end
    multitini_vectrex_compositor compositor_i(
        .clk(pixel_clk),.resetn(pixel_resetn),
        .visible(scan_x>=viewport_x-4 && scan_x<viewport_x+viewport_width-4 && visible_y),
        .intensity(intensity_sample),.rgba(artwork_sample),.artwork_valid(artwork_valid_sample),
        .video_config(video_config),.rgb565(machine_rgb565));
endmodule
