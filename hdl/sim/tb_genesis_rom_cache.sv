// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_genesis_rom_cache;
    reg clk=0,resetn=0,invalidate=0,loading=0;
    always #5 clk=~clk;
    reg [23:0] read_addr=0;reg read_valid=0;
    wire read_ready,cached;wire [15:0] cached_data,read_data;wire read_response;
    reg [23:0] write_addr=0;reg [63:0] write_data=0;reg [7:0] write_strobe=0;reg write_valid=0;
    wire write_ready,write_response;
    wire [23:0] memory_addr;wire memory_write;wire [63:0] memory_wdata;wire [7:0] memory_wstrb;
    wire memory_valid;reg memory_ready=0;reg [63:0] memory_rdata=0;reg memory_rvalid=0,memory_fault=0;
    wire fault;wire [31:0] misses,wait_cycles;
    reg [7:0] memory[0:4095];
    integer cycle=0,pending=0,requests=0,responses=0;
    reg [63:0] response_word=0;
    reg held=0;reg [96:0] held_request;
    multitini_genesis_rom_cache #(.LINES(4)) dut(.*);
    function automatic [7:0] expected_byte(input integer address);
        expected_byte=address<4096 ? memory[address] : 8'(address^(address>>8)^8'h51);
    endfunction
    function automatic [15:0] expected_word(input integer address);
        expected_word={expected_byte(address),expected_byte(address+1)};
    endfunction
    always @(posedge clk)begin
        cycle<=cycle+1;memory_ready<=cycle%4!=0;memory_rvalid<=0;
        if(resetn && held && memory_valid &&
           held_request!={memory_addr,memory_write,memory_wstrb,memory_wdata})$fatal(1,"request changed under backpressure");
        held<=memory_valid && !memory_ready;
        held_request<={memory_addr,memory_write,memory_wstrb,memory_wdata};
        if(memory_valid && memory_ready)begin
            if(pending!=0)$fatal(1,"overlapping memory requests");
            if(memory_addr[2:0]!=0 || memory_addr>=24'h800000)$fatal(1,"invalid PSRAM address/prefetch");
            pending<=6;requests<=requests+1;
            for(integer b=0;b<8;b=b+1)begin
                response_word[b*8+:8]<=expected_byte(integer'(memory_addr)+b);
                if(memory_write && memory_wstrb[b] && memory_addr<4096)
                    memory[memory_addr+b]<=memory_wdata[b*8+:8];
            end
        end else if(pending>0)begin
            pending<=pending-1;
            if(pending==1)begin memory_rvalid<=1;memory_rdata<=response_word;end
        end
        if(read_response)responses<=responses+1;
    end
    task automatic read_word(input integer address);
        begin
            @(negedge clk);while(!read_ready)@(negedge clk);
            read_addr=24'(address);read_valid=1;
            @(negedge clk);read_valid=0;
            wait(read_response);@(negedge clk);
            if(read_data!==expected_word(address))$fatal(1,"bad read address%0h got%0h expected%0h",address,read_data,expected_word(address));
        end
    endtask
    task automatic settle;
        begin repeat(40)@(negedge clk);end
    endtask
    integer saved_misses,saved_responses;
    initial begin
        for(integer i=0;i<4096;i=i+1)memory[i]=8'(i^(i>>8)^8'ha7);
        repeat(4)@(negedge clk);resetn=1;
        read_word(4);settle();
        saved_misses=misses;
        read_word(6);
        if(misses!=saved_misses || !cached || cached_data!=expected_word(6))$fatal(1,"cache hit missed or byte order changed");
        read_word(32);settle();
        read_addr=4;#1;if(cached)$fatal(1,"direct-mapped collision retained wrong tag");
        // Invalidate an accepted demand while its old result is delayed. The
        // old response must drain, and a new demand must fetch changed bytes.
        @(negedge clk);read_addr=128;read_valid=1;
        @(negedge clk);read_valid=0;
        wait(pending>0);@(negedge clk);
        saved_responses=responses;invalidate=1;
        for(integer i=128;i<136;i=i+1)memory[i]=8'h3c;
        @(negedge clk);invalidate=0;
        settle();
        if(responses!=saved_responses)$fatal(1,"aborted demand leaked a response");
        read_word(128);settle();
        loading=1;#1;
        if(read_ready || cached)$fatal(1,"cache lookup enabled during ROM upload");
        @(negedge clk);invalidate=1;@(negedge clk);invalidate=0;
        while(!write_ready)@(negedge clk);
        write_addr=64;write_data=64'h8070605040302010;write_strobe=8'h55;write_valid=1;
        @(negedge clk);write_valid=0;
        if(write_response)$fatal(1,"write acknowledged before physical completion");
        wait(write_response);@(negedge clk);loading=0;
        read_word(64);read_word(66);read_word(68);read_word(70);settle();
        read_word(24'h7ffffe);settle();
        if(fault || misses<4 || wait_cycles==0)$fatal(1,"cache diagnostics");
        @(negedge clk);read_addr=1;read_valid=1;
        @(negedge clk);read_valid=0;
        if(!fault || read_ready || cached)$fatal(1,"odd CPU address was accepted");
        resetn=0;repeat(3)@(negedge clk);resetn=1;settle();
        write_addr=3;write_strobe=8'hff;write_valid=1;
        @(negedge clk);write_valid=0;
        if(!fault || memory_valid)$fatal(1,"unaligned write reached memory");
        resetn=0;repeat(3)@(negedge clk);resetn=1;
        memory_fault=1;@(negedge clk);memory_fault=0;
        if(!fault || read_ready || write_ready)$fatal(1,"physical memory fault did not fail closed");
        $display("PASS Genesis ROM cache: misses/hits, tags, byte strobes, invalidation drain and faults");
        $finish;
    end
    initial begin #200000;$fatal(1,"ROM cache timeout");end
endmodule
