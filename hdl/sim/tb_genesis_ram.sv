// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_genesis_ram;
    reg clock=0;always #5 clock=~clock;
    reg [7:0] address=0;
    reg [31:0] byteena=0;
    reg [255:0] data=0;
    reg wren=0;
    wire [255:0] q;
    reg [255:0] reference_memory[0:255];
    vram_ip dut(.*);
    task automatic cycle;
        @(posedge clock);#1;
        if(wren) for(integer byte_index=0;byte_index<32;byte_index=byte_index+1)
            if(byteena[byte_index])reference_memory[address][byte_index*8+:8]=data[byte_index*8+:8];
        if(q!==reference_memory[address])$fatal(1,"VRAM write-first/byte-lane mismatch at %h",address);
        @(negedge clock);
    endtask
    initial begin
        for(integer row=0;row<256;row=row+1)reference_memory[row]=0;
        @(negedge clock);
        for(integer row=0;row<256;row=row+1)begin
            address=row;
            for(integer byte_index=0;byte_index<32;byte_index=byte_index+1)begin
                byteena=32'b1<<byte_index;
                data={32{row[7:0]^byte_index[7:0]}};
                wren=1;cycle;
            end
        end
        wren=0;
        for(integer row=255;row>=0;row=row-1)begin address=row;cycle;end
        $display("PASS Genesis VRAM all rows, independent byte enables and synchronous write-first output");
        $finish;
    end
endmodule
