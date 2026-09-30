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
    // Preserve accepted 1080p sizes. Smaller modes fit the complete source at
    // an integer scale; Vectrex also retains its existing 3:2 1080p ratio.
    // Every case is constant: no pixel-rate divider or multiplier is inferred.
    always @* begin
        scale_den=1;
        if(MACHINE==3)begin scale_num=mode==0 ? 3 : 1;scale_den=mode==0 ? 2 : 1;end
        else if(MACHINE==1)scale_num=(mode==1 || mode==2 || mode==4) ? 5 : 6;
        else scale_num=mode==0 ? 4 : (mode==1 || mode==2 || mode==4) ? 2 : 3;
        if(MACHINE==3)begin view_width=mode==0 ? WIDTH*3/2 : WIDTH;view_height=mode==0 ? HEIGHT*3/2 : HEIGHT;end
        else if(MACHINE==1)begin
            view_width=(mode==1 || mode==2 || mode==4) ? WIDTH*5 : WIDTH*6;
            view_height=(mode==1 || mode==2 || mode==4) ? HEIGHT*5 : HEIGHT*6;
        end else begin
            view_width=mode==0 ? WIDTH*4 : (mode==1 || mode==2 || mode==4) ? WIDTH*2 : WIDTH*3;
            view_height=mode==0 ? HEIGHT*4 : (mode==1 || mode==2 || mode==4) ? HEIGHT*2 : HEIGHT*3;
        end
        view_x=(timing.width-view_width)>>1;
        view_y=(timing.height-view_height)>>1;
    end
endmodule
