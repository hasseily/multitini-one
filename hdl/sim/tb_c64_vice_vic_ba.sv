// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_ba;
logic clk=0,reset=1;integer phase=0,ntsc=0,cycle=11,beam=0,cycles,lines,fd,count=0;
logic aligned=0;logic[7:0] configuration[0:2];
wire phi=phase>=16,ena_pixel=(phase%4)==3,ena_data=phase==15||phase==31;
logic cs=0,we=0;logic[5:0] reg_addr=0;logic[7:0] reg_data=0;
wire[3:0]state;wire[8:0]raster;wire ba,ba_reu;
always #5 clk=!clk;always @(negedge clk)phase=(phase+1)%32;
video_vicii_656x #(.registeredAddress(1),.emulateRefresh(1),.emulateLightpen(1),.emulateGraphics(1)) dut(
 .clk(clk),.reset(reset),.phi(phi),.enaData(ena_data),.enaPixel(ena_pixel),.baSync(1'b0),.ba(ba),.ba_dma(),.ba_reu(ba_reu),
 .mode6569(!ntsc),.mode6567old(1'b0),.mode6567R8(ntsc!=0),.mode6572(1'b0),.turbo_en(1'b0),.turbo_state(),.variant(2'b00),
 .cs(cs),.we(we),.lp_n(1'b1),.aRegisters(reg_addr),.diRegisters(reg_data),.di(8'h0),.diColor(4'h0),.\do (),
 .vicAddr(),.irq_n(),.hSync(),.vSync(),.colorIndex(),.debugX(),.debugY(),.vicRefresh(),.addrValid(),
 .observe_state(state),.observe_raster(raster));
always @(posedge clk)begin
 if(phase==0)begin
  if(!aligned&&state==0)begin aligned=1;cycle=11;beam=raster;end
  else if(aligned)begin if(cycle==cycles)begin cycle=1;beam=(beam+1)%lines;end else cycle++;end
 end
 if(!reset&&raster>=79&&raster<=81&&(phase==15||phase==28))begin
  if($isunknown({ba,ba_reu,raster}))$fatal(1,"unknown BA state");
  $fdisplay(fd,"%0d,%0d,%0d,%0d,%0d",raster,cycle,phase,ba,ba_reu);count++;
 end
end
task automatic write_reg(input integer address,input integer value);
 while(phase!=16)@(negedge clk);cs=1;we=1;reg_addr=address;reg_data=value;
 repeat(32)@(negedge clk);cs=0;we=0;repeat(32)@(negedge clk);
endtask
initial begin
 $readmemh("case.hex",configuration);ntsc=configuration[0];cycles=ntsc?65:63;lines=ntsc?263:312;
 fd=$fopen("rtl_ba.csv","w");if(!fd)$fatal(1,"no trace");$fdisplay(fd,"line,cycle,phase,ba,ba_reu");
 repeat(64)@(negedge clk);reset=0;write_reg('h11,0);
 for(integer i=0;i<8;i++)write_reg(1+2*i,configuration[2]);
 write_reg('h15,configuration[1]);
 while(!(raster==82&&cycle==2&&phase==16))@(negedge clk);
 if(count!=3*cycles*2)$fatal(1,"wrong BA sample count %0d",count);
 $fclose(fd);$display("PASS directed sprite BA stimulus completed");$finish;
end
initial begin repeat(3*312*65*32)@(posedge clk);$fatal(1,"timeout");end
endmodule
