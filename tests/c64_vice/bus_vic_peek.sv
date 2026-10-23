// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Real VIC graphics/collision state with synthetic all-set pixel data. The
// early bus strobe must latch and clear collision bytes without a device tick.
module bus_vic_peek;
  reg clk=0,reset=1;always #5 clk=!clk;
  integer phase=0;
  always @(negedge clk)phase=(phase+1)%32;
  wire phi=phase>=16,ena_pixel=phase%4==3,ena_data=phase==15||phase==31;
  reg cs=0,we=0,early_active=0,early=0;reg [5:0] address=0;reg [7:0] data=0;
  wire [7:0] result;wire [8:0] raster;
  video_vicii_656x #(.registeredAddress(1),.emulateRefresh(1),.emulateLightpen(1),.emulateGraphics(1)) dut(
    .clk(clk),.reset(reset),.phi(phi),.enaData(ena_data),.enaPixel(ena_pixel),.baSync(1'b0),.ba(),.ba_dma(),.ba_reu(),
    .mode6569(1'b1),.mode6567old(1'b0),.mode6567R8(1'b0),.mode6572(1'b0),.turbo_en(1'b0),.turbo_state(),.variant(2'b00),
    .read_early_active(early_active),.read_early(early),.cs(cs),.we(we),.lp_n(1'b1),.aRegisters(address),.diRegisters(data),.di(8'hff),.diColor(4'hf),.\do (result),
    .vicAddr(),.irq_n(),.hSync(),.vSync(),.colorIndex(),.debugX(),.debugY(raster),.vicRefresh(),.addrValid());
  task write_reg(input integer a,input integer v);
    while(phase!=16)@(negedge clk);cs=1;we=1;address=a;data=v;
    repeat(32)@(negedge clk);cs=0;we=0;repeat(32)@(negedge clk);
  endtask
  task peek(input integer a,input integer expected);
    while(phase!=16)@(negedge clk);early_active=1;cs=1;address=a;
    @(negedge clk);early=1;
    if(ena_data||ena_pixel)$fatal(1,"Early read unexpectedly uses a device tick");
    @(negedge clk);early=0;
    if(expected>=0&&result!==expected[7:0])$fatal(1,"Peek register %h result=%h expected=%h",a,result,expected);
    repeat(13)@(negedge clk);cs=0;early_active=0;
  endtask
  initial begin
    repeat(64)@(negedge clk);reset=0;
    // The upstream collision latch is read-cleared, not reset-cleared.
    peek('h1e,-1);peek('h1f,-1);
    write_reg('h11,'h1b);write_reg('h16,'h08);write_reg(0,64);write_reg(2,64);
    write_reg(1,80);write_reg(3,80);write_reg('h15,3);
    wait(raster==120);write_reg('h15,0);
    // An unselected strobe must not clear the retained collision.
    while(phase!=16)@(negedge clk);early_active=1;cs=0;address='h1e;
    @(negedge clk);early=1;@(negedge clk);early=0;
    peek('h1e,3);peek('h1e,0);peek('h1f,3);peek('h1f,0);
    // Ordinary register data also arrives without waiting for a pixel edge.
    peek('h11,'h1b);
    $display("PASS: VIC early peek 5 selected reads, unselected read preserved, no native tick");$finish;
  end
  initial begin repeat(312*63*32)@(posedge clk);$fatal(1,"VIC peek timeout");end
endmodule
