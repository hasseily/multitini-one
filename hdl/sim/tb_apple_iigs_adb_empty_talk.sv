// SPDX-License-Identifier: GPL-3.0-only
// IIgs host-side empty TALK, Doom-style polling and $C010 held-key status.
`timescale 1ns/1ps
module tb_apple_iigs_adb_empty_talk;
 reg clk=0,reset=1,strobe=0,rw=1,rom_select=0;
 always #5 clk=~clk;
 reg [7:0] addr=0,din=0;
 reg [10:0] ps2_key=0;
 reg [24:0] ps2_mouse=0;
 wire [7:0] dout, registered_dout;
 wire irq, general_irq;
 adb dut(.load_rom_clk(clk),.load_rom_we(1'b0),.load_rom_addr(12'b0),.load_rom_data(8'b0),.load_rom_bank(1'b0),
  .CLK_14M(clk),.cen(1'b1),.reset(reset),.cold_reset(1'b1),.addr(addr),.rw(rw),.din(din),.dout(registered_dout),.dout_comb(dout),
  .irq(general_irq),.kbd_srq_irq(irq),.strobe(strobe),.ps2_key(ps2_key),.ps2_mouse(ps2_mouse),.vbl_count(9'b0),
  .CLR80COL(),.STORE80(),.RAMRD(),.RAMWRT(),.ALTZP(),.capslock(),.selftest_override(1'b0),
  .open_apple(),.closed_apple(),.apple_shift(),.apple_ctrl(),.akd(),.K(),.reset_key_pressed(),.rom_select(rom_select));
 task wr(input [7:0] a,input [7:0] d);
  @(negedge clk);addr=a;din=d;rw=0;strobe=1;
  repeat(3)@(negedge clk);
  strobe=0;rw=1;
  repeat(5)@(negedge clk);
 endtask
 task rd(input [7:0] a,output [7:0] d);
  @(negedge clk);addr=a;rw=1;strobe=1;
  #1;d=dout;
  repeat(3)@(negedge clk);
  strobe=0;
  repeat(3)@(negedge clk);
 endtask
 task header(input [7:0] expected);
  @(negedge clk);addr=8'h26;rw=1;strobe=1;
  #1;
  if(dout!==expected)$fatal(1,"header first sample expected=%02x actual=%02x",expected,dout);
  repeat(8)begin
   @(negedge clk);#1;
   if(dout!==expected || registered_dout!==expected)
    $fatal(1,"held header changed expected=%02x comb=%02x reg=%02x",expected,dout,registered_dout);
   if(!dut.cmd_response_ready || !general_irq || !irq)$fatal(1,"response cleared before read completed");
  end
  @(negedge clk);strobe=0;
  repeat(3)@(negedge clk);
  if(dut.cmd_response_ready)$fatal(1,"response header not acknowledged on strobe fall");
 endtask
 task check_status(input bit expected);
  rd(8'h27,status);
  if(status[5]!==expected || registered_dout[5]!==expected)
   $fatal(1,"C027 data full expected=%d comb=%02x reg=%02x",expected,status,registered_dout);
 endtask
 task send_key(input [8:0] scancode,input bit down);
  @(negedge clk);ps2_key={~ps2_key[10],down,scancode};
  repeat(5)@(negedge clk);
 endtask
 task inject_key;
  send_key(9'h076,1'b1);
 endtask
 task reset_keyboard(input bit rom01);
  @(negedge clk);reset=1;rom_select=rom01;ps2_key=0;addr=0;rw=1;strobe=0;
  repeat(8)@(negedge clk);
  reset=0;
  repeat(5)@(negedge clk);
 endtask
 task check_c010(input [7:0] expected,input [3:0] fifo_after);
  @(negedge clk);addr=8'h10;rw=1;strobe=1;
  #1;
  if(dout!==expected)
   $fatal(1,"C010 before commit ROM01=%b expected=%02x comb=%02x",rom_select,expected,dout);
  @(negedge clk);
  if(registered_dout!==expected)
   $fatal(1,"C010 committed ROM01=%b expected=%02x reg=%02x",rom_select,expected,registered_dout);
  repeat(3)@(negedge clk);
  if(dut.kbd_fifo_count!==fifo_after)
   $fatal(1,"C010 held strobe advanced FIFO incorrectly: expected=%d actual=%d",fifo_after,dut.kbd_fifo_count);
  strobe=0;addr=8'h00;
  repeat(3)@(negedge clk);
 endtask
 task inject_mouse;
  @(negedge clk);ps2_mouse={~ps2_mouse[24],8'h01,8'h02,8'h00};
  repeat(5)@(negedge clk);
 endtask
 // Model the relevant upstream IIGS_PollKeys decisions, with a finite guard.
 task doom_poll;
  integer iterations;
  bit complete;
  iterations=0;complete=0;
  while(!complete && iterations<16)begin
   iterations++;
   rd(8'h27,status);
   if(status[5])begin
    rd(8'h26,value);
    if(value[7])begin
     if(value[2:0]!=0)begin
      rd(8'h26,key_first);rd(8'h26,key_second);
      if(doom_adbTalk && key_second==8'h35)key_seen=1;
     end
     doom_adbTalk=0;
    end else if(value[3] && !doom_adbTalk)begin
     wr(8'h26,8'hc2);doom_adbTalk=1;mouse_count=0;
    end
   end else begin
    if(status[7])begin
     rd(8'h24,value);rd(8'h24,value);mouse_count++;
     if(mouse_count==4 && !doom_adbTalk)begin
      wr(8'h26,8'hc2);doom_adbTalk=1;mouse_count=0;
     end
    end
    complete=1;
   end
  end
  if(!complete)$fatal(1,"Doom raw keyboard loop did not finish after %0d iterations",iterations);
 endtask
 reg [7:0] status,value,key_first,key_second;
 bit doom_adbTalk=0,key_seen=0;
 integer mouse_count=0;
 initial begin
  repeat(8)@(negedge clk);reset=0;
  wr(8'h26,8'h04);wr(8'h26,8'h01);
  wr(8'h26,8'h52);wr(8'h27,8'h50);
  rd(8'h26,value); // Drain startup command acknowledgement.
  if(!dut.adb_mode[0])$fatal(1,"autopoll not disabled");
  check_status(0);

  wr(8'h26,8'hc2);
  repeat(8)@(negedge clk);
  // It must survive both command-write and unrelated status-read falling edges.
  check_status(1);check_status(1);
  if(!general_irq || !irq || dut.pending_data!=0)$fatal(1,"empty completion IRQ/payload incorrect");
  header(8'h80);check_status(0);
  if(general_irq || irq || dut.state!=dut.IDLE)$fatal(1,"empty completion failed to release");
  $display("PASS empty keyboard TALK header80 visible/full/IRQ and held until read completes");

  wr(8'h26,8'hc7);repeat(8)@(negedge clk);
  check_status(0);
  if(general_irq || irq || dut.cmd_response_ready)$fatal(1,"absent device gained response");
  $display("PASS absent address retained no response");

  inject_key();check_status(1);rd(8'h26,value);
  if(value!=8'h08)$fatal(1,"keyboard SRQ missing");
  wr(8'h26,8'hc2);check_status(1);header(8'h81);
  rd(8'h26,key_first);rd(8'h26,key_second);check_status(0);
  if(key_first!=8'hff || key_second!=8'h35 || irq || general_irq)
   $fatal(1,"nonempty keyboard packet changed: %02x %02x",key_first,key_second);
  $display("PASS nonempty keyboard TALK retains header81/fillerFF/key35 sequence");

  for(integer i=0;i<4;i++)begin inject_mouse();doom_poll();end
  if(!doom_adbTalk)$fatal(1,"four mouse reports did not issue backup TALK");
  doom_poll();
  if(doom_adbTalk)$fatal(1,"empty backup TALK failed to complete");
  inject_key();doom_poll();check_status(0);
  if(doom_adbTalk || !key_seen || irq || general_irq)$fatal(1,"next key failed after mouse backup TALK");
  $display("PASS Doom poll completes empty backup TALK after four mouse reports and accepts next key");

  // Queue a key on exactly the clock which accepts an empty TALK. The old
  // queue is empty, so completion is empty; the newly queued event keeps SRQ.
  @(negedge clk);addr=8'h26;din=8'hc2;rw=0;strobe=1;
  ps2_key={~ps2_key[10],1'b1,9'h076};
  repeat(3)@(negedge clk);strobe=0;rw=1;
  repeat(5)@(negedge clk);
  header(8'h80);check_status(1);
  if(!irq || !dut.kbd_srq_pending || dut.device_data_pending[2]==0)
   $fatal(1,"simultaneous key/empty TALK lost keyboard service request");
  key_seen=0;doom_poll();check_status(0);
  if(!key_seen || doom_adbTalk || irq || general_irq)$fatal(1,"simultaneous key failed to drain");
  $display("PASS key arriving on the empty TALK clock retains SRQ and drains next");

  for(integer rom01=0;rom01<2;rom01++)begin
   reset_keyboard(rom01[0]);
   send_key(9'h01c,1'b1); // A
   check_c010(8'he1,0);
   rd(8'h00,value);
   if(value!==8'h61)$fatal(1,"C010 did not clear C000 strobe: %02x",value);
   repeat(4)check_c010(8'he1,0);
   send_key(9'h01c,1'b0);
   check_c010(8'h61,0);
   send_key(9'h012,1'b1); // Shift is a modifier, not a character.
   check_c010(8'h61,0);
  end
  $display("PASS C010 reports held character across strobe clears and ignores a lone modifier");

  for(integer rom01=0;rom01<2;rom01++)begin
   reset_keyboard(rom01[0]);
   send_key(9'h01c,1'b1);check_c010(8'he1,0);
   send_key(9'h032,1'b1);check_c010(8'he2,0); // B, most recent
   send_key(9'h01c,1'b0);check_c010(8'he2,0);
   send_key(9'h032,1'b0);check_c010(8'h62,0);

   send_key(9'h01c,1'b1);check_c010(8'he1,0);
   send_key(9'h032,1'b1);check_c010(8'he2,0);
   send_key(9'h032,1'b0);check_c010(rom01[0] ? 8'he2 : 8'h62,0);
   send_key(9'h01c,1'b0);check_c010(8'h62,0);
  end
  $display("PASS C010 release order follows ROM01 and ROM03 held-key rules");

  reset_keyboard(1'b0);
  send_key(9'h01c,1'b1); // A
  send_key(9'h032,1'b1); // B
  send_key(9'h021,1'b1); // C
  send_key(9'h021,1'b0);
  check_c010(8'he3,2);
  check_c010(8'he2,1);
  check_c010(8'h63,0);
  $display("PASS C010 keeps AKD through queued characters and advances FIFO once per strobe");
  $finish;
 end
 initial begin #200000;$fatal(1,"timeout");end
endmodule
