// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_ua_fetch;
    reg clk=0;always #5 clk=~clk;
    reg hard_reset=1,reset=1,paused=0;
    reg [6:0] fetch=0,playing=0,sixteen_bit=0,predict_valid=0;
    reg [167:0] fetch_address=0,predict_address=0;
    reg [125:0] deadlines=0;
    wire [111:0] samples;wire [6:0] sample_ready;
    reg [83:0] cache_pending_addr=0;reg [3:0] cache_pending_valid=0;
    reg [20:0] cache_inflight_write_addr=0,cache_write_done_addr=0,cache_write_snoop_addr=0;
    reg cache_inflight_write_valid=0,cache_write_done=0,cache_write_snoop_valid=0,reu_memory_busy=0;
    wire [23:0] memory_addr;wire memory_valid,busy,fault,accepted,deferred;
    reg memory_ready=1;reg [63:0] memory_rdata=0;reg memory_rvalid=0,memory_fault=0;
    multitini_c64_ua_fetch dut(.*);
    integer cycle=0,last_accept=-100,issues=0,delay_count=0,latency=6;
    reg [23:0] issued_addr;reg [63:0] response_data;
    reg backend_owed=0,override_enable=0;reg [20:0] override_tag=0;reg [63:0] override_data=0;
    integer i,j,start_issues,waits;
    function automatic [7:0] byte_at(input [23:0] address);begin byte_at=address[7:0]^address[15:8]^address[23:16]^8'h51;end endfunction
    function automatic [63:0] data_at(input [23:0] address);
        integer b;begin
            for(b=0;b<8;b=b+1)data_at[b*8+:8]=byte_at(address+b);
            if(override_enable && address[23:3]==override_tag)data_at=override_data;
        end
    endfunction
    always @(posedge clk) begin
        cycle=cycle+1;
        if(accepted && !hard_reset) begin
            if(backend_owed) $fatal(1,"More than one HP3 read outstanding");
            if(cycle-last_accept<32) $fatal(1,"Issue gap violated");
            last_accept=cycle;issues=issues+1;issued_addr=memory_addr;
            response_data=data_at(memory_addr);backend_owed=1;delay_count=latency;
        end
    end
    always @(negedge clk) begin
        memory_rvalid=0;
        if(backend_owed) begin
            if(delay_count==0) begin memory_rvalid=1;memory_rdata=response_data;backend_owed=0;end
            else delay_count=delay_count-1;
        end
    end
    task step;begin @(posedge clk);#1;@(negedge clk);#1;end endtask
    task fresh;
        begin
            reset=1;fetch=0;playing=0;predict_valid=0;cache_pending_valid=0;cache_inflight_write_valid=0;
            cache_write_done=0;cache_write_snoop_valid=0;paused=0;memory_ready=1;override_enable=0;
            while(busy) step();repeat(20)step();hard_reset=1;step();hard_reset=0;reset=0;last_accept=-100;
        end
    endtask
    task request(input integer voice,input [23:0] address,input bit wide);
        begin
            playing[voice]=1;fetch_address[voice*24+:24]=address;sixteen_bit[voice]=wide;fetch[voice]=1;
            step();fetch[voice]=0;
        end
    endtask
    task expect_sample(input integer voice,input [15:0] expected);
        begin
            waits=0;while(!sample_ready[voice] && waits<1500)begin step();waits=waits+1;end
            if(!sample_ready[voice] || samples[voice*16+:16]!==expected)
                $fatal(1,"Sample v%0d expected %04h got %04h ready=%b",voice,expected,samples[voice*16+:16],sample_ready[voice]);
        end
    endtask
    initial begin
        repeat(2)step();hard_reset=0;reset=0;
        // Every byte offset, including a physical 16MiB end-to-zero straddle.
        for(i=0;i<8;i=i+1)begin
            request(0,24'hfffff8+i,1);expect_sample(0,{byte_at((24'hfffff9+i)&24'hffffff),byte_at(24'hfffff8+i)});
        end
        fresh();
        // Coherence fences are physical words, independent of selected voice.
        cache_pending_addr[20:0]=21'h1234;cache_pending_valid=1;
        start_issues=issues;request(0,24'h0091a0,0);repeat(40)step();
        if(issues!=start_issues || sample_ready[0] || !deferred)$fatal(1,"Posted-write fence missed");
        cache_pending_valid=0;cache_inflight_write_addr=21'h1234;cache_inflight_write_valid=1;repeat(20)step();
        if(issues!=start_issues)$fatal(1,"In-flight write fence missed");
        cache_inflight_write_valid=0;cache_write_done_addr=21'h1234;cache_write_done=1;step();cache_write_done=0;
        repeat(15)begin step();if(issues!=start_issues)$fatal(1,"Post-B hold ended early");end
        expect_sample(0,{byte_at(24'h0091a0),8'b0});
        // Sixteen different writes complete on sixteen successive clocks.
        fresh();paused=1;
        for(i=0;i<16;i=i+1)begin cache_write_done=1;cache_write_done_addr=i;step();end
        cache_write_done=0;
        for(i=0;i<16;i=i+1)
            if(!dut.recent_valid[i] || dut.recent_tag[i]!=(15-i))$fatal(1,"Lost dense post-B history slot %0d",i);
        paused=0;request(0,24'd120,0);start_issues=issues;
        repeat(13)begin step();if(issues!=start_issues)$fatal(1,"Newest dense write not fenced");end
        expect_sample(0,{byte_at(120),8'b0});
        // A snoop at the read response edge invalidates the arriving old word.
        fresh();latency=20;request(0,24'h400,0);while(!backend_owed)step();
        override_enable=1;override_tag=21'h80;override_data=64'h123456789abcdef0;
        while(delay_count>1)step();cache_write_snoop_addr=21'h80;cache_write_snoop_valid=1;step();cache_write_snoop_valid=0;
        expect_sample(0,16'hf000);latency=6;
        // Latched sample survives a later write, but the next logical fetch
        // cannot reuse the resident old bytes after the registered snoop.
        override_data=64'h8877665544332211;cache_write_snoop_valid=1;step();cache_write_snoop_valid=0;step();
        if(samples[15:0]!=16'hf000)$fatal(1,"Later write changed a latched sample");
        request(0,24'h400,0);expect_sample(0,16'h1100);
        // Soft reset while a request is backpressured may cancel it. If ready
        // goes high on that reset edge, ownership must survive until response.
        fresh();memory_ready=0;request(0,24'h800,0);while(!memory_valid)step();
        reset=1;memory_ready=1;step();reset=0;
        if(!busy)$fatal(1,"Reset forgot accepted request");
        request(0,24'h900,0);expect_sample(0,{byte_at(24'h900),8'b0});
        // Pause stops issuance and state consumption but drains an owed beat.
        fresh();request(0,24'ha00,0);while(!backend_owed)step();paused=1;repeat(40)step();
        if(busy)$fatal(1,"Pause prevented response drain");
        paused=0;expect_sample(0,{byte_at(24'ha00),8'b0});
        // Deadline ordering selects voice 6 ahead of voice 0 at equal class.
        fresh();deadlines={7{18'd100}};deadlines[6*18+:18]=1;
        fetch_address[23:0]=24'h1000;fetch_address[6*24+:24]=24'h2000;fetch=7'b1000001;playing=fetch;
        step();fetch=0;while(!accepted)step();
        if(memory_addr!=24'h2000)$fatal(1,"EDF tie class ordering failed");
        expect_sample(6,{byte_at(24'h2000),8'b0});expect_sample(0,{byte_at(24'h1000),8'b0});
        // Fault lifetime is hard reset only, and faults remain local.
        memory_fault=1;step();memory_fault=0;reset=1;step();reset=0;repeat(50)step();
        if(!fault || memory_valid)$fatal(1,"Fault was not sticky or issued again");
        $display("PASS: UA fetch coherence=10 offsets=8 issues=%0d",issues);$finish;
    end
    initial begin #1000000;$fatal(1,"UA fetch timeout");end
endmodule
