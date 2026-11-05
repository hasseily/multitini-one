// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Full native SHRES capture extends into real1024x768 horizontal blanking.
// Both AXI ports use data-checked symbolic DDR: every written pixel is
// validated before reads from its completed immutable slot are permitted.
module tb_amiga_wide_pipeline;
 parameter bit OMIT_VIRTUAL_CAPTURE=0;
 parameter bit BYPASS_FRAME_EFFECTS=0;
 localparam integer WIDTH=1280,HEIGHT=512,STRIDE=2560,FRAME_BYTES=1310720;
 localparam integer HISTORY_BYTES=2621440,H_TOTAL=1344,V_TOTAL=806;
 reg clk=0,pixel_clk=0,resetn=0;
 always #3.750 clk=~clk;
 always #7.692 pixel_clk=~pixel_clk;
 reg[2:0]release_pixel=0;
 always @(posedge pixel_clk or negedge resetn)
  if(!resetn)release_pixel<=0;else release_pixel<={release_pixel[1:0],1'b1};
 wire pixel_resetn=release_pixel[2];
 wire[11:0]cx,cy,cw,ch,fx,fy,fw,fh,ha,va,ht,vt;
 wire[9:0]sx,sy;
 wire[3:0]cn,cd,fn,fd,px,py;
 multitini_display_geometry_pair #(.MACHINE(7),.WIDTH(640),.HEIGHT(512),
  .ALT_WIDTH(724),.ALT_HEIGHT(566),.WIDE_WIDTH(1280),.WIDE_HEIGHT(512),
  .VIRTUAL_CAPTURE(!OMIT_VIRTUAL_CAPTURE)) capture_geometry(
  .mode(4'd1),.multiplier(3'd0),.source_profile(2'd2),
  .view_x(cx),.view_y(cy),.view_width(cw),.view_height(ch),.scale_num(cn),.scale_den(cd));
 multitini_display_geometry_pair #(.MACHINE(7),.WIDTH(640),.HEIGHT(512),
  .ALT_WIDTH(724),.ALT_HEIGHT(566),.WIDE_WIDTH(1280),.WIDE_HEIGHT(512)) final_geometry(
  .mode(4'd1),.multiplier(3'd0),.source_profile(2'd2),
  .view_x(fx),.view_y(fy),.view_width(fw),.view_height(fh),.scale_num(fn),.scale_den(fd),
  .h_active(ha),.v_active(va),.h_total(ht),.v_total(vt),
  .source_x0(sx),.source_y0(sy),.phase_x0(px),.phase_y0(py));
 reg[11:0]x=0,y=767;
 always @(posedge pixel_clk)if(pixel_resetn)begin
  if(x==H_TOTAL-1)begin x<=0;y<=y==V_TOTAL-1?0:y+1'b1;end else x<=x+1'b1;
 end
 wire vblank=x==0&&y==768;
 function automatic[15:0]pattern(input integer ax,ay);
  pattern=16'(16'h1357^(ax*97)^(ay*997)^(ax<<6)^(ay<<3));
 endfunction
 function automatic[31:0]history_pixel(input integer ax,ay);
  reg[15:0]value;
  begin value=pattern(ax,ay);history_pixel={8'd0,value[4:0],value[4:2],
   value[10:5],value[10:9],value[15:11],value[15:13]};end
 endfunction
 wire[15:0]rgb=pattern(x,y-128);
 reg wr_en=0;reg[11:0]wr_addr=0,rd_addr=0;reg[31:0]wr_data=0;wire[31:0]rd_data;
 wire pixel_use;wire[15:0]pixel_rgb565;
 wire[31:0]awaddr[0:1],araddr[0:1];wire[3:0]awlen[0:1],arlen[0:1];
 wire[2:0]awsize[0:1],arsize[0:1];wire[1:0]awburst[0:1],arburst[0:1];
 wire[1:0]awvalid,wvalid,wlast,bready,arvalid,rready;
 reg[1:0]awready=0,wready=0,arready=0,bvalid=0,rvalid=0,rlast=0;
 wire[63:0]wdata[0:1];wire[7:0]wstrb[0:1];reg[63:0]rdata[0:1];
 multitini_video_framebuffer #(.DYNAMIC_DISPLAY(1),.ENABLE_FRAME_EFFECTS(!BYPASS_FRAME_EFFECTS),.WIDTH(640),.HEIGHT(512),
  .ALT_WIDTH(724),.ALT_HEIGHT(566),.WIDE_WIDTH(1280),.WIDE_HEIGHT(512),
  .WIDE_SLOT_SHIFT(21),.WIDE_CAPTURE_BASE(32'h22000000),.WIDE_OUTPUT_BASE(32'h22800000),
  .WIDE_HISTORY_BASE(32'h23000000),.WIDE_HISTORY_BYTES(4194304),
  .SCALE_NUM(1),.SCALE_DEN(1),.SHARED_PIXEL_RESET(1)) dut(
  .source_alt(1'b0),.pixel_source_alt(1'b0),.source_profile(2'd2),.pixel_source_profile(2'd2),
  .display_pause(1'b0),.display_quiescent(),.display_filter_scale_num(4'd1),.display_filter_scale_den(4'd1),
  .display_view_x(fx),.display_view_y(fy),.display_view_width(fw),.display_view_height(fh),
  .display_scale_num(fn),.display_scale_den(fd),
  .capture_display_view_x(cx),.capture_display_view_y(cy),.capture_display_view_width(cw),.capture_display_view_height(ch),
  .capture_display_scale_num(cn),.capture_display_scale_den(cd),
  .display_h_active(ha),.display_v_active(va),.display_h_total(ht),.display_v_total(vt),
  .display_source_x0(sx),.display_source_y0(sy),.display_phase_x0(px),.display_phase_y0(py),
  .clk(clk),.resetn(resetn),.wr_en(wr_en),.wr_addr(wr_addr),.wr_data(wr_data),.wr_strb(4'hf),.rd_addr(rd_addr),.rd_data(rd_data),
  .pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.shared_pixel_resetn(pixel_resetn),
  .vblank_start(vblank),.machine_ready(1'b1),.scan_x(x),.scan_y(y),.machine_rgb565(rgb),.pixel_use(pixel_use),.pixel_rgb565(pixel_rgb565),
  .awaddr(awaddr[0]),.awlen(awlen[0]),.awsize(awsize[0]),.awburst(awburst[0]),.awvalid(awvalid[0]),.awready(awready[0]),
  .wdata(wdata[0]),.wstrb(wstrb[0]),.wlast(wlast[0]),.wvalid(wvalid[0]),.wready(wready[0]),.bresp(2'd0),.bvalid(bvalid[0]),.bready(bready[0]),
  .araddr(araddr[0]),.arlen(arlen[0]),.arsize(arsize[0]),.arburst(arburst[0]),.arvalid(arvalid[0]),.arready(arready[0]),
  .rdata(rdata[0]),.rresp(2'd0),.rlast(rlast[0]),.rvalid(rvalid[0]),.rready(rready[0]),
  .fx_awaddr(awaddr[1]),.fx_awlen(awlen[1]),.fx_awsize(awsize[1]),.fx_awburst(awburst[1]),.fx_awvalid(awvalid[1]),.fx_awready(awready[1]),
  .fx_wdata(wdata[1]),.fx_wstrb(wstrb[1]),.fx_wlast(wlast[1]),.fx_wvalid(wvalid[1]),.fx_wready(wready[1]),.fx_bresp(2'd0),.fx_bvalid(bvalid[1]),.fx_bready(bready[1]),
  .fx_araddr(araddr[1]),.fx_arlen(arlen[1]),.fx_arsize(arsize[1]),.fx_arburst(arburst[1]),.fx_arvalid(arvalid[1]),.fx_arready(arready[1]),
  .fx_rdata(rdata[1]),.fx_rresp(2'd0),.fx_rlast(rlast[1]),.fx_rvalid(rvalid[1]),.fx_rready(rready[1]));
 reg[1:0]reading=0,writing=0,response_pending=0;
 reg[31:0]ra[0:1],wa[0:1],last_wa[0:1];
 integer rleft[0:1],wleft[0:1],bdelay[0:1];
 integer capture_bytes[0:3],output_bytes[0:3],capture_right[0:3],output_right[0:3];
 reg[3:0]capture_complete=0,output_complete=0;
 integer captures=0,outputs=0,hist_bytes=0,ticks=0,p,k,slot,offset,ax,ay;
 function automatic[63:0]read_memory(input[31:0]address,input integer port);
  integer region,off,s,j;
  begin
   region=address>=32'h22800000;
   if(port==0&&!region || port==1&&region || address<32'h22000000 || address>=32'h23000000)
    $fatal(1,"Wide pipeline invalid read pool/owner %0d %h",port,address);
   off=(address-(region?32'h22800000:32'h22000000))%32'h200000;
   s=(address-(region?32'h22800000:32'h22000000))/32'h200000;
   if(off+8>FRAME_BYTES || s>3 || (region?!output_complete[s]:!capture_complete[s]))
    $fatal(1,"Wide pipeline read before finalBRESP/slot bounds %h",address);
   for(j=0;j<4;j=j+1)read_memory[j*16+:16]=pattern((off+j*2)%STRIDE/2,(off+j*2)/STRIDE);
  end
 endfunction
 always @(negedge clk)begin
  ticks=ticks+1;
  for(integer q=0;q<2;q=q+1)begin
   arready[q]=resetn&&!reading[q]&&!rvalid[q]&&ticks%11!=q;
   awready[q]=resetn&&!writing[q]&&!response_pending[q]&&!bvalid[q]&&ticks%7!=q;
   wready[q]=resetn&&writing[q]&&ticks%4!=q;
  end
 end
 always @(posedge clk)if(resetn)for(p=0;p<2;p=p+1)begin
  if(arvalid[p]&&arready[p])begin
   if(reading[p]||araddr[p][2:0]||arsize[p]!=3||arburst[p]!=1||araddr[p][11:0]+8*(arlen[p]+1)>4096)$fatal(1,"Wide invalid AR");
   reading[p]<=1;ra[p]<=araddr[p];rleft[p]<=arlen[p]+1;
  end
  if(rvalid[p]&&rready[p])begin rvalid[p]<=0;ra[p]<=ra[p]+8;rleft[p]<=rleft[p]-1;if(rlast[p])reading[p]<=0;end
  if(reading[p]&&!rvalid[p]&&ticks%5!=p)begin rdata[p]<=read_memory(ra[p],p);rvalid[p]<=1;rlast[p]<=rleft[p]==1;end
  if(awvalid[p]&&awready[p])begin
   if(writing[p]||awaddr[p][2:0]||awsize[p]!=3||awburst[p]!=1||awaddr[p][11:0]+8*(awlen[p]+1)>4096)$fatal(1,"Wide invalid AW");
   writing[p]<=1;wa[p]<=awaddr[p];wleft[p]<=awlen[p]+1;
   if(p==0)begin
    if(awaddr[p]<32'h22000000||awaddr[p]>=32'h22800000)$fatal(1,"Wide capture pool");
    slot=(awaddr[p]-32'h22000000)/32'h200000;offset=(awaddr[p]-32'h22000000)%32'h200000;
    if(offset==0)begin capture_complete[slot]=0;capture_bytes[slot]=0;capture_right[slot]=0;end
   end else if(awaddr[p]<32'h23000000)begin
    if(awaddr[p]<32'h22800000)$fatal(1,"Wide output pool");
    slot=(awaddr[p]-32'h22800000)/32'h200000;offset=(awaddr[p]-32'h22800000)%32'h200000;
    if(dut.active_valid&&slot==dut.active_value[1:0])$fatal(1,"Wide engine overwrote displayed slot");
    if(offset==0)begin output_complete[slot]=0;output_bytes[slot]=0;output_right[slot]=0;end
   end else if(awaddr[p]>=32'h23000000+HISTORY_BYTES)$fatal(1,"Wide history pool");
  end
  if(wvalid[p]&&wready[p])begin
   if(wlast[p]!==(wleft[p]==1)||wstrb[p]!==8'hff)$fatal(1,"Wide WLAST/strobe");
   if(p==0 || wa[p]<32'h23000000)begin
    slot=(wa[p]-(p==0?32'h22000000:32'h22800000))/32'h200000;
    offset=(wa[p]-(p==0?32'h22000000:32'h22800000))%32'h200000;
    if(offset+8>FRAME_BYTES || offset!==(p==0?capture_bytes[slot]:output_bytes[slot]))$fatal(1,"Wide pixel coverage/slot bound %h",wa[p]);
    for(k=0;k<4;k=k+1)begin
     ax=(offset+k*2)%STRIDE/2;ay=(offset+k*2)/STRIDE;
     if(wdata[p][k*16+:16]!==pattern(ax,ay))$fatal(1,"Wide written pixel port%0d x%0d y%0d got%h expected%h",p,ax,ay,wdata[p][k*16+:16],pattern(ax,ay));
     if(ax>=1024)begin if(p==0)capture_right[slot]=capture_right[slot]+1;else output_right[slot]=output_right[slot]+1;end
    end
    if(p==0)capture_bytes[slot]=capture_bytes[slot]+8;else output_bytes[slot]=output_bytes[slot]+8;
   end else begin
    offset=wa[p]-32'h23000000;
    if(offset==0)hist_bytes=0;
    if(offset!=hist_bytes || offset+8>HISTORY_BYTES)$fatal(1,"Wide history coverage/bounds");
    for(k=0;k<2;k=k+1)if(wdata[p][k*32+:32]!==history_pixel(((offset/4+k)%WIDTH),(offset/4+k)/WIDTH))$fatal(1,"Wide identity history pixel");
    hist_bytes=hist_bytes+8;
   end
   wa[p]<=wa[p]+8;wleft[p]<=wleft[p]-1;last_wa[p]<=wa[p];
   if(wlast[p])begin writing[p]<=0;response_pending[p]<=1;bdelay[p]<=9;end
  end
  if(response_pending[p])begin if(bdelay[p]!=0)bdelay[p]<=bdelay[p]-1;else begin bvalid[p]<=1;response_pending[p]<=0;end end
  if(bvalid[p]&&bready[p])begin
   bvalid[p]<=0;
   if(last_wa[p]<32'h23000000)begin
    slot=(last_wa[p]-(p==0?32'h22000000:32'h22800000))/32'h200000;
    offset=(last_wa[p]-(p==0?32'h22000000:32'h22800000))%32'h200000;
    if(offset+8==FRAME_BYTES)begin
     if((p==0?capture_right[slot]:output_right[slot])!=256*512)$fatal(1,"Wide hidden right256pixels absent");
     if(p==0)begin capture_complete[slot]=1;captures=captures+1;end
     else begin output_complete[slot]=1;outputs=outputs+1;end
    end
   end
  end
 end
 reg checking=0;integer checked=0;
 always @(negedge pixel_clk)begin
  #1;
  if(dut.active_valid&&x==0&&y==0)checking=1;
  if(checking)begin
   if(x<1024&&y>=128&&y<640)begin
    if(!pixel_use||pixel_rgb565!==pattern(x+128,y-128))$fatal(1,"Wide final crop pixel x%0d y%0d use%0b got%h expected%h",x,y,pixel_use,pixel_rgb565,pattern(x+128,y-128));
    checked=checked+1;
    if(x==1023&&y==639)begin
     if(checked!=1024*512||captures<1||outputs<1||dut.scheduler_error||dut.engine_errors||dut.deadline_misses||dut.capture_errors||dut.read_errors||dut.underruns)
      $fatal(1,"Wide pipeline incomplete/diagnostics checked%0d cap%0d out%0d sched%0d",checked,captures,outputs,dut.scheduler_error);
     $display("PASS wide virtual pipeline:1024x768@65MHz real1344x806 raster; capture/output655360pixels inclhiddenright256, visible524288centerpixels; captures%0d outputs%0d,2MiBslots,lastBRESP,AXIstalls",captures,outputs);$finish;
    end
   end else if(pixel_use)$fatal(1,"Wide final pixel outside actual viewport");
  end
 end
 task write_reg(input[11:0]address,input[31:0]value);
  begin @(negedge clk);wr_addr=address;wr_data=value;wr_en=1;@(negedge clk);wr_en=0;end
 endtask
 initial begin
  for(integer s=0;s<4;s=s+1)begin capture_bytes[s]=0;output_bytes[s]=0;capture_right[s]=0;output_right[s]=0;end
  #1;
  if({cx,cy,cw,ch,cn,cd}!={12'd0,12'd128,12'd1280,12'd512,4'd1,4'd1})$fatal(1,"Wide virtual capture geometry missing");
  if({fx,fy,fw,fh,sx,sy,px,py}!={12'd0,12'd128,12'd1024,12'd512,10'd128,10'd0,4'd0,4'd0}||ha!=1024||va!=768||ht!=1344||vt!=806)$fatal(1,"Wide final geometry differs from physical1024x768");
  repeat(20)@(negedge clk);resetn=1;repeat(30)@(negedge clk);
  write_reg(12'h1b0,0);write_reg(12'h180,7);
 end
 initial begin #100000000;$fatal(1,"Wide virtual pipeline watchdog xy%0d,%0d cap%0d out%0d producer%0d error%0d",x,y,captures,outputs,dut.producer,dut.scheduler_error);end
endmodule
