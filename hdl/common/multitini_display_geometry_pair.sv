// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Three elaborated geometry tables, selected only while native-video owners
// are drained. Keeping dimensions literal avoids pixel-rate division logic.
module multitini_display_geometry_pair #(
    parameter bit ENABLE_MULTIPLIER=1'b0,
    parameter integer MACHINE=0,WIDTH=382,HEIGHT=270,
    parameter integer ALT_WIDTH=WIDTH,ALT_HEIGHT=HEIGHT,
    parameter integer WIDE_WIDTH=WIDTH,WIDE_HEIGHT=HEIGHT,
    parameter bit VIRTUAL_CAPTURE=1'b0
)(
    input wire [3:0] mode,input wire [2:0] multiplier,input wire [1:0] source_profile,
    output wire [11:0] h_active,h_front,h_sync,h_total,
    output wire [11:0] v_active,v_front,v_sync,v_total,
    output wire h_positive,v_positive,
    output wire [3:0] scale_num,scale_den,
    output wire [11:0] view_x,view_y,view_width,view_height,
    output wire [9:0] source_x0,source_y0,
    output wire [3:0] phase_x0,phase_y0
);
    wire [181:0] tables[0:2];
    localparam integer OTHER_WIDTH=ALT_WIDTH>0 ? ALT_WIDTH : WIDTH;
    localparam integer OTHER_HEIGHT=ALT_HEIGHT>0 ? ALT_HEIGHT : HEIGHT;
    localparam integer THIRD_WIDTH=MACHINE==7 && WIDE_WIDTH>0 ? WIDE_WIDTH : WIDTH;
    localparam integer THIRD_HEIGHT=MACHINE==7 && WIDE_HEIGHT>0 ? WIDE_HEIGHT : HEIGHT;
    genvar p;
    generate for(p=0;p<3;p=p+1)begin:g_profile
        wire [11:0] ha,hf,hs,ht,va,vf,vs,vt,vx,vy,vw,vh;
        wire hp,vp;
        wire [3:0] sn,sd,px,py;
        wire [9:0] sx,sy;
        multitini_display_geometry #(.ENABLE_MULTIPLIER(ENABLE_MULTIPLIER),.MACHINE(MACHINE),
            .CLIP_OVERSIZE_CANVAS(p==2 && MACHINE==7),
            .VIRTUAL_CAPTURE(VIRTUAL_CAPTURE && p==2 && MACHINE==7),
            .WIDTH(p==2 ? THIRD_WIDTH : p==1 ? OTHER_WIDTH : WIDTH),
            .HEIGHT(p==2 ? THIRD_HEIGHT : p==1 ? OTHER_HEIGHT : HEIGHT)) fixed_i(
            .mode(mode),.multiplier(multiplier),
            .h_active(ha),.h_front(hf),.h_sync(hs),.h_total(ht),
            .v_active(va),.v_front(vf),.v_sync(vs),.v_total(vt),.h_positive(hp),.v_positive(vp),
            .scale_num(sn),.scale_den(sd),.view_x(vx),.view_y(vy),.view_width(vw),.view_height(vh),
            .source_x0(sx),.source_y0(sy),.phase_x0(px),.phase_y0(py));
        assign tables[p]={ha,hf,hs,ht,va,vf,vs,vt,hp,vp,sn,sd,vx,vy,vw,vh,sx,sy,px,py};
    end endgenerate
    wire [1:0] selected_profile=source_profile==2 &&
        (THIRD_WIDTH!=WIDTH || THIRD_HEIGHT!=HEIGHT) ? 2'd2 :
        source_profile==1 && (OTHER_WIDTH!=WIDTH || OTHER_HEIGHT!=HEIGHT) ? 2'd1 : 2'd0;
    assign {h_active,h_front,h_sync,h_total,v_active,v_front,v_sync,v_total,
        h_positive,v_positive,scale_num,scale_den,view_x,view_y,view_width,view_height,
        source_x0,source_y0,phase_x0,phase_y0}=tables[selected_profile];
endmodule
