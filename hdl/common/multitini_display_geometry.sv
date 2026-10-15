// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Mode is stable while scanout is reset. All geometry derives from that single
// bundled mode value; it is never sampled as independent asynchronous bits.
module multitini_display_geometry #(
    parameter bit ENABLE_MULTIPLIER=1'b0,
    parameter integer MACHINE=0,
    parameter integer WIDTH=382, HEIGHT=270
)(
    input wire [3:0] mode,
    input wire [2:0] multiplier,
    output wire [11:0] h_active,h_front,h_sync,h_total,
    output wire [11:0] v_active,v_front,v_sync,v_total,
    output wire h_positive,v_positive,
    output reg [3:0] scale_num,scale_den,
    output reg [11:0] view_x,view_y,view_width,view_height,
    output reg [9:0] source_x0,source_y0,
    output reg [3:0] phase_x0,phase_y0
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
    function automatic logic [83:0] geometry_for_mode(input logic [3:0] id, input logic [2:0] size);
        logic [$bits(video_mode_t)-1:0] selected_timing;
        logic [3:0] numerator, denominator;
        logic [11:0] width, height, left, top;
        integer signed signed_left,signed_top;
        integer crop_x,crop_y;
        logic [9:0] start_x,start_y;
        logic [3:0] start_phase_x,start_phase_y;
        begin
            selected_timing=video_mode(id);
            denominator=1;
            if(MACHINE==3)begin numerator=id==0 ? 3 : 1;denominator=id==0 ? 2 : 1;end
            else if(MACHINE==5)numerator=(id==0 || id==3 || id==5) ? 2 : 1;
            else if(MACHINE==4 || MACHINE==6)numerator=(id==0 || id==5) ? 4 : 3;
            else if(MACHINE==1)numerator=(id==1 || id==2 || id==4) ? 5 : 6;
            else numerator=id==0 ? 4 : (id==1 || id==2 || id==4) ? 2 : 3;
            if(MACHINE==3)begin width=id==0 ? WIDTH*3/2 : WIDTH;height=id==0 ? HEIGHT*3/2 : HEIGHT;end
            else if(MACHINE==5)begin
                width=(id==0 || id==3 || id==5) ? WIDTH*2 : WIDTH;
                height=(id==0 || id==3 || id==5) ? HEIGHT*2 : HEIGHT;
            end
            else if(MACHINE==4 || MACHINE==6)begin
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
            start_x=0;start_y=0;start_phase_x=0;start_phase_y=0;
            if(size>=1 && size<=4)begin
                numerator={1'b0,size};denominator=1;
                width=12'(WIDTH*size);height=12'(HEIGHT*size);
                signed_left=(int'(selected_timing[$bits(video_mode_t)-1 -: 12])-int'(width))>>>1;
                signed_top=(int'(selected_timing[$bits(video_mode_t)-13 -: 12])-int'(height))>>>1;
                left=12'(signed_left);top=12'(signed_top);
                crop_x=signed_left<0 ? -signed_left : 0;
                crop_y=signed_top<0 ? -signed_top : 0;
                start_x=10'(crop_x/size);start_y=10'(crop_y/size);
                start_phase_x=4'(crop_x%size);start_phase_y=4'(crop_y%size);
            end
            geometry_for_mode={numerator,denominator,left,top,width,height,
                start_x,start_y,start_phase_x,start_phase_y};
        end
    endfunction
    logic [83:0] geometry;
    wire [2:0] selected_size=ENABLE_MULTIPLIER ? multiplier : 3'd0;
    // Literal arguments keep all multiply/divide/crop arithmetic at elaboration.
    always @* begin
        case({selected_size,mode})
            7'h00: geometry = geometry_for_mode(4'd0, 3'd0);
            7'h10: geometry = geometry_for_mode(4'd0, 3'd1);
            7'h20: geometry = geometry_for_mode(4'd0, 3'd2);
            7'h30: geometry = geometry_for_mode(4'd0, 3'd3);
            7'h40: geometry = geometry_for_mode(4'd0, 3'd4);
            7'h01: geometry = geometry_for_mode(4'd1, 3'd0);
            7'h11: geometry = geometry_for_mode(4'd1, 3'd1);
            7'h21: geometry = geometry_for_mode(4'd1, 3'd2);
            7'h31: geometry = geometry_for_mode(4'd1, 3'd3);
            7'h41: geometry = geometry_for_mode(4'd1, 3'd4);
            7'h02: geometry = geometry_for_mode(4'd2, 3'd0);
            7'h12: geometry = geometry_for_mode(4'd2, 3'd1);
            7'h22: geometry = geometry_for_mode(4'd2, 3'd2);
            7'h32: geometry = geometry_for_mode(4'd2, 3'd3);
            7'h42: geometry = geometry_for_mode(4'd2, 3'd4);
            7'h03: geometry = geometry_for_mode(4'd3, 3'd0);
            7'h13: geometry = geometry_for_mode(4'd3, 3'd1);
            7'h23: geometry = geometry_for_mode(4'd3, 3'd2);
            7'h33: geometry = geometry_for_mode(4'd3, 3'd3);
            7'h43: geometry = geometry_for_mode(4'd3, 3'd4);
            7'h04: geometry = geometry_for_mode(4'd4, 3'd0);
            7'h14: geometry = geometry_for_mode(4'd4, 3'd1);
            7'h24: geometry = geometry_for_mode(4'd4, 3'd2);
            7'h34: geometry = geometry_for_mode(4'd4, 3'd3);
            7'h44: geometry = geometry_for_mode(4'd4, 3'd4);
            7'h05: geometry = geometry_for_mode(4'd5, 3'd0);
            7'h15: geometry = geometry_for_mode(4'd5, 3'd1);
            7'h25: geometry = geometry_for_mode(4'd5, 3'd2);
            7'h35: geometry = geometry_for_mode(4'd5, 3'd3);
            7'h45: geometry = geometry_for_mode(4'd5, 3'd4);
            default: geometry=geometry_for_mode(4'd6,3'd0);
        endcase
        {scale_num,scale_den,view_x,view_y,view_width,view_height,
            source_x0,source_y0,phase_x0,phase_y0}=geometry;
    end
endmodule
