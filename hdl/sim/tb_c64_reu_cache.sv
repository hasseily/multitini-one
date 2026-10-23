// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Cache/store correctness only. The separate native regression establishes
// DMA cadence. This independent acknowledged-memory model snapshots a burst
// at acceptance and applies writes only at completion, exposing forwarding.
module tb_c64_reu_cache;
    reg clk=0;
    always #5 clk=~clk;
    reg reset=1,enable=0,flush=0;
    reg [23:0] hint_addr=0,request_addr=0;
    reg hint_read=0,request_write=0,request_valid=0;
    reg [63:0] request_wdata=0;
    reg [7:0] request_wstrb=0;
    wire request_ready,response_valid,busy,fault;
    wire [63:0] response_rdata,memory_wdata;
    wire [23:0] memory_addr;
    wire memory_write,memory_burst,memory_valid;
    wire [7:0] memory_wstrb;
    wire memory_ready,memory_rvalid,memory_fault;
    wire [63:0] memory_rdata;
    multitini_c64_reu_cache dut(.*);

    localparam integer WORDS=8192;
    reg [63:0] ram[0:WORDS-1];
    reg [63:0] expected[0:WORDS-1];
    reg [63:0] burst_data[0:3];
    reg active=0,active_write=0;
    reg [23:0] active_addr=0;
    reg [63:0] active_data=0;
    reg [7:0] active_mask=0;
    integer beat=0,delay_left=0;
    reg hold_request=0,hold_response=0,random_stalls=1,zero_latency=0;
    reg inject_fault=0,force_fault=0;
    reg [31:0] random_state=32'h65108726;
    integer writes=0,reads=0,responses=0,logical_writes=0,logical_reads=0;
    integer same_edge_writes=0,checks=0,random_ops=0,instant_responses=0;
    wire instant=zero_latency && memory_valid && memory_ready;
    wire delayed=active && delay_left==0 && !hold_response;
    assign memory_ready=!active && !hold_request && (!random_stalls || random_state[1:0]!=0);
    assign memory_rvalid=instant || delayed;
    assign memory_rdata=instant ? ram[memory_addr[15:3]] : burst_data[beat];
    assign memory_fault=force_fault || (inject_fault && memory_rvalid);

    task automatic check(input bit okay,input string reason);
        begin checks=checks+1;if(okay !== 1'b1)$fatal(1,"cache check: %s",reason);end
    endtask
    function automatic [63:0] merged(input [63:0] before_value,new_value,input [7:0] mask);
        integer j;
        begin
            merged=before_value;
            for(j=0;j<8;j=j+1)if(mask[j])merged[8*j+:8]=new_value[8*j+:8];
        end
    endfunction
    integer j,read_index;
    reg stalled=0;
    reg [97:0] held_payload;
    wire [97:0] payload={memory_addr,memory_write,memory_wdata,memory_wstrb,memory_burst};
    always @(posedge clk) begin
        random_state<={random_state[30:0],random_state[31]^random_state[21]^random_state[1]^random_state[0]};
        if(reset) begin
            // The physical bridge separately proves orphan drain. This model
            // drops the response to its reset requester, without clearing RAM.
            active<=0;stalled<=0;delay_left<=0;beat<=0;
        end else begin
            if(stalled && !memory_fault)
                check(memory_valid && payload===held_payload,"request payload held under backpressure");
            stalled<=memory_valid && !memory_ready;
            held_payload<=payload;
            if(memory_valid && memory_ready) begin
                check(!active,"only one backing transaction");
                check(memory_addr[2:0]==0,"backing word aligned");
                check(memory_write ? !memory_burst && memory_wstrb!=0 : memory_burst && memory_addr[4:0]==0,
                      "single strobed store or four-beat aligned fill");
                active_write<=memory_write;active_addr<=memory_addr;
                active_data<=memory_wdata;active_mask<=memory_wstrb;
                if(memory_write) begin
                    writes=writes+1;
                    if(instant) begin
                        if(!memory_fault)ram[memory_addr[15:3]]=merged(ram[memory_addr[15:3]],memory_wdata,memory_wstrb);
                        active<=0;
                    end else active<=1;
                end else begin
                    reads=reads+1;active<=1;
                    read_index=memory_addr[15:3];
                    burst_data[0]<=ram[read_index];
                    burst_data[1]<=ram[read_index+1];
                    burst_data[2]<=ram[read_index+2];
                    burst_data[3]<=ram[read_index+3];
                end
                beat<=instant ? 1 : 0;
                delay_left<=random_stalls ? random_state[4:2] : 0;
            end
            if(active && delay_left!=0)delay_left<=delay_left-1;
            if(delayed) begin
                if(active_write) begin
                    if(!memory_fault)ram[active_addr[15:3]]=merged(ram[active_addr[15:3]],active_data,active_mask);
                    active<=0;
                end else if(beat==3)begin active<=0;beat<=0;end
                else begin beat<=beat+1;delay_left<=random_stalls ? random_state[6:5] : 0;end
            end
            if(memory_rvalid)responses=responses+1;
            if(instant)instant_responses=instant_responses+1;
            if(request_valid && request_ready) begin
                check(response_valid,"logical completion accompanies acceptance");
                if(request_write) begin
                    expected[request_addr[15:3]]=merged(expected[request_addr[15:3]],request_wdata,request_wstrb);
                    logical_writes=logical_writes+1;
                    if(memory_rvalid && !memory_write)same_edge_writes=same_edge_writes+1;
                end else begin
                    if(response_rdata!==expected[request_addr[15:3]])
                        $fatal(1,"readback mismatch addr=%h expected=%h got=%h",request_addr,expected[request_addr[15:3]],response_rdata);
                    logical_reads=logical_reads+1;
                end
            end
        end
    end

    task automatic access(input bit wr,input [23:0] addr,input [63:0] value,input [7:0] mask);
        integer timeout;bit done;
        begin
            @(negedge clk);request_addr=addr;request_write=wr;request_wdata=value;
            request_wstrb=mask;request_valid=1;done=0;timeout=0;
            while(!done && timeout<3000)begin
                @(posedge clk);done=request_ready===1'b1;timeout=timeout+1;
            end
            check(done,"logical transaction eventually accepted");
            @(negedge clk);request_valid=0;
        end
    endtask
    task automatic rd(input [23:0] addr);begin access(0,addr,0,0);end endtask
    task automatic wr(input [23:0] addr,input [63:0] value,input [7:0] mask);begin access(1,addr,value,mask);end endtask
    task automatic drain;
        integer timeout;
        begin
            @(negedge clk);flush=1;hint_read=0;timeout=0;
            while((busy || active) && timeout<3000)begin @(negedge clk);timeout=timeout+1;end
            check(!busy && !active,"all accepted stores drain");
            for(integer k=0;k<WORDS;k=k+1)
                if(ram[k]!==expected[k])$fatal(1,"durable store mismatch word=%0d expected=%h got=%h",k,expected[k],ram[k]);
            flush=0;
        end
    endtask
    task automatic hard_reset;
        begin
            @(negedge clk);reset=1;enable=0;flush=0;hint_read=0;request_valid=0;
            hold_request=0;hold_response=0;inject_fault=0;force_fault=0;
            repeat(3)@(negedge clk);
            for(integer k=0;k<WORDS;k=k+1)expected[k]=ram[k];
            reset=0;repeat(2)@(negedge clk);
            check(!fault && !busy,"hard reset clears logical state and fault");
        end
    endtask
    task automatic await_active_read;
        integer timeout;
        begin
            timeout=0;
            while((!active || active_write) && timeout<200)begin @(negedge clk);timeout=timeout+1;end
            check(active && !active_write,"prefetch started");
        end
    endtask

    integer before_writes,before_reads,timeout,index,seed=32'h1234567;
    reg [23:0] address;
    reg [63:0] value;
    reg [7:0] mask;
    initial begin
        for(integer k=0;k<WORDS;k=k+1) begin
            ram[k]=64'hc064872600000000 ^ (64'h102030405*k);expected[k]=ram[k];
        end
        hard_reset();enable=1;
        // Cold reads, hits and a direct-mapped eviction.
        rd(24'h000100);before_reads=reads;rd(24'h000108);rd(24'h000118);
        check(reads==before_reads,"resident words need no additional fill");
        rd(24'h000180);rd(24'h000100);

        // Coalesce partial bytes; guest disable is a drain request and must
        // preserve data. Flush models soft reset / command completion.
        before_writes=writes;
        wr(24'h000200,64'h11,8'h01);wr(24'h000200,64'h2200,8'h02);
        repeat(8)@(negedge clk);
        check(writes==before_writes && busy,"partial youngest store remains available for merging");
        enable=0;drain();check(writes==before_writes+1,"merged store uses one backing write");
        rd(24'h000200);enable=1;

        // Fill snapshot predates these writes. Overlay older and newer entries
        // for one word, then include a write on the exact returning beat.
        hold_response=1;hint_addr=24'h000300;hint_read=1;await_active_read();hint_read=0;
        wr(24'h000300,64'h11,8'h01);wr(24'h000308,64'h2200,8'h02);
        wr(24'h000300,64'h330000,8'h04);
        @(negedge clk);hold_response=0;#1;
        timeout=0;
        while(!(memory_rvalid && !active_write && beat==0) && timeout<200)begin @(negedge clk);#1;timeout=timeout+1;end
        check(timeout<200,"returning first fill beat observed");
        request_addr=24'h000300;request_write=1;request_wdata=64'h44000000;
        request_wstrb=8'h08;request_valid=1;
        @(posedge clk);check(request_ready,"same-beat forwarded store accepted");
        @(negedge clk);request_valid=0;
        rd(24'h000300);rd(24'h000308);drain();
        check(same_edge_writes>0,"same-edge fill/write collision exercised");

        // A write to a word already received while later fill beats are held
        // must update it; the eventual line-valid edge must not lose it.
        hold_response=1;hint_addr=24'h000400;hint_read=1;await_active_read();hint_read=0;
        hold_response=0;
        while(beat==0)@(negedge clk);
        hold_response=1;wr(24'h000400,64'h55,8'h01);
        hold_response=0;rd(24'h000400);drain();

        // One blocked physical store plus four queued words. A merge remains
        // admissible when full, but a fifth distinct queued word must wait.
        hold_request=1;
        for(integer k=0;k<5;k=k+1)wr(24'h000500+8*k,64'h90+k,8'h01);
        wr(24'h000520,64'ha500,8'h02);
        @(negedge clk);request_addr=24'h000528;request_write=1;request_wdata=64'hbe;
        request_wstrb=8'h01;request_valid=1;
        repeat(6)begin @(posedge clk);check(!request_ready && !response_valid,"full queue applies backpressure");end
        @(negedge clk);hold_request=0;
        timeout=0;
        while(request_ready!==1'b1 && timeout<200)begin @(negedge clk);timeout=timeout+1;end
        check(timeout<200,"queue resumes after backing progress");
        @(posedge clk);@(negedge clk);request_valid=0;drain();
        for(integer k=0;k<6;k=k+1)rd(24'h000500+8*k);

        // A miss cannot overtake a pending write, including an eviction of a
        // resident updated line. Direct readback is scored from acceptance.
        wr(24'h000600,64'hdeadbeef,8'h0f);rd(24'h000600);
        wr(24'h000600,64'h7654321000000000,8'hf0);rd(24'h000680);rd(24'h000600);drain();

        // Next-line prefetch wraps at the full 16 MiB boundary.
        hint_addr=24'hffffe0;hint_read=1;
        timeout=0;before_reads=reads;
        while((reads<before_reads+2 || active) && timeout<300)begin @(negedge clk);timeout=timeout+1;end
        hint_read=0;check(timeout<300,"last line and wrapped next line prefetched");
        before_reads=reads;rd(24'hfffff8);rd(24'h000000);
        check(reads==before_reads,"wrapped prefetch serves both hits");

        // Zero-cycle completion exercises SEND+ready+rvalid, followed by the
        // other three beats. Directed traffic also includes fixed REU words.
        zero_latency=1;random_stalls=0;
        rd(24'h000700);wr(24'h000700,64'hf1e2d3c4b5a69788,8'hff);drain();rd(24'h000700);
        check(instant_responses>=2,"zero-latency read and write acknowledgements exercised");
        zero_latency=0;random_stalls=1;

        for(integer k=0;k<800;k=k+1)begin
            index=$unsigned($random(seed))%128;address=24'h001000+8*index;
            value={$random(seed),$random(seed)};mask=($unsigned($random(seed))%255)+1;
            if(($unsigned($random(seed))%3)!=0)wr(address,value,mask);else rd(address);
            random_ops=random_ops+1;
            if(k%41==0)drain();
        end
        drain();for(integer k=0;k<128;k=k+1)rd(24'h001000+8*k);

        // A final failed store completion must remain busy/faulted, even with
        // no queued entries: firmware cannot treat lost data as quiescent.
        hard_reset();enable=1;inject_fault=1;
        wr(24'h000800,64'habcdef,8'hff);
        timeout=0;while(!fault && timeout<200)begin @(negedge clk);timeout=timeout+1;end
        check(fault,"write completion fault latched");inject_fault=0;
        repeat(8)@(negedge clk);
        check(busy && !request_ready && !memory_valid,"failed final store blocks clean quiescence");
        // Recovery is hard reset only; the failed data was never committed.
        hard_reset();rd(24'h000800);

        // Fault during a fill drains responses but never publishes a hit.
        hard_reset();enable=1;inject_fault=1;hint_addr=24'h000900;hint_read=1;
        timeout=0;while(!fault && timeout<200)begin @(negedge clk);timeout=timeout+1;end
        check(fault,"fill fault latched");inject_fault=0;hint_read=0;
        repeat(50)@(negedge clk);
        request_addr=24'h000900;request_write=0;
        check(busy && !request_ready,"faulted fill cannot become readable or quiescent");
        hard_reset();rd(24'h000900);

        // A reset while a request is backpressured discards only logical
        // state. Accepted physical transactions are covered by DDR bridge TB.
        enable=1;hold_request=1;wr(24'h000a00,64'h1234,8'hff);
        repeat(5)@(negedge clk);check(memory_valid && busy,"pending store existed at hard reset");
        hard_reset();rd(24'h000a00);drain();

        $display("PASS: C64 REU cache random_ops=%0d logical_reads=%0d logical_writes=%0d physical_reads=%0d physical_writes=%0d same_edge_writes=%0d instant_responses=%0d checks=%0d",
            random_ops,logical_reads,logical_writes,reads,writes,same_edge_writes,instant_responses,checks);
        $finish;
    end
    initial begin #3000000;$fatal(1,"cache regression timeout");end
endmodule
