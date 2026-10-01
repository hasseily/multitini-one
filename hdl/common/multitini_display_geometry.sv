// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Mode is stable while scanout is reset. All geometry derives from that single
// bundled mode value; it is never sampled as independent asynchronous bits.
module multitini_display_geometry #(
    parameter integer MACHINE=0,
    parameter integer WIDTH=382, HEIGHT=270
)(
    input wire [3:0] mode,
    output wire [11:0] h_active,h_front,h_sync,h_total,
    output wire [11:0] v_active,v_front,v_sync,v_total,
    output wire h_positive,v_positive,
    output reg [3:0] scale_num,scale_den,
    output reg [11:0] view_x,view_y,view_width,view_height
);
    import multitini_display_pkg::*;
    video_mode_t timing;
    always @* timing=video_mode(mode);
    assign h_active=timing.width,h_front=timing.h_front,h_sync=timing.h_sync,h_total=timing.h_total;
    assign v_active=timing.height,v_front=timing.v_front,v_sync=timing.v_sync,v_total=timing.v_total;
    assign h_positive=timing.h_positive,v_positive=timing.v_positive;
    // Evaluate sizes AND centering at elaboration. Subtracting two separately
    // selected dimensions would otherwise leave a carry chain between mode
    // selection and every pixel-rate viewport comparison.
    function automatic logic [55:0] geometry_for_mode(input logic [3:0] id);
        logic [$bits(video_mode_t)-1:0] selected_timing;
        logic [3:0] numerator, denominator;
        logic [11:0] width, height, left, top;
        begin
            selected_timing=video_mode(id);
            denominator=1;
            if(MACHINE==3)begin numerator=id==0 ? 3 : 1;denominator=id==0 ? 2 : 1;end
            else if(MACHINE==4)numerator=(id==0 || id==5) ? 4 : 3;
            else if(MACHINE==1)numerator=(id==1 || id==2 || id==4) ? 5 : 6;
            else numerator=id==0 ? 4 : (id==1 || id==2 || id==4) ? 2 : 3;
            if(MACHINE==3)begin width=id==0 ? WIDTH*3/2 : WIDTH;height=id==0 ? HEIGHT*3/2 : HEIGHT;end
            else if(MACHINE==4)begin
                width=(id==0 || id==5) ? WIDTH*4 : WIDTH*3;
                height=(id==0 || id==5) ? HEIGHT*4 : HEIGHT*3;
            end else if(MACHINE==1)begin
                width=(id==1 || id==2 || id==4) ? WIDTH*5 : WIDTH*6;
                height=(id==1 || id==2 || id==4) ? HEIGHT*5 : HEIGHT*6;
            end else begin
                width=id==0 ? WIDTH*4 : (id==1 || id==2 || id==4) ? WIDTH*2 : WIDTH*3;
                height=id==0 ? HEIGHT*4 : (id==1 || id==2 || id==4) ? HEIGHT*2 : HEIGHT*3;
            end
            // Retain the original 12-bit truncation before centering and the
            // 12-bit subtraction wrap for every WIDTH/HEIGHT parameter value.
            // Packed slices avoid a struct-field constant-function limitation
            // in Icarus. Width and height are the first two 12-bit fields.
            left=(selected_timing[$bits(video_mode_t)-1 -: 12]-width)>>1;
            top=(selected_timing[$bits(video_mode_t)-13 -: 12]-height)>>1;
            geometry_for_mode={numerator,denominator,left,top,width,height};
        end
    endfunction
    localparam [55:0] GEOMETRY_0=geometry_for_mode(4'd0);
    localparam [55:0] GEOMETRY_1=geometry_for_mode(4'd1);
    localparam [55:0] GEOMETRY_2=geometry_for_mode(4'd2);
    localparam [55:0] GEOMETRY_3=geometry_for_mode(4'd3);
    localparam [55:0] GEOMETRY_4=geometry_for_mode(4'd4);
    localparam [55:0] GEOMETRY_5=geometry_for_mode(4'd5);
    // IDs 6..15 historically use default timing but nonzero-mode scaling.
    localparam [55:0] GEOMETRY_DEFAULT=geometry_for_mode(4'd6);
    always @* begin
        case(mode)
            0: {scale_num,scale_den,view_x,view_y,view_width,view_height}=GEOMETRY_0;
            1: {scale_num,scale_den,view_x,view_y,view_width,view_height}=GEOMETRY_1;
            2: {scale_num,scale_den,view_x,view_y,view_width,view_height}=GEOMETRY_2;
            3: {scale_num,scale_den,view_x,view_y,view_width,view_height}=GEOMETRY_3;
            4: {scale_num,scale_den,view_x,view_y,view_width,view_height}=GEOMETRY_4;
            5: {scale_num,scale_den,view_x,view_y,view_width,view_height}=GEOMETRY_5;
            default: {scale_num,scale_den,view_x,view_y,view_width,view_height}=GEOMETRY_DEFAULT;
        endcase
    end
endmodule
