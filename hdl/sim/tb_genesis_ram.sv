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
    reg [255:0] previous_q=0;
    wire main_done,z80_done;
    vram_ip dut(.*);
    genesis_ram_check #(.ADDRESS_WIDTH(15),.DATA_WIDTH(16)) main_check(clock,main_done);
    genesis_ram_check #(.ADDRESS_WIDTH(13),.DATA_WIDTH(8)) z80_check(clock,z80_done);
    task automatic cycle;
        #1;
        if(q!==previous_q)$fatal(1,"VRAM changed before clock edge");
        @(posedge clock);#1;
        if(wren) for(integer byte_index=0;byte_index<32;byte_index=byte_index+1)
            if(byteena[byte_index])reference_memory[address][byte_index*8+:8]=data[byte_index*8+:8];
        if(q!==reference_memory[address])$fatal(1,"VRAM write-first/byte-lane mismatch at %h",address);
        previous_q=q;
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
        for(integer trial=0;trial<512;trial=trial+1)begin
            address=$random;byteena=$random;wren=trial%3!=0;
            for(integer lane=0;lane<32;lane=lane+1)data[lane*8+:8]=$random;
            cycle;
        end
        wren=0;
        for(integer row=255;row>=0;row=row-1)begin address=row;cycle;end
        wait(main_done && z80_done);
        $display("PASS Genesis VRAM all rows, independent byte enables and synchronous write-first output");
        $finish;
    end
endmodule

module genesis_ram_check #(
    parameter integer ADDRESS_WIDTH=15,
    parameter integer DATA_WIDTH=16
)(input wire clock,output reg done=0);
    localparam integer DEPTH=1<<ADDRESS_WIDTH;
    reg [ADDRESS_WIDTH-1:0] address=0,initialize_address=0;
    reg [DATA_WIDTH-1:0] data=0,initialize_data=0;
    reg [DATA_WIDTH/8-1:0] byte_enable=0;
    reg write_enable=0,initialize=0;
    wire [DATA_WIDTH-1:0] q;
    reg [DATA_WIDTH-1:0] reference_memory[0:DEPTH-1];
    reg [DATA_WIDTH-1:0] previous_q;
    multitini_genesis_ram #(.ADDRESS_WIDTH(ADDRESS_WIDTH),.DATA_WIDTH(DATA_WIDTH)) dut(
        .clk(clock),.address(address),.data(data),.byte_enable(byte_enable),
        .write_enable(write_enable),.q(q),.initialize(initialize),
        .initialize_address(initialize_address),.initialize_data(initialize_data));
    task automatic cycle;
        reg [DATA_WIDTH-1:0] expected;
        #1;
        if(q!==previous_q)$fatal(1,"Chip RAM output changed before clock edge, width=%0d",DATA_WIDTH);
        expected=reference_memory[address];
        @(posedge clock);#1;
        if(q!==expected)$fatal(1,"Chip RAM read-first mismatch width=%0d address=%h expected=%h got=%h",
                               DATA_WIDTH,address,expected,q);
        if(initialize)reference_memory[initialize_address]=initialize_data;
        else if(write_enable)
            for(integer lane=0;lane<DATA_WIDTH/8;lane=lane+1)
                if(byte_enable[lane])reference_memory[address][lane*8+:8]=data[lane*8+:8];
        previous_q=q;
        @(negedge clock);
    endtask
    initial begin
        @(negedge clock);
        // A complete initialization sweep, including a conflicting runtime
        // write, exercises the separate initialization address and priority.
        initialize=1;write_enable=1;byte_enable='1;data='1;
        for(integer row=0;row<DEPTH;row=row+1)begin
            initialize_address=row;initialize_data=row^16'h51a7;
            address=row==0 ? 0 : row-1;cycle;
        end
        initialize=0;write_enable=0;
        for(integer row=DEPTH-1;row>=0;row=row-1)begin address=row;cycle;end
        for(integer trial=0;trial<1024;trial=trial+1)begin
            address=$random;data=$random;byte_enable=$random;write_enable=trial%3!=0;
            initialize=trial%7==0;
            initialize_address=trial%2==0 ? address : ADDRESS_WIDTH'($random);
            initialize_data=$random;
            cycle;
            // Read back the just-written address on the following clock.
            if(initialize)address=initialize_address;
            initialize=0;write_enable=0;cycle;
        end
        $display("PASS Genesis %0d-bit chip RAM complete initialization, byte writes, read-first collisions and one-cycle reads",DATA_WIDTH);
        done=1;
    end
endmodule
