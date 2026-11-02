// Multitini ECS port; reproduced by scripts/port_amiga_core.py.
// Copyright 2021 Alexey Melnikov
//
// This file is part of Minimig
//
// Minimig is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// Minimig is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http:// www.gnu.org/licenses/>.
//
//----------------------------------------------------------------------------------  

module fastchip (
    input wire clk_sys,reset,sel,
    output wire sel_ack,ready,
    input wire [23:0] addr,input wire [15:0] din,
    output wire [15:0] dout,
    input wire lds,uds,rnw,longword,ide_ena,
    output wire ide_irq,output wire [5:0] ide_req,
    input wire [4:0] ide_address,input wire ide_write,ide_read,
    input wire [15:0] ide_writedata,output wire [15:0] ide_readdata,
    output wire ide_led,output wire [5:0] monitor
);
// Strict-tool declaration order; no functional change.
wire sel_ide;
wire sel_gayle;
reg ide_ack;
wire ide_ready;
wire [15:0] ide_dout;
wire        ide_nrdy;
assign sel_ide = ide_ena && sel && addr[23:16] ==  8'b1101_1010;
assign sel_gayle = ide_ena && sel && addr[23:12] == 12'b1101_1110_0001;
assign ide_ready = ide_ack & (sel_ide | sel_gayle) & ~(ide_nrdy & rnw);

// IDE-only extraction. No RTG, Akiko, extra CPU clock or memory interface.
assign sel_ack=sel_ide | sel_gayle;
assign ready=ide_ready;
assign dout=ide_dout;
       //IDE registers at $DA0000 - $DAFFFF	
  //GAYLE registers at $DE1000 - $DE1FFF


always @(posedge clk_sys) ide_ack <= (sel_ide | sel_gayle);






gayle gayle
(
	.clk(clk_sys),
	.reset(reset),

	.addr(addr[23:1]),
	.data_in(din),
	.data_out(ide_dout),
	.rd(rnw & uds),
	.wr(~rnw & uds),
	.sel_ide(sel_ide),
	.sel_gayle(sel_gayle),
	.irq(ide_irq),
	.nrdy(ide_nrdy),
	.longword(longword),

	.monitor(monitor),
	.ide_req(ide_req),
	.ide_address(ide_address),
	.ide_write(ide_write),
	.ide_writedata(ide_writedata),
	.ide_read(ide_read),
	.ide_readdata(ide_readdata),
	
	.led(ide_led)
);


endmodule
