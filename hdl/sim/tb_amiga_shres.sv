// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Actual two-plane ECS SuperHiRes DMA and Denise pixel stream through the
// production normalizer. Independent four-colour corner/odd-even patterns
// verify every pixel of the1280x512 progressive weave.
module tb_amiga_shres;
 reg clk=0,reset=1;always #17.62 clk=~clk;
 reg native_enable=1,bpl_enable=0;integer phase=0;
 always @(negedge clk)begin phase=phase+1;native_enable=reset || !$test$plusargs("PAUSES") || phase%97<93;end
 wire native_clk=clk && native_enable;
 reg consumed_tick=0;always @(posedge clk)consumed_tick<=!reset && native_enable;
 wire en7,en7n,c1,c3,cck;wire[9:0]eclk;
 wire[8:0]hp,dh0,dh1;wire[10:0]vp,dv0,dv1;wire hs,vs,hb,vb,sof,field_short,lace;
 wire[23:0]rgb;reg[7:0]reg_address=0;reg[15:0]reg_data=0;
 wire bp_dma;wire[7:0]bp_reg;wire[20:1]bp_addr;
 integer width=1280,rows=256,stride=1280,height=512,hstart=129,vstart=44,ddfstart=62,ddfstop=214;
 reg[1:0]capture_mode=3;reg weave=1;
 function automatic[3:0] pattern(input integer x,y);
  begin
   pattern=(x^(x>>2)^(x>>5)^(y*3)^(y>>4))&3;
   if(y==0 && x<16)pattern=(x<8)?4'hf:4'h2;
   if(y==0 && x>=width-16)pattern=(x<width-8)?4'h6:4'hb;
   if(y==height-1 && x<16)pattern=(x<8)?4'h9:4'h4;
   if(y==height-1 && x>=width-16)pattern=(x<width-8)?4'hc:4'h7;
   pattern=pattern&3;
  end
 endfunction
 function automatic[15:0] dma_pattern(input[20:1]address);
  integer byte_address,plane,offset,row,word_x,py,b;reg[3:0]p;
  begin
   byte_address={address,1'b0};plane=byte_address/131072;offset=byte_address%131072;
   row=(offset/((width+15)/16*2))%(rows*2);word_x=(offset%((width+15)/16*2))/2;
   py=weave ? 2*(row%rows)+(row>=rows):2*(row%rows);
   dma_pattern=0;
   for(b=0;b<16;b=b+1)begin p=pattern(word_x*16+b,py);dma_pattern[15-b]=p[plane];end
  end
 endfunction
 wire[7:0]denise_reg=bp_dma ? bp_reg:reg_address;
 reg[15:0]memory[0:131071];
 wire[15:0]denise_data=bp_dma ? memory[bp_addr[17:1]]:reg_data;
 amiga_clk ac(.clk_28(native_clk),.reset_n(!reset),.clk7_en(en7),.clk7n_en(en7n),.c1(c1),.c3(c3),.cck(cck),.eclk(eclk));
 agnus_beamcounter bc(.clk(native_clk),.clk7_en(en7),.reset(reset),.cck(cck),.ntsc(1'b0),.aga(1'b0),.ecs(1'b1),.a1k(1'b0),
  .data_in(reg_data),.reg_address_in(reg_address),.hpos(hp),.vpos(vp),._hsync(hs),._vsync(vs),.hblank(hb),.vblank(vb),.eof(sof),.field1(field_short),.lace(lace));
 agnus_bitplanedma bp(.clk(native_clk),.clk7_en(en7),.reset(reset),.harddis(1'b0),.aga(1'b0),.ecs(1'b1),.a1k(1'b0),
  .sof(sof),.dmaena(bpl_enable),.vpos(vp),.hpos(hp),.reg_address_in(reg_address),.data_in(reg_data),.dma(bp_dma),.reg_address_out(bp_reg),.address_out(bp_addr),
  .diw_hstart(dh0),.diw_hstop(dh1),.diw_vstart(dv0),.diw_vstop(dv1));
 denise de(.clk(native_clk),.clk7_en(en7),.c1(c1),.c3(c3),.cck(cck),.reset(reset),.strhor(hp==11),
  .bitplane_dma(bp_dma),.bitplane_fetch_phase(1'b0),.bitplane_fetch_phase_valid(1'b0),.bitplane_fetch_unit_start(1'b0),
  .reg_address_in(denise_reg),.data_in(denise_data),.chip48(48'd0),.blank(!hs|vb),.red(rgb[23:16]),.green(rgb[15:8]),.blue(rgb[7:0]),.a1k(1'b0),.ecs(1'b1),.aga(1'b0));
 wire[11:0]x,y;wire[15:0]pixel;wire valid,fs,frame_alt,frame_weave;wire[1:0]frame_profile;wire[31:0]malformed;
 multitini_amiga_video_normalizer n(.clk(clk),.reset(reset),.ready(!reset),.chip_tick(consumed_tick),.field_short(field_short),.native_lace(lace),
  .capture_mode(capture_mode),.diw_hstart(dh0),.diw_vstart(dv0),.hsync_n(hs),.vsync_n(vs),.ntsc(1'b0),.hblank(hb),.vblank(vb),.rgb(rgb),
  .scan_x(x),.scan_y(y),.rgb565(pixel),.frame_start(fs),.valid(valid),.frame_alt(frame_alt),.frame_weave(frame_weave),.frame_profile(frame_profile),.malformed_frames(malformed));
 function automatic[15:0] expected(input integer px,py);
  reg[3:0]col;reg[7:0]r,g,b;
  begin
   col=pattern(px,weave ? py:(py&~1));
   r=col*17;g=((col*3)&15)*17;b=((col*7)&15)*17;
   expected=px>=width ? 16'd0:{r[7:3],g[7:2],b[7:3]};
  end
 endfunction
 integer count=0,starts=0,ex,ey,stream_row;reg monitoring=0;
 always @(negedge clk)begin
  if(fs)begin
   if(monitoring && count!=stride*height)$fatal(1,"New frame truncated native resolution output at%0d",count);
   monitoring=1;count=0;starts=starts+1;
   if(frame_alt!==0 || frame_profile!==2 || frame_weave!==weave)$fatal(1,"Native profile metadata mismatch");
  end
  if(valid && monitoring)begin
   ex=count%stride;stream_row=count/stride;
   ey=weave ? 2*(stream_row%rows)+(stream_row>=rows):stream_row;
   if(x!==ex || y!==ey)$fatal(1,"Resolution coordinate mismatch expected%0d,%0d got%0d,%0d",ex,ey,x,y);
   if(pixel!==expected(ex,ey))$fatal(1,"Resolution RGB mismatch at%0d,%0d expected%h got%h DMAaddr%h DIWH%0d V%0d crop%0d/%0d",ex,ey,expected(ex,ey),pixel,bp_addr,dh0,dv0,n.capture_x,n.first_line);
   count=count+1;
   if(count==stride*height)begin
    if(malformed)$fatal(1,"Native complete resolution frame marked malformed%0d",malformed);
    $display("PASS native SHRES normalized mode%0d: %0dx%0d stride%0d,4colours,four independent DMA corners,distinct odd/even fields,DIW H%0d V%0d,all%0d pixels",capture_mode,width,height,stride,dh0,dv0,count);
    $finish;
   end
  end
 end
 task wr(input[7:0]address,input[15:0]value);
  @(negedge clk);reg_address=address;reg_data=value;
  repeat(8)@(negedge native_clk);reg_address=0;repeat(8)@(negedge native_clk);
 endtask
 integer p,r,w,word_address;reg[15:0]color;
 initial begin
  if($test$plusargs("SHIFT"))begin hstart=hstart+4;vstart=vstart-1;ddfstart=ddfstart+2;ddfstop=ddfstop+2;end
  // Begin with inactive sync so the first complete native VSYNC pulse is
  // observable. This conditions fixture startup, not FPGA INIT behavior.
  bc.hpos[8:1]=0;bc.vpos=0;bc._hsync=1;bc._vsync=1;de.window=0;de.window_d=0;
  #1;
  for(p=0;p<16;p=p+1)begin
   color={4'd0,4'(p),4'(p*3),4'(p*7)};
   de.clut0.clut.mem[p]={color,color};
  end
  for(p=0;p<2;p=p+1)for(r=0;r<rows*2;r=r+1)for(w=0;w<(width+15)/16;w=w+1)begin
   word_address=p*65536+r*((width+15)/16)+w;
   memory[word_address]=dma_pattern(20'(word_address));
  end
  repeat(16)@(negedge clk);reset=0;
  wr(8'h47,{8'(vstart),8'(hstart)});wr(8'h48,{8'(vstart+rows),8'(hstart+width/4)});wr(8'hf2,16'h2100);
  wr(8'h80,16'h2244);
  wr(8'h81,0);
  wr(8'h49,16'(ddfstart));wr(8'h4a,16'(ddfstop));
  for(p=0;p<2;p=p+1)begin wr(8'(8'h70+p*2),16'(p*2));wr(8'(8'h71+p*2),0);end
  wr(8'h84,0);wr(8'h85,0);
  wait(vp==vstart-4);@(negedge native_clk);bpl_enable=1;
 end
 initial begin #150000000;$fatal(1,"Native resolution watchdog mode%0d starts%0d count%0d malformed%0d",capture_mode,starts,count,malformed);end
endmodule
