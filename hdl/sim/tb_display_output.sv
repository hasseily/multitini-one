// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_display_output;
    reg clk=0,control_clk=0,resetn=0,control_resetn=0;
    always #5 clk=~clk;
    always #7 control_clk=~control_clk;
    reg [3:0] mode=0;
    wire [11:0] ha,hf,hs,ht,va,vf,vs,vt;
    wire hp,vp;
    wire [3:0] sn[0:3],sd[0:3];
    wire [11:0] vx[0:3],vy[0:3],vw[0:3],vh[0:3];
    multitini_display_geometry #(.MACHINE(0)) c64(.mode(mode),
        .h_active(ha),.h_front(hf),.h_sync(hs),.h_total(ht),
        .v_active(va),.v_front(vf),.v_sync(vs),.v_total(vt),.h_positive(hp),.v_positive(vp),
        .scale_num(sn[0]),.scale_den(sd[0]),.view_x(vx[0]),.view_y(vy[0]),.view_width(vw[0]),.view_height(vh[0]));
    multitini_display_geometry #(.MACHINE(1),.WIDTH(160),.HEIGHT(144)) gbc(.mode(mode),
        .scale_num(sn[1]),.scale_den(sd[1]),.view_x(vx[1]),.view_y(vy[1]),.view_width(vw[1]),.view_height(vh[1]));
    multitini_display_geometry #(.MACHINE(3),.WIDTH(540),.HEIGHT(720)) vectrex(.mode(mode),
        .scale_num(sn[2]),.scale_den(sd[2]),.view_x(vx[2]),.view_y(vy[2]),.view_width(vw[2]),.view_height(vh[2]));
    multitini_display_geometry #(.MACHINE(4),.WIDTH(320),.HEIGHT(240)) genesis(.mode(mode),
        .scale_num(sn[3]),.scale_den(sd[3]),.view_x(vx[3]),.view_y(vy[3]),.view_width(vw[3]),.view_height(vh[3]));
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
    integer pixels,cycles,hcount,vcount,blanks,text_pixels;
    bit expected_ui = 0;
    logic [15:0] standby_color_q = 16'h0842, expected_output_color = 0;
    logic expected_de = 0, expected_hsync = 0, expected_vsync = 0;
    integer standby_x_q = -1, standby_y_q = -1, output_color_x = -1, output_color_y = -1;

    // Fixed golden 5x7 glyphs preserve the 1080p artwork. The reference uses
    // physical positions relative to the screen center, not RTL brand coords.
    function automatic logic [34:0] golden_glyph(input byte character);
        case (character)
            "M": golden_glyph = 35'h4775AC631;
            "U": golden_glyph = 35'h46318C62E;
            "L": golden_glyph = 35'h42108421F;
            "T": golden_glyph = 35'h7C8421084;
            "I": golden_glyph = 35'h7C842109F;
            "N": golden_glyph = 35'h47359C631;
            "O": golden_glyph = 35'h3A318C62E;
            "E": golden_glyph = 35'h7E10F421F;
            "P": golden_glyph = 35'h7A31F4210;
            "R": golden_glyph = 35'h7A31F5251;
            "S": golden_glyph = 35'h3E107043E;
            "F": golden_glyph = 35'h7E10F4210;
            "1": golden_glyph = 35'h11842108E;
            "2": golden_glyph = 35'h3A211111F;
            default: golden_glyph = 0;
        endcase
    endfunction

    function automatic logic [15:0] golden_standby(input integer px,py,width,height);
        integer unit_size,grid,x0,y0,dx,dy,index,column,row,glyph_size,pitch;
        string label_text;
        logic [34:0] glyph;
        logic [15:0] color;
        unit_size = width == 1920 ? 2 : 1;
        grid = 64 * unit_size;
        color = 16'h0842;
        if (px < width && py < height &&
            ((px - (width/2 - 480*unit_size)) % grid == 0 ||
             (py - (height/2 - 270*unit_size)) % grid == 0)) color = 16'h1063;
        if (px >= width/2 - 352*unit_size && px < width/2 + 352*unit_size &&
            py >= height/2 - 120*unit_size && py < height/2 + 120*unit_size) color = 16'h1084;
        if (py >= height/2 - 94*unit_size && py < height/2 - 90*unit_size) begin
            if (px >= width/2 - 288*unit_size && px < width/2) color = 16'h2DFF;
            if (px >= width/2 && px < width/2 + 288*unit_size) color = 16'hF15F;
        end
        for (integer line_index=0;line_index<3;line_index++) begin
            case (line_index)
                0: begin
                    label_text="MULTITINI";glyph_size=8*unit_size;pitch=64*unit_size;
                    x0=width==1920 ? width/2-576 : width/2-276;
                    y0=width==1920 ? height/2-132 : height/2-75;
                end
                1: begin
                    label_text="ONE";glyph_size=4*unit_size;pitch=32*unit_size;
                    x0=width==1920 ? width/2-96 : width/2-42;
                    y0=width==1920 ? height/2+28 : height/2+5;
                end
                default: begin
                    label_text="PRESS F12 FOR MENU";glyph_size=2*unit_size;pitch=16*unit_size;
                    x0=width==1920 ? width/2-288 : width/2-141;
                    y0=width==1920 ? height/2+140 : height/2+61;
                end
            endcase
            dx=px-x0;dy=py-y0;
            if (dx >= 0 && dx < label_text.len()*pitch && dy >= 0 && dy < 7*glyph_size) begin
                index=dx/pitch;column=(dx%pitch)/glyph_size;row=dy/glyph_size;
                glyph=golden_glyph(label_text[index]);
                if (column < 5 && glyph[34-row*5-column]) begin
                    case(line_index)
                        0: color=index < 5 ? 16'h2DFF : 16'hF15F;
                        1: color=16'hFFFF;
                        default: color=16'hBDF7;
                    endcase
                end
            end
        end
        return color;
    endfunction

    // Standby has its existing extra color register; DE/sync and UI keep the
    // physical output register. Model both latencies independently of the DUT.
    always @(posedge clk) begin
        if (!resetn) begin
            standby_color_q=16'h0842;expected_output_color=0;
            expected_de=0;expected_hsync=0;expected_vsync=0;
            standby_x_q=-1;standby_y_q=-1;output_color_x=-1;output_color_y=-1;
        end else begin
            expected_de=x<ha && y<va;
            expected_hsync=(x>=ha+hf && x<ha+hf+hs) ^ !hp;
            expected_vsync=(y>=va+vf && y<va+vf+vs) ^ !vp;
            expected_output_color=expected_de ? (expected_ui ? 16'hF800 : standby_color_q) : 16'd0;
            output_color_x=standby_x_q;output_color_y=standby_y_q;
            // Compact source artwork advances one column to compensate its
            // extra color register. The physical output is then centered.
            standby_color_q=golden_standby(int'(x)+(ha==1920 ? 0:1),int'(y),int'(ha),int'(va));
            standby_x_q=int'(x);standby_y_q=int'(y);
        end
    end

    task automatic check_frame(input integer m,input bit menu_visible);
        integer panel_min_x,panel_max_x,panel_min_y,panel_max_y,panel_pixels,motif_pixels;
        integer one_pixels,hint_pixels,unit_size;
        integer physical_x,physical_y,first_columns;
        bit previous_de;
        integer main_min_x,main_max_x,main_min_y,one_min_x,one_max_x,one_max_y,hint_min_x,hint_max_x,hint_max_y;
        pixels=0;cycles=0;hcount=0;vcount=0;blanks=0;text_pixels=0;
        panel_min_x=4096;panel_max_x=-1;panel_min_y=4096;panel_max_y=-1;
        panel_pixels=0;motif_pixels=0;one_pixels=0;hint_pixels=0;
        main_min_x=4096;main_max_x=-1;main_min_y=4096;
        one_min_x=4096;one_max_x=-1;one_max_y=-1;hint_min_x=4096;hint_max_x=-1;hint_max_y=-1;
        unit_size=m==0 ? 2 : 1;
        physical_x=-1;physical_y=-1;previous_de=0;first_columns=0;
        do begin
            if (de !== expected_de || h !== expected_hsync || v !== expected_vsync)
                $fatal(1,"registered timing mismatch mode%0d menu%0d scan(%0d,%0d)",m,menu_visible,x,y);
            if ({red,green,blue} !== expected_output_color)
                $fatal(1,"output pixel mismatch mode%0d menu%0d color(%0d,%0d): expected%04h got%04h",
                       m,menu_visible,output_color_x,output_color_y,expected_output_color,{red,green,blue});
            pixels+=int'(de);hcount+=int'(h==hp);vcount+=int'(v==vp);blanks+=int'(vblank);
            if (de && {red,green,blue}==16'hf800) text_pixels++;
            if (opaque && text_i.fetch_address>=7680) $fatal(1,"text address outside dense plane");
            // Count output DE pulses independently of scan/source coordinates.
            // A registered standby color can lag those coordinates by a pixel.
            if(de)begin
                if(!previous_de)begin physical_x=0;physical_y++;end
                else physical_x++;
                if(physical_x>=ha || physical_y>=va)$fatal(1,"physical DE raster dimensions");
                if(!menu_visible && physical_x==0)begin
                    if({red,green,blue}!=16'h0842)$fatal(1,"standby first-column color latency");
                    first_columns++;
                end
            end else if(previous_de && physical_x!=ha-1)$fatal(1,"partial physical DE line");
            previous_de=de;
            if (!menu_visible && de) begin
                case ({red,green,blue})
                    16'h1084: begin
                        panel_pixels++;
                        if (physical_x<panel_min_x) panel_min_x=physical_x;
                        if (physical_x>panel_max_x) panel_max_x=physical_x;
                        if (physical_y<panel_min_y) panel_min_y=physical_y;
                        if (physical_y>panel_max_y) panel_max_y=physical_y;
                    end
                    16'hFFFF: begin
                        one_pixels++;
                        if(physical_x<one_min_x)one_min_x=physical_x;
                        if(physical_x>one_max_x)one_max_x=physical_x;
                        if(physical_y>one_max_y)one_max_y=physical_y;
                    end
                    16'hBDF7: begin
                        hint_pixels++;
                        if(physical_x<hint_min_x)hint_min_x=physical_x;
                        if(physical_x>hint_max_x)hint_max_x=physical_x;
                        if(physical_y>hint_max_y)hint_max_y=physical_y;
                    end
                    default: ;
                endcase
                if (physical_y>=int'(va)/2-94*unit_size && physical_y<int'(va)/2-90*unit_size &&
                    ({red,green,blue}==16'h2DFF || {red,green,blue}==16'hF15F)) motif_pixels++;
                if(physical_y>int'(va)/2-90*unit_size &&
                   ({red,green,blue}==16'h2DFF || {red,green,blue}==16'hF15F))begin
                    if(physical_x<main_min_x)main_min_x=physical_x;
                    if(physical_x>main_max_x)main_max_x=physical_x;
                    if(physical_y<main_min_y)main_min_y=physical_y;
                end
            end
            cycles++;@(negedge clk);
        end while(x!=0 || y!=0);
        if(pixels!=ha*va || cycles!=ht*vt || hcount!=hs*vt || vcount!=vs*ht || blanks!=1)
            $fatal(1,"timing mode%0d menu%0d",m,menu_visible);
        if (menu_visible) begin
            if(text_pixels!=ha*va) $fatal(1,"menu background must cover every physical pixel mode%0d got%0d expected%0d",m,text_pixels,ha*va);
        end else begin
            // Preserve the legacy 1080p physical raster, including its original
            // one-column color lag. Compact modes compensate that lag.
            if (panel_min_x!=int'(ha)/2-352*unit_size+(m==0 ? 1:0) || panel_max_x!=int'(ha)/2+352*unit_size-1+(m==0 ? 1:0) ||
                panel_min_y!=int'(va)/2-120*unit_size || panel_max_y!=int'(va)/2+120*unit_size-1)
                $fatal(1,"standby panel not centered mode%0d bounds%0d,%0d..%0d,%0d",m,panel_min_x,panel_min_y,panel_max_x,panel_max_y);
            if (motif_pixels!=576*4*unit_size*unit_size || panel_pixels==0 || one_pixels==0 || hint_pixels==0)
                $fatal(1,"standby motif/glyph coverage mode%0d",m);
            if(m!=0 && (main_min_x+main_max_x!=int'(ha)-1 || one_min_x+one_max_x!=int'(ha)-1 ||
                        hint_min_x+hint_max_x!=int'(ha)-1 || main_min_y+hint_max_y!=int'(va)-1))
                $fatal(1,"visible compact lettering not centered mode%0d MAIN x%0d..%0d y%0d ONE x%0d..%0d y%0d hint x%0d..%0d",
                    m,main_min_x,main_max_x,main_min_y,one_min_x,one_max_x,one_max_y,hint_min_x,hint_max_x);
            if(first_columns!=va)$fatal(1,"physical first-column coverage");
            $display("PASS standby %0d: centered panel, motif and visible title glyphs; %0d active pixels",m,pixels);
        end
    endtask
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
            @(negedge clk);resetn=0;mode=4'(m);expected_ui=0;
            repeat(32) @(negedge clk);resetn=1;
            if(ha!=expected_w[m] || va!=expected_h[m] || ht!=expected_ht[m] || vt!=expected_vt[m])$fatal(1,"mode geometry");
            for(integer core=0;core<4;core=core+1)begin
                if(vw[core]==0 || vh[core]==0 || vx[core]+vw[core]>ha || vy[core]+vh[core]>va)$fatal(1,"cropped core");
                if(2*vx[core]+vw[core]!=ha || 2*vy[core]+vh[core]!=va)$fatal(1,"uncentered core");
            end
            if(sd[3]!=1 || sn[3]!=((m==0 || m==5) ? 4 : 3) ||
               vw[3]!=320*sn[3] || vh[3]!=240*sn[3])$fatal(1,"Genesis fixed fit scale");
            if(active) $fatal(1,"text visibility survived pixel reset");
            do @(negedge clk);while(x!=0 || y!=0);
            check_frame(m,0);
            wait(ready);@(negedge control_clk);commit=1;
            @(negedge control_clk);commit=0;
            wait(active);expected_ui=1;do @(negedge clk);while(x!=0 || y!=0);
            check_frame(m,1);
            $display("PASS display %0d: %0dx%0d complete image and 128x60 menu",m,ha,va);
        end
        $finish;
    end
    initial begin #700000000;$fatal(1,"display output timeout");end
endmodule
