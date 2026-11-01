// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Original native ECS SuperHiRes probe. The capture origin is found by
// matching an independently built nonperiodic2plane bitmap, then every raw
// native-clock pixel through the rest of the256-row field is checked.
module tb_amiga_shres_probe;
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
 function automatic[15:0] expected(input integer px,py);
  reg[3:0]col;reg[7:0]r,g,b;
  begin
   col=pattern(px,weave ? py:(py&~1));
   r=col*17;g=((col*3)&15)*17;b=((col*7)&15)*17;
   expected=px>=width ? 16'd0:{r[7:3],g[7:2],b[7:3]};
  end
 endfunction
 integer debug_count=0;
 always @(posedge native_clk)if(en7 && bp_dma && vp==44 && debug_count<20)begin
  debug_count=debug_count+1;
  $display("DMA addr%h reg%h data%h bpu%h raw%h plf%h",bp_addr,bp_reg,denise_data,de.l_bpu,de.bpldata_out,de.plfdata);
 end
 reg hs_q=1,vs_q=1;
 integer raw_x=0,raw_y=0,origin=-1,candidate,errors,matching_origins,verified=0,best_error=9999,best_origin=-1;
 reg[15:0] row_zero[0:1815];
 reg[15:0] actual;
 always @(posedge clk)if(!reset && consumed_tick)begin
  hs_q<=hs;vs_q<=vs;
  if(vs_q && !vs)raw_y<=0;
  else if(hs_q && !hs)raw_y<=raw_y+1;
  if(hs_q && !hs)begin
   raw_x<=0;
   if(raw_y==41 && origin<0)begin
    matching_origins=0;
    for(candidate=390;candidate<=440;candidate=candidate+1)begin
     errors=0;
     for(integer px=0;px<1280;px=px+1)
      if(row_zero[candidate+px]!==expected(px,0))errors=errors+1;
     if(errors<best_error)begin best_error=errors;best_origin=candidate;end
     if(errors==0)begin origin=candidate;matching_origins=matching_origins+1;end
    end
    if(matching_origins!=1)begin
     $display("BEST origin%0d errors%0d RGB412..427",best_origin,best_error);
     for(integer dp=400;dp<448;dp=dp+1)$display("RAW %0d %h EXPECT %h",dp,row_zero[dp],expected(dp-412,0));
     $fatal(1,"Native SHRES origin not uniquely identified:matching_origins%0d",matching_origins);end
    verified=1280;
    $display("CALIBRATED native ECS SHRES first pixel:HSYNC-relative native tick%0d,DIWH%0d",origin,dh0);
   end
  end else begin
   raw_x<=raw_x+1;
   actual={rgb[23:19],rgb[15:10],rgb[7:3]};
   if(raw_y==41 && raw_x<1816)row_zero[raw_x]=actual;
   if(origin>=0 && raw_y>41 && raw_y<297 && raw_x>=origin && raw_x<origin+1280)begin
    if(actual!==expected(raw_x-origin,2*(raw_y-41)))
     $fatal(1,"Native SHRES pixel mismatch at%0d,%0d got%h expected%h",raw_x-origin,raw_y-41,actual,expected(raw_x-origin,2*(raw_y-41)));
    verified=verified+1;
    if(verified==1280*256)begin
     $display("PASS native ECS SHRES:1280x256field,2planes,4colours,%0dpixels,every consumed28MHz tick,origin%0d",verified,origin);
     $finish;
    end
   end
  end
 end
 task wr(input[7:0]address,input[15:0]value);
  @(negedge clk);reg_address=address;reg_data=value;
  repeat(8)@(negedge native_clk);reg_address=0;repeat(8)@(negedge native_clk);
 endtask
 integer p,r,w,word_address;reg[15:0]color;
 initial begin

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
 initial begin #150000000;$fatal(1,"Native SHRES watchdog origin%0d verified%0d",origin,verified);end
endmodule
