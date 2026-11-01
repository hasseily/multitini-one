// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Live base -> ALT -> WIDE -> base through the complete two-port pipeline.
// Only pixels are held; PS resets and accepted AXI ownership remain alive.
module tb_amiga_profile_transition;
 parameter bit OMIT_PIXEL_HOLD=0;
 reg clk=0,pixel_clk=0,resetn=0,pause=0,pixel_hold=0;
 reg[1:0]profile=0;
 wire alt=profile==1;
 always #3.759 clk=~clk;always #3.367 pixel_clk=~pixel_clk;
 reg[2:0] release_pixel=0;
 always @(posedge pixel_clk or negedge resetn)
  if(!resetn)release_pixel<=0;else release_pixel<={release_pixel[1:0],1'b1};
 wire shared_pixel_resetn=release_pixel[2];
 wire[11:0] width=profile==2?65:alt?19:13,height=profile==2?12:alt?10:8;
 wire[3:0] scale=profile==0?2:1;
 wire[11:0] vx=profile==2?7:alt?19:16,vy=profile==2?10:alt?5:2;
 wire[31:0]slot_bytes=profile==2?32'h200000:32'h100000;
 wire[31:0]output_base=profile==2?32'h22800000:32'h22400000;
 wire[31:0]history_base=profile==2?32'h23000000:32'h22800000;
 wire[31:0]stride=((width+63)/64)*128;
 wire[11:0] vw=width*scale,vh=height*scale;
 reg[11:0] x=0,y=0;
 always @(posedge pixel_clk)if(!resetn)begin x<=0;y<=0;end
  else if(x==159)begin x<=0;y<=y==63?0:y+1;end else x<=x+1;
 wire vblank=x==0 && y==40;
 integer epoch=0;
 function automatic[15:0] pattern(input integer px,py,version);
  pattern=16'(16'h1357^(px*97)^(py*997)^(version*16'h2601));
 endfunction
 wire[15:0] rgb=pattern((x-vx)/scale,(y-vy)/scale,epoch);
 reg wr_en=0;reg[11:0]wr_addr=0,rd_addr=0;reg[31:0]wr_data=0;wire[31:0]rd_data;
 wire quiescent,pixel_use;wire[15:0]pixel_rgb565;
 wire[31:0]awaddr[0:1],araddr[0:1];wire[3:0]awlen[0:1],arlen[0:1];
 wire[2:0]awsize[0:1],arsize[0:1];wire[1:0]awburst[0:1],arburst[0:1];
 wire[1:0]awvalid,wvalid,wlast,bready,arvalid,rready;
 reg[1:0]awready=0,wready=0,arready=0,bvalid=0,rvalid=0,rlast=0;
 wire[63:0]wdata[0:1];wire[7:0]wstrb[0:1];reg[63:0]rdata[0:1];
 multitini_video_framebuffer #(.DYNAMIC_DISPLAY(1),.WIDTH(13),.HEIGHT(8),.ALT_WIDTH(19),.ALT_HEIGHT(10),
  .WIDE_WIDTH(65),.WIDE_HEIGHT(12),.WIDE_SLOT_SHIFT(21),.WIDE_CAPTURE_BASE(32'h22000000),
  .WIDE_OUTPUT_BASE(32'h22800000),.WIDE_HISTORY_BASE(32'h23000000),.WIDE_HISTORY_BYTES(4194304),
  .VIEW_X(16),.VIEW_Y(2),.SCALE_NUM(2),.SCALE_DEN(1),.SHARED_PIXEL_RESET(1)) dut(
  .source_alt(alt),.pixel_source_alt(alt),.display_pause(pause),.display_quiescent(quiescent),
  .source_profile(profile),.pixel_source_profile(profile),
  .display_filter_scale_num(4'd2),.display_filter_scale_den(4'd1),
  .display_view_x(vx),.display_view_y(vy),.display_view_width(vw),.display_view_height(vh),
  .display_scale_num(scale),.display_scale_den(4'd1),
  .capture_display_view_x(vx),.capture_display_view_y(vy),.capture_display_view_width(vw),.capture_display_view_height(vh),
  .capture_display_scale_num(scale),.capture_display_scale_den(4'd1),
  .display_h_active(12'd160),.display_v_active(12'd64),.display_h_total(12'd160),.display_v_total(12'd64),
  .display_source_x0(10'd0),.display_source_y0(10'd0),.display_phase_x0(4'd0),.display_phase_y0(4'd0),
  .clk(clk),.resetn(resetn),.wr_en(wr_en),.wr_addr(wr_addr),.wr_data(wr_data),.wr_strb(4'hf),.rd_addr(rd_addr),.rd_data(rd_data),
  .pixel_clk(pixel_clk),.pixel_resetn(shared_pixel_resetn&&!pixel_hold),.shared_pixel_resetn(shared_pixel_resetn),
  .vblank_start(vblank),.machine_ready(1'b1),.scan_x(x),.scan_y(y),.machine_rgb565(rgb),.pixel_use(pixel_use),.pixel_rgb565(pixel_rgb565),
  .awaddr(awaddr[0]),.awlen(awlen[0]),.awsize(awsize[0]),.awburst(awburst[0]),.awvalid(awvalid[0]),.awready(awready[0]),
  .wdata(wdata[0]),.wstrb(wstrb[0]),.wlast(wlast[0]),.wvalid(wvalid[0]),.wready(wready[0]),.bresp(2'b0),.bvalid(bvalid[0]),.bready(bready[0]),
  .araddr(araddr[0]),.arlen(arlen[0]),.arsize(arsize[0]),.arburst(arburst[0]),.arvalid(arvalid[0]),.arready(arready[0]),
  .rdata(rdata[0]),.rresp(2'b0),.rlast(rlast[0]),.rvalid(rvalid[0]),.rready(rready[0]),
  .fx_awaddr(awaddr[1]),.fx_awlen(awlen[1]),.fx_awsize(awsize[1]),.fx_awburst(awburst[1]),.fx_awvalid(awvalid[1]),.fx_awready(awready[1]),
  .fx_wdata(wdata[1]),.fx_wstrb(wstrb[1]),.fx_wlast(wlast[1]),.fx_wvalid(wvalid[1]),.fx_wready(wready[1]),.fx_bresp(2'b0),.fx_bvalid(bvalid[1]),.fx_bready(bready[1]),
  .fx_araddr(araddr[1]),.fx_arlen(arlen[1]),.fx_arsize(arsize[1]),.fx_arburst(arburst[1]),.fx_arvalid(arvalid[1]),.fx_arready(arready[1]),
  .fx_rdata(rdata[1]),.fx_rresp(2'b0),.fx_rlast(rlast[1]),.fx_rvalid(rvalid[1]),.fx_rready(rready[1]));
 // Preserve physical1MiB aliases in a compact model. Wideoutput22800000
 // intentionally aliases the oldlegacy history; switching cannot rely on
 // fresh disjoint backing storage to conceal a staleprofile owner.
 localparam integer REGION_BYTES=4096,REGIONS=32;
 reg[7:0]memory[0:REGIONS*REGION_BYTES-1];
 reg[1:0]reading=0,writing=0,response_pending=0;reg hold_r=0,hold_b=0;
 reg[31:0]read_address[0:1],write_address[0:1];
 integer read_left[0:1],write_left[0:1],response_delay[0:1];
 integer p,lane,ticks=0,traffic=0;
 function automatic integer index(input[31:0]address);
  integer region,offset;
  reg legal;
  begin
   legal=0;
   if(address>=32'h22000000 && address<32'h22000000+4*slot_bytes)
    legal=(address-32'h22000000)%slot_bytes<stride*height;
   if(address>=output_base && address<output_base+4*slot_bytes)
    legal=(address-output_base)%slot_bytes<stride*height;
   if(address>=history_base && address<history_base+width*height*4)legal=1;
   region=(address-32'h22000000)>>20;offset=address&32'hfffff;
   if(!legal || region>=REGIONS || offset>=REGION_BYTES)
    $fatal(1,"Live profile%0d escaped selected pools/slot extent %h",profile,address);
   index=region*REGION_BYTES+offset;
  end
 endfunction
 function automatic[63:0]read_memory(input[31:0]address);
  integer k;begin for(k=0;k<8;k=k+1)read_memory[k*8+:8]=memory[index(address+k)];end
 endfunction
 always @(negedge clk)begin
  ticks=ticks+1;
  for(integer q=0;q<2;q=q+1)begin
   arready[q]=resetn&&!reading[q]&&!rvalid[q]&&ticks%5!=q;
   awready[q]=resetn&&!writing[q]&&!response_pending[q]&&!bvalid[q]&&ticks%7!=q;
   wready[q]=resetn&&writing[q]&&ticks%4!=q;
  end
 end
 always @(posedge clk)if(resetn)for(p=0;p<2;p=p+1)begin
  if(arvalid[p]&&arready[p])begin
   if(reading[p]||araddr[p][2:0]||arsize[p]!=3||arburst[p]!=1||araddr[p][11:0]+8*(arlen[p]+1)>4096)$fatal(1,"Live invalid AR");
   reading[p]<=1;read_address[p]<=araddr[p];read_left[p]<=arlen[p]+1;traffic=traffic+1;
  end
  if(rvalid[p]&&rready[p])begin rvalid[p]<=0;read_address[p]<=read_address[p]+8;read_left[p]<=read_left[p]-1;if(rlast[p])reading[p]<=0;end
  if(reading[p]&&!rvalid[p]&&!(hold_r&&p==0)&&ticks%3!=p)begin
   rdata[p]<=read_memory(read_address[p]);rvalid[p]<=1;rlast[p]<=read_left[p]==1;
  end
  if(awvalid[p]&&awready[p])begin
   if(writing[p]||awaddr[p][2:0]||awsize[p]!=3||awburst[p]!=1||awaddr[p][11:0]+8*(awlen[p]+1)>4096)$fatal(1,"Live invalid AW");
   writing[p]<=1;write_address[p]<=awaddr[p];write_left[p]<=awlen[p]+1;traffic=traffic+1;
  end
  if(wvalid[p]&&wready[p])begin
   if(wlast[p]!==(write_left[p]==1))$fatal(1,"Live invalid WLAST");
   for(lane=0;lane<8;lane=lane+1)if(wstrb[p][lane])memory[index(write_address[p]+lane)]=wdata[p][lane*8+:8];
   write_address[p]<=write_address[p]+8;write_left[p]<=write_left[p]-1;
   if(wlast[p])begin writing[p]<=0;response_pending[p]<=1;response_delay[p]<=4;end
  end
  if(response_pending[p])begin
   if(response_delay[p]!=0)response_delay[p]<=response_delay[p]-1;
   else if(!(hold_b&&p==1))begin bvalid[p]<=1;response_pending[p]<=0;end
  end
  if(bvalid[p]&&bready[p])bvalid[p]<=0;
 end
 reg checking=0;integer frames=0,checked=0,frame_pixels=0;
 always @(negedge pixel_clk)begin
  #1;
  if(checking)begin
   if(x>=vx&&x<vx+vw&&y>=vy&&y<vy+vh)begin
    if(!pixel_use||pixel_rgb565!==pattern((x-vx)/scale,(y-vy)/scale,epoch))
     $fatal(1,"Live profile%0d pixel%0d,%0d use%0b got%h expected%h",profile,x,y,pixel_use,pixel_rgb565,pattern((x-vx)/scale,(y-vy)/scale,epoch));
    checked=checked+1;frame_pixels=frame_pixels+1;
    if(x==vx+vw-1&&y==vy+vh-1)begin
     if(frame_pixels!=vw*vh)$fatal(1,"Live profile clipped frame");frames=frames+1;frame_pixels=0;
    end
   end else if(pixel_use)$fatal(1,"Live profile outside viewport");
  end
 end
 task write_reg(input[11:0]address,input[31:0]value);
  begin @(negedge clk);wr_addr=address;wr_data=value;wr_en=1;@(negedge clk);wr_en=0;end
 endtask
 task check_profile;
  begin
   wait(dut.active_valid&&dut.reader_i.active_profile==profile);
   @(negedge pixel_clk);wait(vblank);@(negedge pixel_clk);frame_pixels=0;frames=0;checking=1;
   wait(frames>=3);@(negedge pixel_clk);checking=0;
   if(dut.scheduler_error||dut.engine_errors||dut.deadline_misses||dut.read_errors||dut.underruns!=dut.underrun_base)$fatal(1,"Live profile diagnostics");
   $display("PASS live profile%0d %0dx%0d scale%0d threeframes; total%0d pixels",profile,width,height,scale,checked);
  end
 endtask
 task transition(input[1:0]next_profile);
  integer age,saved;
  begin
   @(negedge clk);hold_r=1;hold_b=1;
   wait(reading[0]&&!rvalid[0]&&response_pending[1]);
   @(negedge clk);pause=1;
   repeat(100)begin @(negedge clk);if(quiescent||!reading[0]||!response_pending[1])$fatal(1,"Live pause lost accepted R/B ownership");end
   hold_r=0;hold_b=0;age=0;
   while(!quiescent&&age<100000)begin @(negedge clk);age=age+1;end
   if(!quiescent||reading||writing||response_pending||rvalid||bvalid)$fatal(1,"Live pause failed DDR drain");
   saved=traffic;repeat(40)@(negedge clk);if(traffic!=saved)$fatal(1,"Live traffic after quiescence");
   @(negedge pixel_clk);pixel_hold=!OMIT_PIXEL_HOLD;repeat(12)@(negedge pixel_clk);
   profile=next_profile;epoch=epoch+1;repeat(16)@(negedge pixel_clk);
   if(!resetn||!shared_pixel_resetn||dut.capture_i.view_right!=vx+vw||dut.reader_i.visible_x0!=vx||dut.reader_i.visible_y0!=vy)
    $fatal(1,"Live pixel hold did not snapshot changed geometry");
   if(traffic!=saved)$fatal(1,"Live DMA while pixel-held");
   pixel_hold=0;pause=0;check_profile;
  end
 endtask
 integer profile_invalidations=0,same_profile_history=0;
 reg[1:0]previous_job_profile;
 always @(posedge clk)if(resetn&&dut.engine_start)begin
  previous_job_profile=dut.engine_i.job_profile;
  #1;
  if(dut.engine_i.job_profile!==profile || dut.engine_i.active_output_base!==output_base ||
     dut.engine_i.active_history_base!==history_base || dut.engine_i.active_slot_bytes!==slot_bytes)
   $fatal(1,"Live engine accepted wrong immutable profile/layout");
  if(previous_job_profile!=profile)begin
   if(dut.engine_i.use_history || dut.engine_i.history_read_left!=0)
    $fatal(1,"Live profile transition reused foreign history");
   profile_invalidations=profile_invalidations+1;
  end else if(dut.engine_i.use_history)same_profile_history=same_profile_history+1;
 end
 initial begin
  for(integer n=0;n<REGIONS*REGION_BYTES;n=n+1)memory[n]=8'hcd;
  repeat(20)@(negedge clk);resetn=1;repeat(30)@(negedge clk);
  write_reg(12'h1b0,1);write_reg(12'h180,7);check_profile;
  transition(1);transition(2);transition(0);
  if(profile_invalidations!=3 || same_profile_history<4)
   $fatal(1,"Live history transition coverage invalidations%0d reuse%0d",profile_invalidations,same_profile_history);
  $display("PASS live profile transitions: base->ALT->WIDE->base,pixelhold,resnapshot,oldR/Bdrain,physicalpoolaliases,historyinvalidations%0d,reuse%0d",
   profile_invalidations,same_profile_history);$finish;
 end
 initial begin #10000000;$fatal(1,"Live profile transition watchdog profile%0d epoch%0d frames%0d state%0d",profile,epoch,frames,dut.engine_i.read_state);end
endmodule
