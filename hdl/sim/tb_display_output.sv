// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_display_output;
    reg clk=0,control_clk=0,resetn=0,control_resetn=0;
    always #5 clk=~clk;
    always #7 control_clk=~control_clk;
    reg [3:0] mode=0;
    wire [11:0] ha,hf,hs,ht,va,vf,vs,vt;
    wire hp,vp;
    wire [3:0] sn[0:2],sd[0:2];
    wire [11:0] vx[0:2],vy[0:2],vw[0:2],vh[0:2];
    multitini_display_geometry #(.MACHINE(0)) c64(.mode(mode),
        .h_active(ha),.h_front(hf),.h_sync(hs),.h_total(ht),
        .v_active(va),.v_front(vf),.v_sync(vs),.v_total(vt),.h_positive(hp),.v_positive(vp),
        .scale_num(sn[0]),.scale_den(sd[0]),.view_x(vx[0]),.view_y(vy[0]),.view_width(vw[0]),.view_height(vh[0]));
    multitini_display_geometry #(.MACHINE(1),.WIDTH(160),.HEIGHT(144)) gbc(.mode(mode),
        .scale_num(sn[1]),.scale_den(sd[1]),.view_x(vx[1]),.view_y(vy[1]),.view_width(vw[1]),.view_height(vh[1]));
    multitini_display_geometry #(.MACHINE(3),.WIDTH(540),.HEIGHT(720)) vectrex(.mode(mode),
        .scale_num(sn[2]),.scale_den(sd[2]),.view_x(vx[2]),.view_y(vy[2]),.view_width(vw[2]),.view_height(vh[2]));
    wire [11:0] x,y;
    wire vblank,de,h,v,opaque;
    wire [15:0] rgb;
    wire [4:0] red,blue;wire [5:0] green;
    multitini_video_test #(.DYNAMIC_DISPLAY(1)) video(
        .pixel_clk(clk),.resetn(resetn),.enable(1'b0),.machine_enable(1'b0),
        .display_h_active(ha),.display_h_front(hf),.display_h_sync(hs),.display_h_total(ht),
        .display_v_active(va),.display_v_front(vf),.display_v_sync(vs),.display_v_total(vt),
        .display_h_positive(hp),.display_v_positive(vp),.ui_opaque(opaque),.ui_rgb565(rgb),
        .scan_x(x),.scan_y(y),.vblank_start(vblank),.dvi_de(de),.dvi_hsync(h),.dvi_vsync(v),
        .dvi_red(red),.dvi_grn(green),.dvi_blu(blue));
    reg wr=0,commit=0;
    reg [15:0] addr=0;
    wire ready,active;
    multitini_text_overlay #(.DYNAMIC_DISPLAY(1)) text_i(
        .control_clk(control_clk),.control_resetn(control_resetn),
        .vram_wr_en(wr),.vram_wr_addr(addr),.vram_wr_data(32'h68206820),.vram_wr_strb(4'hf),
        .commit_valid(commit),.commit_visible(1'b1),.commit_bank(1'b0),.commit_dense(1'b1),
        .commit_ready(ready),.active_visible_control(active),
        .pixel_clk(clk),.pixel_resetn(resetn),.scan_x(x),.scan_y(y),.vblank_start(vblank),
        .display_h_active(ha),.display_h_total(ht),.display_v_active(va),.display_v_total(vt),
        .pixel_opaque(opaque),.pixel_rgb565(rgb));
    integer expected_w[0:5],expected_h[0:5],expected_ht[0:5],expected_vt[0:5];
    integer pixels,cycles,hcount,vcount,blanks,text_pixels,text_w,text_h;
    initial begin
        expected_w[0]=1920;expected_h[0]=1080;expected_ht[0]=2200;expected_vt[0]=1125;
        expected_w[1]=1024;expected_h[1]=768;expected_ht[1]=1344;expected_vt[1]=806;
        expected_w[2]=1200;expected_h[2]=800;expected_ht[2]=1360;expected_vt[2]=828;
        expected_w[3]=1280;expected_h[3]=1024;expected_ht[3]=1688;expected_vt[3]=1066;
        expected_w[4]=1360;expected_h[4]=768;expected_ht[4]=1792;expected_vt[4]=795;
        expected_w[5]=1680;expected_h[5]=1050;expected_ht[5]=1840;expected_vt[5]=1080;
        repeat(4) @(negedge control_clk);control_resetn=1;
        for(integer word_index=0;word_index<3840;word_index=word_index+1)begin
            @(negedge control_clk);wr=1;addr=16'h4000+word_index*4;
        end
        @(negedge control_clk);wr=0;
        for(integer m=0;m<6;m=m+1)begin
            @(negedge clk);resetn=0;mode=4'(m);
            repeat(32) @(negedge clk);resetn=1;
            if(ha!=expected_w[m] || va!=expected_h[m] || ht!=expected_ht[m] || vt!=expected_vt[m])$fatal(1,"mode geometry");
            for(integer core=0;core<3;core=core+1)begin
                if(vw[core]==0 || vh[core]==0 || vx[core]+vw[core]>ha || vy[core]+vh[core]>va)$fatal(1,"cropped core");
                if(2*vx[core]+vw[core]!=ha || 2*vy[core]+vh[core]!=va)$fatal(1,"uncentered core");
            end
            wait(ready);@(negedge control_clk);commit=1;
            @(negedge control_clk);commit=0;
            wait(active);do @(negedge clk);while(x!=0 || y!=0);
            pixels=0;cycles=0;hcount=0;vcount=0;blanks=0;text_pixels=0;
            text_w=m==0 ? 1920 : (ha/128)*128;
            text_h=m==0 ? 1080 : (va/60)*60;
            do begin
                pixels+=int'(de);hcount+=int'(h==hp);vcount+=int'(v==vp);blanks+=int'(vblank);
                if(de && {red,green,blue}==16'hf800)text_pixels++;
                if(opaque && text_i.fetch_address>=7680)$fatal(1,"text address outside dense plane");
                cycles++;@(negedge clk);
            end while(x!=0 || y!=0);
            if(pixels!=ha*va || cycles!=ht*vt || hcount!=hs*vt || vcount!=vs*ht || blanks!=1)$fatal(1,"timing mode %0d",m);
            if(text_pixels!=text_w*text_h)$fatal(1,"compact text clipped mode%0d got%0d expected%0d",m,text_pixels,text_w*text_h);
            $display("PASS display %0d: %0dx%0d complete image and 128x60 menu",m,ha,va);
        end
        $finish;
    end
    initial begin #700000000;$fatal(1,"display output timeout");end
endmodule
