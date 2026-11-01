// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Native Agnus DMA, Denise data latches/shifters and palette drive the
// production normalizer. The bitmap's four corner words are independent of
// capture constants. No CPU, vendor ROM or board state is involved.
module tb_amiga_native_geometry;
 reg clk=0,reset=1;always #17.62 clk=~clk;
 reg native_enable=1;
 integer pause_counter=0;
 always @(negedge clk)begin
  pause_counter=pause_counter+1;
  native_enable=reset || !$test$plusargs("PAUSES") || (pause_counter%97)<93;
 end
 wire native_clk=clk && native_enable;
 reg consumed_tick=0;
 always @(posedge clk)consumed_tick<=!reset && native_enable;
 wire en7,en7n,c1,c3,cck;wire [9:0] eclk;
 wire [8:0] hp;wire [10:0] vp;wire hs,vs,hb,vb,sof;
 wire [23:0] rgb;wire strhor=hp==11;
 reg [7:0] reg_address=0;reg [15:0] reg_data=0;
 wire bp_dma;wire [7:0] bp_reg;wire [20:1] bp_addr;
 wire [7:0] denise_reg=bp_dma ? bp_reg:reg_address;
 function automatic [15:0] dma_pattern(input[20:1] address);
  integer byte_address,row,word_in_row;
  begin
   byte_address={address,1'b0};row=(byte_address/80)%256;word_in_row=(byte_address%80)/2;
   dma_pattern=16'hffff;
   if(row==0 && word_in_row==0)dma_pattern=16'hf001;
   if(row==0 && word_in_row==39)dma_pattern=16'h800f;
   if(row==255 && word_in_row==0)dma_pattern=16'hc001;
   if(row==255 && word_in_row==39)dma_pattern=16'h8003;
  end
 endfunction
 wire [15:0] denise_data=bp_dma ? dma_pattern(bp_addr):reg_data;
 amiga_clk ac(.clk_28(native_clk),.reset_n(!reset),.clk7_en(en7),.clk7n_en(en7n),.c1(c1),.c3(c3),.cck(cck),.eclk(eclk));
 agnus_beamcounter bc(.clk(native_clk),.clk7_en(en7),.reset(reset),.cck(cck),.ntsc(1'b0),.aga(1'b0),.ecs(1'b1),.a1k(1'b0),
  .data_in(reg_data),.reg_address_in(reg_address),.hpos(hp),.vpos(vp),._hsync(hs),._vsync(vs),.hblank(hb),.vblank(vb),.eof(sof));
 agnus_bitplanedma bp(.clk(native_clk),.clk7_en(en7),.reset(reset),.harddis(1'b0),.aga(1'b0),.ecs(1'b1),.a1k(1'b0),
  .sof(sof),.dmaena(1'b1),.vpos(vp),.hpos(hp),.reg_address_in(reg_address),.data_in(reg_data),.dma(bp_dma),.reg_address_out(bp_reg),.address_out(bp_addr));
 denise de(.clk(native_clk),.clk7_en(en7),.c1(c1),.c3(c3),.cck(cck),.reset(reset),.strhor(strhor),
  .bitplane_dma(bp_dma),.bitplane_fetch_phase(1'b0),.bitplane_fetch_phase_valid(1'b0),.bitplane_fetch_unit_start(1'b0),
  .reg_address_in(denise_reg),.data_in(denise_data),.chip48(48'd0),.blank(!hs|vb),.red(rgb[23:16]),.green(rgb[15:8]),.blue(rgb[7:0]),.a1k(1'b0),.ecs(1'b1),.aga(1'b0));
 wire [11:0] x,y;wire [15:0] p;wire valid,fs;wire[31:0] malformed;
 multitini_amiga_video_normalizer n(.clk(clk),.reset(reset),.ready(!reset),.chip_tick(consumed_tick),.field_short(!bc.long_frame && bc.lace),.hsync_n(hs),.vsync_n(vs),.ntsc(1'b0),
  .capture_mode(2'd0),.native_lace(bc.lace),.diw_hstart(9'd129),.diw_vstart(11'd44),
  .hblank(hb),.vblank(vb),.rgb(rgb),.scan_x(x),.scan_y(y),.rgb565(p),.frame_start(fs),.valid(valid),.malformed_frames(malformed));
 reg synced=0;
 reg hs_q=1;integer lines=0,first_x=-1,last_x=-1,line_pixels=0;
 integer first_y=-1,last_y=-1,active_lines=0,measured_fields=0;
 integer completed_canvases=0,output_count=0,expected_y;
 reg waiting_for_first_field=1;
 reg [15:0] left_marker=0,right_marker=0;
 reg [23:0] first_color=0,last_color=0;
 always @(posedge clk)if(!reset && consumed_tick)begin
  hs_q<=hs;
  if(n.new_field)begin synced=1;active_lines=0;first_y=-1;last_y=-1;end
  if(hs_q && !hs)begin
   if(line_pixels!=0 && synced)begin
    if(first_x!=206 || last_x!=845 || (active_lines>0 && active_lines<255 && line_pixels!=640))$fatal(1,"Native horizontal geometry changed x%0d..%0d pixels%0d",first_x,last_x,line_pixels);
    active_lines=active_lines+1;
    if(active_lines==1)begin
     first_y=n.native_y;
     $display("Top marker words %h/%h",left_marker,right_marker);
     if(left_marker!=16'hf001 || right_marker!=16'h800f)$fatal(1,"Native top marker words shifted");
     $display("Native top field%0d: Agnus v%0d normalizer native_y%0d RGB x%0d..%0d colors%h/%h",bc.long_frame,vp-1,n.native_y,first_x,last_x,first_color,last_color);

    end
    last_y=n.native_y;
    if(active_lines==256)begin
     $display("Native bottom field%0d: Agnus v%0d normalizer native_y%0d RGB x%0d..%0d colors%h/%h",bc.long_frame,vp-1,n.native_y,first_x,last_x,first_color,last_color);

     if(first_y!=(bc.long_frame ? 41:42) || last_y!=(bc.long_frame ? 296:297))$fatal(1,"Unexpected vertical native origin %0d..%0d",first_y,last_y);
     $display("Bottom marker words %h/%h",left_marker,right_marker);
     if(left_marker!=16'hc001 || right_marker!=16'h8003)$fatal(1,"Native bottom marker words shifted");
     measured_fields=measured_fields+1;
     $display("PASS native field long=%b, DMA four-corner markers, active x206..845/y%0d..%0d",bc.long_frame,first_y,last_y);

    end
   end
   lines=lines+1;first_x=-1;last_x=-1;line_pixels=0;left_marker=0;right_marker=0;

  end
  if(n.half && !hb && !vb && n.native_x>=206 && n.native_x<846)begin
   if(n.native_x<222)left_marker={left_marker[14:0],rgb==24'hffffff};
   if(n.native_x>=830)right_marker={right_marker[14:0],rgb==24'hffffff};
  end
  if(n.half && !hb && !vb && rgb!=0)begin
   if(first_x<0)begin first_x=n.native_x;first_color=rgb;end
   last_x=n.native_x;last_color=rgb;line_pixels=line_pixels+1;
  end
 end
 function automatic [15:0] expected_pixel(input integer px,py);
  integer row,word_in_row;reg [15:0] bits;
  begin
   row=py/2;word_in_row=px/16;bits=16'hffff;
   if(row==0 && word_in_row==0)bits=16'hf001;
   if(row==0 && word_in_row==39)bits=16'h800f;
   if(row==255 && word_in_row==0)bits=16'hc001;
   if(row==255 && word_in_row==39)bits=16'h8003;
   expected_pixel=bits[15-(px%16)] ? 16'hffff:16'h0000;
  end
 endfunction
 always @(negedge clk)begin
  if(fs)begin
   if(!waiting_for_first_field && output_count!=640*512)$fatal(1,"Native VSYNC truncated a canvas at%0d pixels",output_count);
   if(waiting_for_first_field)measured_fields=0;
   waiting_for_first_field=0;output_count=0;
  end
  if(valid && !waiting_for_first_field)begin
   expected_y=$test$plusargs("LACE") ? 2*((output_count/640)%256)+((output_count/640)>=256):output_count/640;
   if(x!==output_count%640 || y!==expected_y)$fatal(1,"Normalized coordinate mismatch x%0d y%0d count%0d",x,y,output_count);
   if(p!==expected_pixel(x,y))$fatal(1,"Normalized RGB mismatch at%0d,%0d expected%h got%h",x,y,expected_pixel(x,y),p);
   output_count=output_count+1;
   if(output_count==640*512)begin
    completed_canvases=completed_canvases+1;
    if(measured_fields!=completed_canvases*($test$plusargs("LACE") ? 2:1))$fatal(1,"Canvas completed before native corners were measured");
    $display("PASS native normalized640x512 canvas%0d: every pixel and four DMA corner words",completed_canvases);
    if(!$test$plusargs("LACE") || completed_canvases==1)begin
     if($test$plusargs("PAUSES") && pause_counter<100)$fatal(1,"Native pauses were not exercised");
     $display("PASS actual native geometry: standardPAL640x256 to640x512; fieldshort alignment; four DMA corners intact");$finish;
    end
   end
  end
 end
 task wr(input[7:0] address,input[15:0] value);
  @(negedge clk);reg_address=address;reg_data=value;
  repeat(8)@(negedge native_clk);reg_address=0;
  repeat(8)@(negedge native_clk);
 endtask
 initial begin
  bc.hpos[8:1]=0;bc.vpos=0;
  // Observe the first complete native sync pulse; this is fixture startup
  // conditioning, not a claim about physical configuration INIT values.
  bc._hsync=1;bc._vsync=1;
  de.window=0;de.window_d=0;

  #1;de.clut0.clut.mem[0]=0;de.clut0.clut.mem[1]=32'h0fff0fff;
  de.clut0.clut.mem[2]=32'h0f000f00;de.clut0.clut.mem[3]=32'h00f000f0;
  de.clut0.clut.mem[4]=32'h000f000f;de.clut0.clut.mem[5]=32'h0ff00ff0;
  repeat(16)@(negedge clk);reset=0;
  wr(8'h47,16'h2c81);wr(8'h48,16'h2cc1);
  wr(8'h80,$test$plusargs("LACE") ? 16'h9204:16'h9200);wr(8'h81,0);wr(8'h49,16'h003c);wr(8'h4a,16'h00d4);
  wr(8'h70,0);wr(8'h71,0);wr(8'h84,0);wr(8'h85,0);
 end
 initial begin #100000000;$fatal(1,"probe timeout");end
endmodule
