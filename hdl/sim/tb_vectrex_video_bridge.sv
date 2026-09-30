// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_vectrex_video_bridge;
    parameter bit DYNAMIC_DISPLAY=0;
    reg [3:0] mode=0;
    wire [11:0] vx,vy,vw,vh;
    wire [3:0] sn,sd;
    generate if(DYNAMIC_DISPLAY)begin
    multitini_display_geometry #(.MACHINE(3),.WIDTH(540),.HEIGHT(720)) geometry(
        .mode(mode),.view_x(vx),.view_y(vy),.view_width(vw),.view_height(vh),
        .scale_num(sn),.scale_den(sd));
    end else begin
        assign vx=555,vy=0,vw=810,vh=1080,sn=3,sd=2;
    end endgenerate
    reg clk=0, resetn=0, ready=0, vblank=0;
    reg [11:0] x=0,y=0;
    wire [18:0] address;
    reg [7:0] intensity=0;
    wire active;
    wire [15:0] rgb;
    wire [11:0] viewport_x,viewport_y,viewport_width,viewport_height;
    integer row,col,frame,m,expected_address,checks=0;
    reg [7:0] expected_intensity;
    always #5 clk=~clk;
    // Independent synchronous memory with a distinctive address pattern.
    always @(posedge clk) intensity <= (address ^ (address>>8)) & 255;
    multitini_vectrex_video_bridge #(.DYNAMIC_DISPLAY(DYNAMIC_DISPLAY)) dut(
        .display_view_x(vx),.display_view_y(vy),.display_view_width(vw),.display_view_height(vh),
        .display_scale_num(sn),.display_scale_den(sd),
        .pixel_clk(clk),.pixel_resetn(resetn),.source_ready(ready),
        .scan_x(x),.scan_y(y),.vblank_start(vblank),.pixel_address(address),
        .pixel_intensity(intensity),.machine_active(active),.machine_rgb565(rgb),
        .overlay_rgba(16'd0),.overlay_valid(1'b0),.video_config(5'd0),.source_x(),.source_y(),
        .viewport_x(viewport_x),.viewport_y(viewport_y),
        .viewport_width(viewport_width),.viewport_height(viewport_height)
    );
    initial begin
        repeat(3) @(negedge clk);
        resetn=1;ready=1;
        if(viewport_x!==555 || viewport_y!==0 || viewport_width!==810 || viewport_height!==1080)
            $fatal(1,"Fixed Vectrex viewport geometry mismatch");
        for(m=0;m<(DYNAMIC_DISPLAY ? 6 : 1);m=m+1) begin
        resetn=0;mode=4'(m);repeat(4) @(negedge clk);resetn=1;
        // Two complete scans prove vertical phase restart as well as every
        // duplicated row/column, both portrait edges and the blanking area.
        for(frame=0;frame<2;frame=frame+1) begin
            for(row=0;row<vy+vh+2;row=row+1) begin
                for(col=0;col<vx+vw+15;col=col+1) begin
                    x=col;y=row;vblank=(row==vy+vh && col==0);
                    #1;
                    if(col>=vx && col<vx+vw && row>=vy && row<vy+vh) begin
                        expected_address=((row-vy)*sd/sn)*540+((col-vx)*sd/sn);
                        expected_intensity=(expected_address^(expected_address>>8))&255;
                        if(rgb!=={expected_intensity[7:3],expected_intensity[7:2],expected_intensity[7:3]})
                            $fatal(1,"Pixel mismatch frame=%0d x=%0d y=%0d address=%0d got=%h",frame,col,row,expected_address,rgb);
                        checks=checks+1;
                    end else if(rgb!==0 && col!=0)
                        $fatal(1,"Nonblack portrait margin at %0d,%0d",col,row);
                    @(negedge clk);
                end
            end
            if(!active) $fatal(1,"Ready not latched at vertical blank");
        end
        end
        ready=0;repeat(4) @(negedge clk);vblank=1;
        @(negedge clk);if(active) $fatal(1,"Ready loss not applied at blank");
        $display("PASS Vectrex DVI bridge: %0d pixels, scale phases, edges and frame restart",checks);
        $finish;
    end
endmodule
