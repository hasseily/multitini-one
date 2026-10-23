// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_irq;
logic clk=0, reset=1; integer phase=0,ntsc=0,cycle=11,beam=0,cycles,lines,fd; logic aligned=0; logic[7:0] mode_file[0:0];
wire phi=phase>=16,ena_pixel=(phase%4)==3,ena_data=phase==15||phase==31;
logic cs=0,we=0;logic [5:0] reg_addr=0;logic[7:0] reg_data=0;wire[3:0] state;wire[8:0] raster;wire irst,irq_n;
always #5 clk=!clk;
always @(negedge clk)phase=(phase+1)%32;
video_vicii_656x #(.registeredAddress(1),.emulateRefresh(1),.emulateLightpen(1),.emulateGraphics(1)) dut(
 .clk(clk),.reset(reset),.phi(phi),.enaData(ena_data),.enaPixel(ena_pixel),.baSync(1'b0),.ba(),.ba_dma(),
 .mode6569(!ntsc),.mode6567old(1'b0),.mode6567R8(ntsc!=0),.mode6572(1'b0),.turbo_en(1'b0),.turbo_state(),.variant(2'b00),
 .cs(cs),.we(we),.lp_n(1'b1),.aRegisters(reg_addr),.diRegisters(reg_data),.di(8'hA5),.diColor(4'h5),.\do (),
 .vicAddr(),.irq_n(irq_n),.hSync(),.vSync(),.colorIndex(),.debugX(),.debugY(),.vicRefresh(),.addrValid(),
 .observe_state(state),.observe_raster(raster),.observe_irst(irst));
always @(posedge clk)if(phase==0)begin
 if(!aligned&&state==0)begin aligned=1;cycle=11;beam=raster;end
 else if(aligned)begin if(cycle==cycles)begin cycle=1;beam=(beam+1)%lines;end else cycle++;end
end
task automatic write_reg(input integer address,input integer value);
 while(phase!=16)@(negedge clk);cs=1;we=1;reg_addr=address;reg_data=value;
 repeat(32)@(negedge clk);cs=0;we=0;repeat(32)@(negedge clk);
endtask
task automatic sample(input integer stage);
 if($isunknown({raster,irq_n,irst}))$fatal(1,"unknown IRQ state");
 $fdisplay(fd,"%0d,%0d,%0d,%0d",stage,raster,!irq_n,irst);
 $display("IRQ_SAMPLE mode=%0d stage=%0d raster=%0d IRQ=%0d IRST=%0d",ntsc,stage,raster,!irq_n,irst);
endtask
initial begin
 $readmemh("mode.hex",mode_file);ntsc=mode_file[0];cycles=ntsc?65:63;lines=ntsc?263:312;
 fd=$fopen("rtl_irq.csv","w");if(!fd)$fatal(1,"no trace");$fdisplay(fd,"stage,line,irq,irst");
 repeat(64)@(negedge clk);reset=0;
 write_reg('h11,0);write_reg('h12,254);write_reg('h1a,1);write_reg('h19,15);
 while(!(raster==80&&cycle==10&&phase==16))@(negedge clk);
 write_reg('h12,80);sample(0);
 write_reg('h19,1);sample(1);
 write_reg('h12,81);sample(2);
 write_reg('h12,80);sample(3);
 write_reg('h19,1);sample(4);
 write_reg('h12,80);sample(5);
 write_reg('h11,'h80);sample(6);
 write_reg('h11,0);sample(7);
 write_reg('h1a,0);sample(8);
 write_reg('h19,1);sample(9);
 write_reg('h1a,1);sample(10);
 write_reg('h12,81);
 while(!(raster==81&&cycle==10&&phase==16))@(negedge clk);sample(11);
 $fclose(fd);$display("PASS directed IRQ stimulus completed");$finish;
end
initial begin repeat(3*312*65*32)@(posedge clk);$fatal(1,"timeout");end
endmodule
