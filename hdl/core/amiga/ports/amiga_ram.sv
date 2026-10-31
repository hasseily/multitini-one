// SPDX-License-Identifier: GPL-3.0-only
// Original Multitini portable implementations of the documented RAM interfaces.
// No Intel megafunction source is included. Single-clock RAM, write-first on
// the writing port; mixed-port same-address reads return old data.
module dpram #(parameter addr_width=8, data_width=8, mem_init_file=" ") (
 input wire clock,
 input wire [addr_width-1:0] address_a,
 input wire [data_width-1:0] data_a=0,
 input wire enable_a=1'b1, wren_a=1'b0,
 output wire [data_width-1:0] q_a,
 input wire cs_a=1'b1,
 input wire [addr_width-1:0] address_b=0,
 input wire [data_width-1:0] data_b=0,
 input wire enable_b=1'b1, wren_b=1'b0,
 output wire [data_width-1:0] q_b,
 input wire cs_b=1'b1
);
 (* ram_style="block" *) reg [data_width-1:0] mem[0:(1<<addr_width)-1];
 reg [data_width-1:0] qa,qb;
 integer i;
 initial begin
  for(i=0;i<(1<<addr_width);i=i+1) mem[i]=0;
  qa=0; qb=0;
  if(mem_init_file!=" " && mem_init_file!="") $readmemh(mem_init_file,mem);
 end
 always @(posedge clock) if(enable_a) begin
  qa<=mem[address_a];
  if(wren_a && cs_a) begin mem[address_a]<=data_a; qa<=data_a; end
 end
 always @(posedge clock) if(enable_b) begin
  qb<=mem[address_b];
  if(wren_b && cs_b) begin mem[address_b]<=data_b; qb<=data_b; end
 end
 assign q_a=cs_a ? qa : {data_width{1'b1}};
 assign q_b=cs_b ? qb : {data_width{1'b1}};
endmodule

module denise_colortable_ram_mf (
 input wire [3:0] byteena_a,
 input wire clock,
 input wire [31:0] data,
 input wire enable,
 input wire [7:0] rdaddress,wraddress,
 input wire wren,
 output reg [31:0] q
);
 (* ram_style="block" *) reg [31:0] mem[0:255];
 integer i,b;
 initial begin for(i=0;i<256;i=i+1) mem[i]=0; q=0; end
 always @(posedge clock) if(enable) begin
  q<=mem[rdaddress];
  for(b=0;b<4;b=b+1) if(wren && byteena_a[b]) mem[wraddress][8*b+:8]<=data[8*b+:8];
 end
endmodule
