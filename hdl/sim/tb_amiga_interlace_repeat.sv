// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Original repeated-field diagnostic. Actual Copper lists reload separate
// long/short bitmaps and rewrite DIW during blanking on every field. The
// unchanged normalizer must emit identical complete progressive frames.
module tb_amiga_interlace_repeat;
 reg clk=0,reset=1;always #17.62 clk=~clk;
 reg native_enable=1;integer phase=0;
 always @(negedge clk)begin phase=phase+1;native_enable=reset || !$test$plusargs("PAUSES") || phase%97<93;end
 wire native_clk=clk && native_enable;
 reg consumed_tick=0;always @(posedge clk)consumed_tick<=!reset && native_enable;
 wire en7,en7n,c1,c3,cck;wire[9:0]eclk;
 wire[8:0]hp,dh0,dh1;wire[10:0]vp,dv0,dv1;wire hs,vs,hb,vb,sof,field_short,lace;
 wire[23:0]rgb;reg[7:0]cpu_reg_address=0;reg[15:0]cpu_reg_data=0;
 wire[7:0]reg_address;wire[15:0]reg_data;
 wire cop_req,cop_ack;wire[7:0]cop_reg;wire[20:1]cop_addr;reg cop_enable=0,bpl_enable=0;
 integer copper_diw_writes=0,copper_pointer_writes=0,completed=0;
 integer short_vstart=44,list_cursor;reg field_vstep=0;
 wire bp_dma;wire[7:0]bp_reg;wire[20:1]bp_addr;
 integer width=640,rows=256,stride=640,height=512,hstart=129,vstart=44,ddfstart=60,ddfstop=212;
 reg[1:0]capture_mode=1;reg weave=1;
 function automatic[3:0] pattern(input integer x,y);
  begin
   pattern=(x^(y*3)^(y>>4))&15;
   if(y==0 && x<16)pattern=(x<8)?4'hf:4'h2;
   if(y==0 && x>=width-16)pattern=(x<width-8)?4'h6:4'hb;
   if(y==height-1 && x<16)pattern=(x<8)?4'h9:4'h4;
   if(y==height-1 && x>=width-16)pattern=(x<width-8)?4'hc:4'h7;
  end
 endfunction
 function automatic[15:0] dma_pattern(input[20:1]address);
  integer byte_address,plane,offset,row,word_x,py,b;reg[3:0]p;
  begin
   byte_address={address,1'b0};plane=byte_address/65536;offset=byte_address%65536;
   row=(offset/((width+15)/16*2))%(rows*2);word_x=(offset%((width+15)/16*2))/2;
   py=weave ? 2*(row%rows)+(row>=rows):2*(row%rows);
   dma_pattern=0;
   for(b=0;b<16;b=b+1)begin p=pattern(word_x*16+b,py);dma_pattern[15-b]=p[plane];end
  end
 endfunction
 reg[15:0]memory[0:262143];
 wire[7:0]denise_reg=reg_address;
 wire[15:0]denise_data=reg_data;
 assign cop_ack=cop_enable && cop_req && !bp_dma;
 assign reg_address=bp_dma ? bp_reg:cop_ack ? cop_reg:cpu_reg_address;
 assign reg_data=bp_dma ? memory[bp_addr[18:1]]:cop_ack ? memory[cop_addr[18:1]]:cpu_reg_data;
 agnus_copper copper(.clk(native_clk),.clk7_en(en7),.reset(reset),.ecs(1'b1),
  .reqdma(cop_req),.ackdma(cop_ack),.enadma(!bp_dma),.sof(sof),.blit_busy(1'b0),
  .vpos(vp[7:0]),.hpos(hp),.data_in(reg_data),.reg_address_in(reg_address),
  .reg_address_out(cop_reg),.address_out(cop_addr));
 always @(posedge native_clk)if(en7 && cop_ack)begin
  if(cop_reg==8'h47)copper_diw_writes=copper_diw_writes+1;
  if(cop_reg>=8'h70 && cop_reg<=8'h77)copper_pointer_writes=copper_pointer_writes+1;
 end
 amiga_clk ac(.clk_28(native_clk),.reset_n(!reset),.clk7_en(en7),.clk7n_en(en7n),.c1(c1),.c3(c3),.cck(cck),.eclk(eclk));
 agnus_beamcounter bc(.clk(native_clk),.clk7_en(en7),.reset(reset),.cck(cck),.ntsc(1'b0),.aga(1'b0),.ecs(1'b1),.a1k(1'b0),
  .data_in(reg_data),.reg_address_in(reg_address),.hpos(hp),.vpos(vp),._hsync(hs),._vsync(vs),.hblank(hb),.vblank(vb),.eof(sof),.field1(field_short),.lace(lace));
 agnus_bitplanedma bp(.clk(native_clk),.clk7_en(en7),.reset(reset),.harddis(1'b0),.aga(1'b0),.ecs(1'b1),.a1k(1'b0),
  .sof(sof),.dmaena(bpl_enable),.vpos(vp),.hpos(hp),.reg_address_in(reg_address),.data_in(reg_data),.dma(bp_dma),.reg_address_out(bp_reg),.address_out(bp_addr),
  .diw_hstart(dh0),.diw_hstop(dh1),.diw_vstart(dv0),.diw_vstop(dv1));
 denise de(.clk(native_clk),.clk7_en(en7),.c1(c1),.c3(c3),.cck(cck),.reset(reset),.strhor(hp==11),
  .bitplane_dma(bp_dma),.bitplane_fetch_phase(1'b0),.bitplane_fetch_phase_valid(1'b0),.bitplane_fetch_unit_start(1'b0),
  .reg_address_in(denise_reg),.data_in(denise_data),.chip48(48'd0),.blank(!hs|vb),.red(rgb[23:16]),.green(rgb[15:8]),.blue(rgb[7:0]),.a1k(1'b0),.ecs(1'b1),.aga(1'b0));
 wire[11:0]x,y;wire[15:0]pixel;wire valid,fs,frame_alt,frame_weave;wire[31:0]malformed;
 multitini_amiga_video_normalizer n(.clk(clk),.reset(reset),.ready(!reset),.chip_tick(consumed_tick),.field_short(field_short),.native_lace(lace),
  .capture_mode(capture_mode),.diw_hstart(dh0),.diw_vstart(dv0),.hsync_n(hs),.vsync_n(vs),.ntsc(1'b0),.hblank(hb),.vblank(vb),.rgb(rgb),
  .scan_x(x),.scan_y(y),.rgb565(pixel),.frame_start(fs),.valid(valid),.frame_alt(frame_alt),.frame_weave(frame_weave),.malformed_frames(malformed));
 function automatic[15:0] expected(input integer px,py);
  reg[3:0]col;reg[7:0]r,g,b;
  begin
   col=pattern(px,weave ? py:(py&~1));
   r=col*17;g=((col*3)&15)*17;b=((col*7)&15)*17;
   expected=px>=width ? 16'd0:{r[7:3],g[7:2],b[7:3]};
  end
 endfunction
 integer debug_writes=0;
 always @(posedge native_clk)if(en7 && $test$plusargs("DEBUG") && debug_writes<100)begin
  if(reg_address!=8'hff)begin
   debug_writes=debug_writes+1;
   $display("BUS v%0d h%0d reg%h data%h bp%b cop%b pc%h state%0d ptr%h:%h strobes%b%b",vp,hp,reg_address,reg_data,bp_dma,cop_ack,cop_addr,copper.copper_state,copper.cop1lch,copper.cop1lcl,copper.strobe1,copper.strobe2);
  end
 end
 integer count=0,starts=0,ex,ey,stream_row;reg monitoring=0;
 always @(negedge clk)begin
  if(fs)begin
   if(monitoring && count!=stride*height)$fatal(1,"New frame truncated native resolution output at%0d",count);
   monitoring=1;count=0;starts=starts+1;
   if(frame_alt!==(capture_mode==2) || frame_weave!==weave)$fatal(1,"Native profile metadata mismatch");
  end
  if(valid && monitoring)begin
   ex=count%stride;stream_row=count/stride;
   ey=weave ? 2*(stream_row%rows)+(stream_row>=rows):stream_row;
   if(x!==ex || y!==ey)$fatal(1,"Resolution coordinate mismatch expected%0d,%0d got%0d,%0d",ex,ey,x,y);
   if(pixel!==expected(ex,ey))$fatal(1,"Resolution RGB mismatch at%0d,%0d expected%h got%h DMAaddr%h DIWH%0d V%0d crop%0d/%0d",ex,ey,expected(ex,ey),pixel,bp_addr,dh0,dv0,n.capture_x,n.first_line);
   count=count+1;
   if(count==stride*height)begin
    completed=completed+1;
    if(malformed)$fatal(1,"Native complete resolution frame marked malformed%0d",malformed);
    $display("PAIR %0d native mode%0d %0dx%0d all%0d pixels,DIW H%0d V%0d",completed,capture_mode,width,height,count,dh0,dv0);
    if(completed==4)begin
     if(copper_diw_writes<8 || copper_pointer_writes<64)$fatal(1,"Copper did not reload every field: DIW%0d ptr%0d",copper_diw_writes,copper_pointer_writes);
     $display("PASS repeated native Copper weave:4identical progressive pairs,%0dx%0d,fieldVstep%0d,%0dDIW/%0dptr writes",width,height,field_vstep,copper_diw_writes,copper_pointer_writes);
     $finish;
    end
   end
  end
 end
 task wr(input[7:0]address,input[15:0]value);
  @(negedge clk);cpu_reg_address=address;cpu_reg_data=value;
  repeat(8)@(negedge native_clk);cpu_reg_address=0;repeat(8)@(negedge native_clk);
 endtask
 task move(input[15:0]address,value);
  memory[list_cursor]=address;memory[list_cursor+1]=value;list_cursor=list_cursor+2;
 endtask
 task field_list(input integer base,next_base,vertical,parity);
  integer plane,bitmap;
  begin
   list_cursor=base/2;
   move(16'h0080,16'h0007);move(16'h0082,16'(next_base));
   // Real WAIT executes in vertical blank; no direct testbench DIW update.
   move($test$plusargs("LATE_BLANK") ? 16'h1401:16'h0c01,16'hfffe);
   move(16'h008e,{8'(vertical),8'(hstart)});
   move(16'h0090,{8'(vertical+rows),8'(hstart+width/2)});
   move(16'h01e4,16'h2100);move(16'h0100,16'hc204);
   move(16'h0102,capture_mode==2 ? 16'h0044:16'h0000);
   move(16'h0092,16'(ddfstart));move(16'h0094,16'(ddfstop));
   for(plane=0;plane<4;plane=plane+1)begin
    bitmap=plane*65536+parity*rows*((width+15)/16)*2;
    move(16'(16'h00e0+plane*4),16'(bitmap>>16));
    move(16'(16'h00e2+plane*4),16'(bitmap));
   end
   move(16'h0108,0);move(16'h010a,0);
   move(16'hffff,16'hfffe);
  end
 endtask
 integer p,r,w,word_address;reg[15:0]color;
 initial begin
  if($test$plusargs("WEAVE"))begin capture_mode=1;weave=1;end
  if($test$plusargs("BASE_AUTO"))capture_mode=0;
  if($test$plusargs("MAX"))begin capture_mode=2;weave=1;width=724;rows=283;height=566;stride=768;hstart=93;vstart=29;ddfstart=40;ddfstop=216;end
  if($test$plusargs("SHIFT"))begin
   hstart=hstart+(capture_mode==2 ? -4:4);vstart=vstart-1;
   ddfstart=ddfstart+(capture_mode==2 ? -2:2);ddfstop=ddfstop+(capture_mode==2 ? -2:2);
  end
  field_vstep=$test$plusargs("FIELD_VSTEP");short_vstart=vstart+field_vstep;
  field_list(32'h70000,32'h70100,vstart,0);
  field_list(32'h70100,32'h70000,short_vstart,1);
  // Begin with inactive sync so the first complete native VSYNC pulse is
  // observable. This conditions fixture startup, not FPGA INIT behavior.
  bc.hpos[8:1]=0;bc.vpos=0;bc._hsync=1;bc._vsync=1;de.window=0;de.window_d=0;
  #1;
  for(p=0;p<16;p=p+1)begin
   color={4'd0,4'(p),4'(p*3),4'(p*7)};
   de.clut0.clut.mem[p]={color,color};
  end
  for(p=0;p<4;p=p+1)for(r=0;r<rows*2;r=r+1)for(w=0;w<(width+15)/16;w=w+1)begin
   word_address=p*32768+r*((width+15)/16)+w;
   memory[word_address]=dma_pattern(20'(word_address));
  end
  repeat(16)@(negedge clk);reset=0;
  // Clear the unreset vertical-window latch through its real V0 stop comparator.
  wr(8'h48,0);wr(8'hf2,0);
  wr(8'h47,{8'(vstart),8'(hstart)});wr(8'h48,{8'(vstart+rows),8'(hstart+width/2)});wr(8'hf2,16'h2100);
  wr(8'h80,weave ? 16'hc204:16'hc200);
  wr(8'h81,capture_mode==2 ? ($test$plusargs("SHIFT") ? 16'h88:16'h44):($test$plusargs("SHIFT") ? 16'h44:0));
  wr(8'h49,16'(ddfstart));wr(8'h4a,16'(ddfstop));
  wr(8'h40,16'h0007);wr(8'h41,16'h0000);
  wr(8'h84,0);wr(8'h85,0);wr(8'h44,0);cop_enable=1;
  wait(vp==vstart-4);@(negedge native_clk);bpl_enable=1;
 end
 initial begin #250000000;$fatal(1,"Native resolution watchdog mode%0d starts%0d count%0d malformed%0d",capture_mode,starts,count,malformed);end
endmodule
