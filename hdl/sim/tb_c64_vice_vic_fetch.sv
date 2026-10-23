// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_fetch;
logic clk=0,reset=1;integer phase=0,ntsc=0,cycle=11,beam=0,cycles,lines,fd;logic aligned=0;logic[15:0] configuration[0:6];
wire phi=phase>=16,ena_pixel=(phase%4)==3,ena_data=phase==15||phase==31;
logic cs=0,we=0;logic[5:0] reg_addr=0;logic[7:0] reg_data=0;wire[3:0]state;wire[8:0]raster;wire[9:0]vc;wire[2:0]rc;wire[7:0]code;wire[13:0]address;
always #5 clk=!clk;always @(negedge clk)phase=(phase+1)%32;
video_vicii_656x #(.registeredAddress(1),.emulateRefresh(1),.emulateLightpen(1),.emulateGraphics(1)) dut(
 .clk(clk),.reset(reset),.phi(phi),.enaData(ena_data),.enaPixel(ena_pixel),.baSync(1'b0),.ba(),.ba_dma(),.mode6569(!ntsc),.mode6567old(1'b0),.mode6567R8(ntsc!=0),.mode6572(1'b0),.turbo_en(1'b0),.turbo_state(),.variant(2'b00),
 .char_rom_bank(configuration[1]!=0),.cs(cs),.we(we),.lp_n(1'b1),.aRegisters(reg_addr),.diRegisters(reg_data),.di(configuration[4][7:0]),.diColor(4'h5),.\do (),.vicAddr(address),.irq_n(),.hSync(),.vSync(),.colorIndex(),.debugX(),.debugY(),.vicRefresh(),.addrValid(),
 .observe_state(state),.observe_raster(raster),.observe_vc(vc),.observe_rc(rc),.observe_code(code));
always @(posedge clk)begin
 if(phase==0)begin
  if(!aligned&&state==0)begin aligned=1;cycle=11;beam=raster;end
  else if(aligned)begin if(cycle==cycles)begin cycle=1;beam=(beam+1)%lines;end else cycle++;end
 end
 if(!reset&&phase==12&&raster==configuration[5]&&cycle>=20&&cycle<=34)begin
  if($isunknown({address,vc,rc,code}))$fatal(1,"unknown fetch state");
  $fdisplay(fd,"%0d,%0d,%0d,%0d,%0d",cycle,address,vc,rc,code);
 end
end
task automatic write_reg(input integer address,input integer value);
 while(phase!=16)@(negedge clk);cs=1;we=1;reg_addr=address;reg_data=value;
 repeat(32)@(negedge clk);cs=0;we=0;repeat(32)@(negedge clk);
endtask
task automatic target(input integer c);while(!(raster==configuration[5]&&cycle==c&&phase==16))@(negedge clk);endtask
initial begin
 $readmemh("case.hex",configuration);ntsc=configuration[0];cycles=ntsc?65:63;lines=ntsc?263:312;
 fd=$fopen("rtl_fetch.csv","w");if(!fd)$fatal(1,"no trace");$fdisplay(fd,"cycle,address,vc,rc,code");
 repeat(64)@(negedge clk);reset=0;write_reg('h11,configuration[2]);write_reg('h18,configuration[3]);
 target(22);write_reg('h11,configuration[2]^'h20);
 target(26);write_reg('h11,configuration[2]);
 target(30);write_reg('h18,configuration[3]^8);
 target(35);$fclose(fd);$display("PASS directed dynamic graphics fetch stimulus completed");$finish;
end
initial begin repeat(3*312*65*32)@(posedge clk);$fatal(1,"timeout");end
endmodule
