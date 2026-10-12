// SPDX-License-Identifier: GPL-3.0-only
// Physical output coordinates come only from DE pulse counts. The fixed 5x7
// golden glyphs preserve legacy 1080p and independently check compact ink.
`timescale 1ns/1ps
module tb_physical_splash #(parameter integer COMPACT_OFFSET=0);
 import multitini_display_pkg::*;
 reg clk=0,control_clk=0,resetn=0,ps_resetn=0;
 realtime half=7.692;
 always #(half) clk=~clk;
 always #3.75 control_clk=~control_clk;
 reg [2:0] shared_release=0;
 always @(posedge clk or negedge ps_resetn)
  if(!ps_resetn)shared_release<=0;else shared_release<={shared_release[1:0],1'b1};
 reg [3:0] mode=1;
 video_mode_t timing;
 always_comb timing=video_mode(mode);
 wire [15:0] rgb;wire de,hs,vs;
 multitini_video_test #(.DYNAMIC_DISPLAY(1),.SIGNED_VIEWPORT(1),.ENABLE_EFFECTS(1),
  .SHARED_PIXEL_RESET(1),.SHARED_PIXEL_LIVENESS(1)) dut(
  .display_h_active(timing.width),.display_h_front(timing.h_front),.display_h_sync(timing.h_sync),.display_h_total(timing.h_total),
  .display_v_active(timing.height),.display_v_front(timing.v_front),.display_v_sync(timing.v_sync),.display_v_total(timing.v_total),
  .display_h_positive(timing.h_positive),.display_v_positive(timing.v_positive),
  .display_scale_num(4'd4),.display_scale_den(4'd1),.pixel_clk(clk),.resetn(resetn),
  .enable(1'b1),.machine_enable(1'b0),.control_clk(control_clk),.control_resetn(ps_resetn),
  .shared_pixel_resetn(shared_release[2]),.shared_pixel_running(ps_resetn),
  .effect_write(1'b0),.effect_address(10'd0),.effect_data(32'd0),.effect_commit(1'b0),.effect_commit_sequence(32'd0),
  .viewport_x(12'd0),.viewport_y(12'd0),.viewport_width(12'd0),.viewport_height(12'd0),
  .machine_rgb565(16'd0),.machine_neighbor_rgb565(16'd0),.machine_opposite_rgb565(16'd0),
  .machine_dot_valid(1'b0),.machine_neighbor_weight(8'd0),.border_opaque(1'b0),.border_rgb565(16'd0),
  .ui_opaque(1'b0),.ui_rgb565(16'd0),.dvi_red(rgb[15:11]),.dvi_grn(rgb[10:5]),.dvi_blu(rgb[4:0]),
  .dvi_de(de),.dvi_hsync(hs),.dvi_vsync(vs));
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


 reg armed=0,done=0,previous_de=0;
 integer px=-1,py=-1,pixels=0,blanks=0,boundaries=0,offset=COMPACT_OFFSET;
 integer ml,mr,mt,mb,ol,orr,ot,ob,hl,hr,htop,hb,pl,pr,pt,pb;
 integer row_min[0:2][0:1079],row_max[0:2][0:1079];
 integer source_x,source_y,line_kind,u;
 reg [15:0] expected_rgb;
 always @(negedge clk)begin
  #0.001;
  if(armed)begin
   if(de)begin
    if(!previous_de)begin px=0;py=py+1;end else px=px+1;
    if(px>=timing.width || py>=timing.height)$fatal(1,"DE-derived raster dimensions");
    source_x=px==0 ? timing.h_total-1 : px-(mode==0 ? 1:offset);
    source_y=px==0 ? (py==0 ? timing.v_total-1:py-1):py;
    expected_rgb=golden_standby(source_x,source_y,timing.width,timing.height);
    if(rgb!==expected_rgb)$fatal(1,"physical raster oracle mode=%0d xy=%0d,%0d expected=%h got=%h",mode,px,py,expected_rgb,rgb);
    if(px==0)begin
     if(rgb!=16'h0842)$fatal(1,"first DE column must retain the registered blank-source color");
     boundaries=boundaries+1;
    end
    pixels=pixels+1;u=mode==0 ? 2:1;line_kind=-1;
    if(rgb==16'h1084)begin
     if(px<pl)pl=px;if(px>pr)pr=px;if(py<pt)pt=py;if(py>pb)pb=py;
    end
    if((rgb==16'h2dff || rgb==16'hf15f) && py>timing.height/2-90*u)begin
     line_kind=0;if(px<ml)ml=px;if(px>mr)mr=px;if(py<mt)mt=py;if(py>mb)mb=py;
    end
    if(rgb==16'hffff)begin
     line_kind=1;if(px<ol)ol=px;if(px>orr)orr=px;if(py<ot)ot=py;if(py>ob)ob=py;
    end
    if(rgb==16'hbdf7)begin
     line_kind=2;if(px<hl)hl=px;if(px>hr)hr=px;if(py<htop)htop=py;if(py>hb)hb=py;
    end
    if(line_kind>=0)begin
     if(px<row_min[line_kind][py])row_min[line_kind][py]=px;
     if(px>row_max[line_kind][py])row_max[line_kind][py]=px;
    end
    if(py==timing.height-1 && px==timing.width-1)begin done=1;armed=0;end
   end else begin
    if(rgb!=0)$fatal(1,"RGB outside DE");blanks=blanks+1;
    if(previous_de && px!=timing.width-1)$fatal(1,"partial DE line");
   end
   previous_de=de;
  end
 end
 task run_mode(input integer m);
  begin
   @(posedge clk);#0.001;resetn=0;armed=0;mode=4'(m);
   case(m)1:half=7.692;2:half=7.407;3:half=4.630;4:half=5.848;5:half=4.202;default:half=3.367;endcase
   repeat(32)@(negedge clk);resetn=1;
   repeat(32)@(negedge clk);
   wait(vs===timing.v_positive);wait(vs!==timing.v_positive);
   @(posedge clk);#0.001;
   px=-1;py=-1;previous_de=0;done=0;pixels=0;blanks=0;boundaries=0;
   ml=4096;mr=-1;mt=4096;mb=-1;ol=4096;orr=-1;ot=4096;ob=-1;
   hl=4096;hr=-1;htop=4096;hb=-1;pl=4096;pr=-1;pt=4096;pb=-1;
   for(integer k=0;k<3;k++)for(integer y=0;y<1080;y++)begin row_min[k][y]=4096;row_max[k][y]=-1;end
   armed=1;wait(done);
   if(pixels!=timing.width*timing.height || boundaries!=timing.height)$fatal(1,"physical raster coverage");
   if(m!=0 && (ml+mr!=timing.width-1+2*COMPACT_OFFSET || ol+orr!=timing.width-1+2*COMPACT_OFFSET ||
    hl+hr!=timing.width-1+2*COMPACT_OFFSET || pl+pr!=timing.width-1+2*COMPACT_OFFSET ||
    mt+hb!=timing.height-1 || pt+pb!=timing.height-1))
    $fatal(1,"physical compact ink/panel centering");
   if(m==0 && (ml!=385 || mr!=1488 || mt!=408 || mb!=519 || ol!=865 || orr!=1032 ||
    ot!=568 || ob!=623 || hl!=673 || hr!=1236 || htop!=680 || hb!=707 ||
    pl!=257 || pr!=1664 || pt!=300 || pb!=779))
    $fatal(1,"legacy 1080p physical pixels changed");
   $display("PHYSICAL mode=%0d W=%0d H=%0d MAIN=%0d,%0d,%0d,%0d ONE=%0d,%0d,%0d,%0d HINT=%0d,%0d,%0d,%0d PANEL=%0d,%0d,%0d,%0d pixels=%0d boundaries=%0d blanks=%0d",m,timing.width,timing.height,ml,mr,mt,mb,ol,orr,ot,ob,hl,hr,htop,hb,pl,pr,pt,pb,pixels,boundaries,blanks);
   for(integer k=0;k<3;k++)for(integer y=0;y<1080;y++)if(row_max[k][y]>=0)
    $display("INKROW mode=%0d kind=%0d y=%0d min=%0d max=%0d",m,k,y,row_min[k][y],row_max[k][y]);
  end
 endtask
 initial begin
  repeat(8)@(negedge control_clk);ps_resetn=1;
  run_mode(0);run_mode(1);run_mode(2);run_mode(3);run_mode(4);run_mode(5);
  $display("PASS physical splash: six full DE-derived rasters, ENABLE_EFFECTS=1, independent physical pixel oracle, per-line ink bounds, blanking and first-column latency");$finish;
 end
 initial begin #250000000;$fatal(1,"physical splash timeout");end
endmodule


