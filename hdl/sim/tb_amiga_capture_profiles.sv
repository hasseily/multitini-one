// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Native source profiles cross to the real DDR writer through its mailboxes.
// Check every write by its physical DDR row, including both field parities.
module tb_amiga_capture_profiles;
 reg clk=0,pixel_clk=0;always #3.333 clk=~clk;always #17.62 pixel_clk=~pixel_clk;
 reg resetn=0,pixel_resetn=0,enable=0,source_alt=0,source_weave=0,frame_start=0;
 reg[1:0]source_profile=0;wire[7:0]slot_profile;integer active_profile=0;
 reg[11:0]x=12'hfff,y=12'hfff;reg[15:0]rgb=0;
 wire[31:0]producer,drops,errors,awaddr;reg[31:0]consumer=0;
 wire[3:0]slot_alt,awlen;wire[2:0]awsize;wire[1:0]awburst;wire awvalid,wvalid,wlast,bready,quiescent;
 wire[63:0]wdata;wire[7:0]wstrb;reg bvalid=0,hold_final=0;reg[1:0]bresp=0;
 integer width=640,height=512,stride=640,expected_bursts=5120,frame_checks=0;
 integer row,px,lane,base,beat=0,bursts=0,checks=0,response_age=0,cycle=0;
 reg transfer=0,awaiting_final=0;reg[31:0]previous_producer=0;
 reg awready=0,wready=0;
 function automatic[15:0]pattern(input integer sx,sy);
  pattern=16'(sx^(sy*997)^(width==724 ? 16'h99aa:16'h1357));
 endfunction
 multitini_video_capture #(.WIDTH(640),.HEIGHT(512),.ALT_WIDTH(724),.ALT_HEIGHT(566),
  .WIDE_WIDTH(1280),.WIDE_HEIGHT(512),.WIDE_BASE(32'h23800000),.WIDE_SLOT_SHIFT(21),
  .VIEW_X(0),.VIEW_Y(0),.SCALE_NUM(1),.SCALE_DEN(1),.BASE(32'h23000000),.SHARED_PIXEL_RESET(1),.ABORT_INCOMPLETE_ON_VBLANK(1)) dut(
  .display_view_x(12'd0),.display_view_y(12'd0),.display_view_width(12'd640),.display_view_height(12'd512),
  .display_scale_num(4'd1),.display_scale_den(4'd1),.source_alt(source_alt),.source_weave(source_weave),.source_profile(source_profile),.slot_alt(slot_alt),.slot_profile(slot_profile),
  .clk(clk),.resetn(resetn),.enable(enable),.consumer(consumer),.producer(producer),.drops(drops),.errors(errors),.quiescent(quiescent),.busy(),.pixel_running(),
  .pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.shared_pixel_resetn(resetn),.machine_ready(1'b1),.scan_x(x),.scan_y(y),.rgb565(rgb),.vblank_start(frame_start),
  .awaddr(awaddr),.awlen(awlen),.awsize(awsize),.awburst(awburst),.awvalid(awvalid),.awready(awready),
  .wdata(wdata),.wstrb(wstrb),.wlast(wlast),.wvalid(wvalid),.wready(wready),.bresp(bresp),.bvalid(bvalid),.bready(bready));
 always @(posedge clk)begin
  if(awvalid && awready)begin
   if(awlen!=15 || awsize!=3 || awburst!=1 || awaddr[6:0]!=0 || awaddr[11:0]>3968)$fatal(1,"Invalid profile DDR burst");
   base=awaddr-(active_profile==2 ? 32'h23800000:32'h23000000)-(producer[1:0]*(active_profile==2 ? 2097152:1048576));beat=0;transfer=1;
   if(base<0 || base>=stride*height*2)$fatal(1,"Profile DDR address escaped frame %h",awaddr);
  end
  if(wvalid && wready)begin
   if(!transfer || wlast!==(beat==15) || wstrb!=255)$fatal(1,"Invalid profile data beat");
   for(lane=0;lane<4;lane=lane+1)begin
    row=(base+beat*8+lane*2)/(stride*2);px=((base+beat*8+lane*2)%(stride*2))/2;
    if(wdata[lane*16+:16]!==(px>=width ? 16'd0:pattern(px,row)))
     $fatal(1,"Woven DDR pixel at%0d,%0d expected%h got%h",px,row,px>=width?16'd0:pattern(px,row),wdata[lane*16+:16]);
    checks=checks+1;frame_checks=frame_checks+1;
   end
   beat=beat+1;
   if(beat==16)begin transfer=0;bursts=bursts+1;response_age=5;if(bursts==expected_bursts)awaiting_final=1;end
  end
  if(bvalid && bready)bvalid<=0;
 end
 always @(negedge clk)begin
  cycle=cycle+1;
  awready=!transfer && !bvalid && response_age==0 && cycle%5!=0;
  wready=cycle%7!=0;
  if(response_age>0 && !(hold_final && awaiting_final))begin
   response_age=response_age-1;if(response_age==0)bvalid=1;
  end
 end
 task quiet;
  integer age;
  begin
   @(negedge pixel_clk);pixel_resetn=0;enable=0;x=12'hfff;y=12'hfff;
   age=0;while(!quiescent && age<10000)begin @(negedge clk);age=age+1;end
   if(!quiescent)$fatal(1,"Profile capture failed to drain pendingprofile/FIFO/AXI");
   repeat(30)begin @(negedge clk);if(!quiescent || awvalid || wvalid || bvalid)$fatal(1,"Capture traffic after quiescence");end
   consumer=producer;
  end
 endtask
 task capture(input integer profile,input bit weave);
  integer r,q,py,age;
  begin
   quiet;active_profile=profile;source_profile=2'(profile);source_alt=profile==1;source_weave=weave;
   width=profile==2?1280:profile==1?724:640;height=profile==1?566:512;stride=profile==2?1280:profile==1?768:640;
   expected_bursts=stride*height/64;bursts=0;frame_checks=0;awaiting_final=0;hold_final=1;previous_producer=producer;
   @(negedge pixel_clk);pixel_resetn=1;enable=1;
   wait(dut.arm_valid_pixel && dut.profile_ready);
   @(negedge pixel_clk);frame_start=1;@(negedge pixel_clk);frame_start=0;
   repeat(4)@(negedge pixel_clk);
   for(r=0;r<height;r=r+1)begin
    py=weave ? 2*(r%(height/2))+(r>=height/2):r;
    for(q=0;q<stride;q=q+1)begin
     @(negedge pixel_clk);x=q;y=py;rgb=q>=width?0:pattern(q,py);
    end
    // Input pins can change after frame arm; the accepted frame profile is
    // immutable and may not be replaced by a later source preference.
    if(r==1)begin source_alt=profile!=1;source_profile=profile==2?0:2;source_weave=!weave;end
   end
   @(negedge pixel_clk);x=12'hfff;y=12'hfff;
   wait(awaiting_final);
   repeat(50)begin @(negedge clk);if(producer!=previous_producer)$fatal(1,"Frame published before final write response");end
   hold_final=0;age=0;
   while(producer==previous_producer && age<10000)begin @(negedge clk);age=age+1;end
   if(age==10000 || errors || frame_checks!=stride*height || slot_alt[previous_producer[1:0]]!==(profile==1) || slot_profile[previous_producer[1:0]*2+:2]!==2'(profile))
    $fatal(1,"Profile publication mismatch pixels%0d producer%0d errors%0d metadata%h",frame_checks,producer,errors,slot_alt);
   $display("PASS capture profile%0d weave%0d: %0dx%0d stride%0d all%0d DDR pixels,immutableprofile,finalBRESPmetadata",profile,weave,width,height,stride,frame_checks);
  end
 endtask
 initial begin
  repeat(10)@(negedge clk);resetn=1;repeat(20)@(negedge clk);
  capture(0,1);capture(1,1);capture(2,1);capture(0,0);capture(2,0);quiet;
  // Cancel an arm before a frame arrives. Its profile mailbox cancellation
  // must drain too, otherwise reconfiguration can accept stale geometry.
  pixel_resetn=1;enable=1;wait(dut.arm_valid_pixel);quiet;
  if(producer!=5)$fatal(1,"Cancelled profile arm published a frame");
  $display("PASS ALTcapture geometryswitch,cancelledarm,coherentprofile,FIFOownershipdrain,total%0d pixels",checks);$finish;
 end
 initial begin #200000000;$fatal(1,"Profile capture watchdog state%0d producer%0d",dut.state,producer);end
endmodule
