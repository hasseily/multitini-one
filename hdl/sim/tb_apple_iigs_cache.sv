// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_cache;
    wire [63:0] bus_rword;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,invalidate=0,bus_valid=0,bus_write=0,bus_commit=0,bypass=0;
    wire lookup_tick=1,request_tick=1;
    wire write_launch=1;
    reg [23:0] bus_addr=0;reg [7:0] bus_wdata=0;
    wire [7:0] bus_rdata;wire stall,fault;
    wire [31:0] hits,misses,wait_cycles;
    reg loader_valid=0,loader_write=1;
    reg [23:0] loader_addr=0;reg [63:0] loader_wdata=0;reg [7:0] loader_wstrb=0;
    wire loader_ready,loader_response;wire [63:0] loader_rdata;
    wire [23:0] memory_addr;wire memory_write,memory_valid;
    wire [63:0] memory_wdata;wire [7:0] memory_wstrb;
    reg memory_ready=0,memory_rvalid=0,memory_fault=0;
    reg [63:0] memory_rdata=0;
    multitini_apple_iigs_cache dut(.*);
    reg [7:0] ram[0:32767];
    reg active=0;
    reg [23:0] saved_addr;reg [63:0] saved_data;reg [7:0] saved_strobe;reg saved_write;
    integer delay_count=0,transactions=0,writes=0,cycle=0;
    always @(posedge clk)begin
        cycle<=cycle+1;memory_rvalid<=0;
        memory_ready<=!active && cycle%3!=0;
        if(memory_valid && memory_ready)begin
            if(active)$fatal(1,"overlapping memory requests");
            active<=1;saved_addr<=memory_addr;saved_data<=memory_wdata;
            saved_strobe<=memory_wstrb;saved_write<=memory_write;
            delay_count<=7+transactions%19;transactions<=transactions+1;
        end
        if(active)begin
            if(delay_count>0)delay_count<=delay_count-1;
            else begin
                for(integer lane=0;lane<8;lane=lane+1)begin
                    if(saved_write && saved_strobe[lane])ram[(saved_addr+lane)&32767]<=saved_data[lane*8+:8];
                    memory_rdata[lane*8+:8]<=ram[(saved_addr+lane)&32767];
                end
                if(saved_write)writes<=writes+1;
                memory_rvalid<=1;active<=0;memory_ready<=0;
            end
        end
    end
    task read_byte(input [23:0] a,input [7:0] expected);
        integer n;
        begin
            @(negedge clk);bus_addr=a;bus_write=0;bus_valid=1;
            @(negedge clk);n=0;
            while(stall && n<2000)begin @(negedge clk);n=n+1;end
            if(stall || fault || bus_rdata!==expected)$fatal(1,"read %h got %h expected %h stall=%b",a,bus_rdata,expected,stall);
            bus_commit=1;@(negedge clk);bus_commit=0;bus_valid=0;
        end
    endtask
    task write_byte(input [23:0] a,input [7:0] value);
        integer n,before_writes;
        begin
            @(negedge clk);bus_addr=a;bus_wdata=value;bus_write=1;bus_valid=1;before_writes=writes;
            @(negedge clk);n=0;
            while(stall && n<2000)begin @(negedge clk);n=n+1;end
            if(stall || writes!=before_writes+1 || ram[a&32767]!==value)$fatal(1,"write not acknowledged exactly once");
            // The same held bus may span several parent clocks before PH2.
            repeat(8)@(negedge clk);
            if(writes!=before_writes+1)$fatal(1,"held write duplicated");
            bus_commit=1;@(negedge clk);bus_commit=0;bus_valid=0;
        end
    endtask
    integer first_count;
    initial begin
        for(integer i=0;i<32768;i=i+1)ram[i]=i^8'h5a;
        repeat(3)@(negedge clk);reset=0;
        repeat(4100)@(negedge clk);
        read_byte(24'h001200,8'h5a);first_count=transactions;
        for(integer i=1;i<8;i=i+1)read_byte(24'h001200+i,i^8'h5a);
        if(transactions!=first_count)$fatal(1,"same cache word missed");
        write_byte(24'h001203,8'ha5);read_byte(24'h001203,8'ha5);
        // Direct-mapped conflict, then refill, must return DDR's updated data.
        read_byte(24'h009200,8'h5a);read_byte(24'h001203,8'ha5);
        bypass=1;read_byte(24'h001203,8'ha5);write_byte(24'h001203,8'h31);read_byte(24'h001203,8'h31);bypass=0;
        // Runtime uploads invalidate the cache and wait for all write ACKs.
        @(negedge clk);invalidate=1;@(negedge clk);invalidate=0;
        repeat(4100)@(negedge clk);
        loader_addr=24'h001200;loader_wdata=64'h0807060504030201;loader_wstrb=8'hff;loader_valid=1;
        while(!loader_ready)@(negedge clk);
        @(negedge clk);loader_valid=0;
        while(!loader_response)@(negedge clk);
        @(negedge clk);
        for(integer i=0;i<8;i=i+1)read_byte(24'h001200+i,i+1);
        // Memory faults are latched and prohibit successful bus completion.
        memory_fault=1;@(negedge clk);memory_fault=0;repeat(2)@(negedge clk);
        if(!fault || !stall)$fatal(1,"DDR error not latched");
        $display("PASS: IIgs cache hits, conflicts, coherent writes, bypass, upload, delayed responses and fault hold; transactions=%0d",transactions);
        $finish;
    end
    initial begin #2000000;$fatal(1,"timeout");end
endmodule
