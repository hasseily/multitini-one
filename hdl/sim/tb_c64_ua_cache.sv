// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Actual REU cache + actual UA fetcher. HP2 B may precede synthetic visibility
// by 16 clocks, deliberately stressing the independently parameterized guard.
module tb_c64_ua_cache;
    reg clk=0;always #5 clk=~clk;
    reg hard_reset=1,reset=1,paused=0;
    reg [6:0] fetch=0,playing=0,sixteen_bit=0,predict_valid=0;
    reg [167:0] fetch_address=0,predict_address=0;reg [125:0] deadlines=0;
    wire [111:0] samples;wire [6:0] sample_ready;
    wire [83:0] cache_pending_addr;wire [3:0] cache_pending_valid;
    wire [20:0] cache_inflight_write_addr,cache_write_done_addr,cache_write_snoop_addr;
    wire cache_inflight_write_valid,cache_write_done,cache_write_snoop_valid,reu_memory_busy;
    wire [23:0] memory_addr;wire memory_valid,busy,fault,accepted,deferred;
    reg memory_ready=1;reg [63:0] memory_rdata=0;reg memory_rvalid=0,memory_fault=0;
    multitini_c64_ua_fetch ua(.*);
    reg flush=0,invalidate=0,request_write=0,request_valid=0;
    reg [23:0] request_addr=0;reg [63:0] request_wdata=0;reg [7:0] request_wstrb=0;
    wire request_ready,response_valid;wire [63:0] response_rdata;
    wire [23:0] hp2_addr;wire hp2_write,hp2_burst,hp2_valid,cache_fault,invalidate_done;
    wire [63:0] hp2_wdata;wire [7:0] hp2_wstrb;
    reg [63:0] hp2_rdata=0;reg hp2_rvalid=0,hp2_fault=0;
    multitini_c64_reu_cache cache(
        .clk(clk),.reset(hard_reset),.enable(1'b1),.flush(flush),.hint_addr(24'b0),.hint_read(1'b0),
        .request_addr(request_addr),.request_write(request_write),.request_wdata(request_wdata),.request_wstrb(request_wstrb),
        .request_valid(request_valid),.request_ready(request_ready),.response_rdata(response_rdata),.response_valid(response_valid),
        .memory_addr(hp2_addr),.memory_write(hp2_write),.memory_wdata(hp2_wdata),.memory_wstrb(hp2_wstrb),.memory_burst(hp2_burst),
        .memory_valid(hp2_valid),.memory_ready(1'b1),.memory_rdata(hp2_rdata),.memory_rvalid(hp2_rvalid),.memory_fault(hp2_fault),
        .busy(reu_memory_busy),.fault(cache_fault),.invalidate(invalidate),.invalidate_done(invalidate_done),
        .pending_addr(cache_pending_addr),.pending_valid(cache_pending_valid),
        .inflight_write_addr(cache_inflight_write_addr),.inflight_write_valid(cache_inflight_write_valid),
        .write_done(cache_write_done),.write_done_addr(cache_write_done_addr),
        .write_snoop_valid(cache_write_snoop_valid),.write_snoop_addr(cache_write_snoop_addr)
    );
    reg [63:0] ram[0:63];
    reg hp2_pending=0,hp3_pending=0,visible_pending=0;
    reg [23:0] a2,a3,visible_addr;reg w2;reg [63:0] d2,visible_data;reg [7:0] s2,visible_strb;
    integer wait2=0,wait3=0,visibility=0,beat2=0,issues=0,checks=0,i,b,waits;
    always @(posedge clk)begin
        if(!hard_reset)begin
            if(hp2_valid)begin
                if(hp2_pending)$fatal(1,"HP2 multiple outstanding");
                hp2_pending=1;a2=hp2_addr;w2=hp2_write;d2=hp2_wdata;s2=hp2_wstrb;wait2=4;beat2=0;
            end
            if(accepted)begin
                if(hp3_pending)$fatal(1,"HP3 multiple outstanding");
                hp3_pending=1;a3=memory_addr;wait3=4;issues=issues+1;
            end
            if(cache_write_done)begin
                visible_pending=1;visible_addr=a2;visible_data=d2;visible_strb=s2;visibility=16;
            end
        end
    end
    always @(negedge clk)begin
        hp2_rvalid=0;memory_rvalid=0;
        if(hp2_pending)begin
            if(wait2==0)begin
                hp2_rvalid=1;hp2_rdata=ram[(a2[8:3]+beat2)&63];
                if(w2 || beat2==3) hp2_pending=0;
                else begin beat2=beat2+1;wait2=2;end
            end else wait2=wait2-1;
        end
        if(visible_pending)begin
            if(visibility==0)begin
                for(integer lane=0;lane<8;lane=lane+1)if(visible_strb[lane])ram[visible_addr[8:3]][lane*8+:8]=visible_data[lane*8+:8];
                visible_pending=0;
            end else visibility=visibility-1;
        end
        if(hp3_pending)begin
            if(wait3==0)begin memory_rvalid=1;memory_rdata=ram[a3[8:3]];hp3_pending=0;end
            else wait3=wait3-1;
        end
    end
    task step;begin @(posedge clk);#1;@(negedge clk);#1;end endtask
    task get(input [23:0] address,input [15:0] expected);
        begin
            playing=1;fetch_address[23:0]=address;fetch=1;step();fetch=0;
            waits=0;while(!sample_ready[0] && waits<500)begin step();waits=waits+1;end
            if(!sample_ready[0] || samples[15:0]!==expected)$fatal(1,"Coherent sample expected %04h got %04h",expected,samples[15:0]);
            checks=checks+1;
        end
    endtask
    task put(input [23:0] address,input [63:0] value,input [7:0] mask);
        begin
            request_addr=address;request_wdata=value;request_wstrb=mask;request_write=1;request_valid=1;
            #1;
            while(!request_ready)step();step();request_valid=0;request_write=0;
        end
    endtask
    initial begin
        for(i=0;i<64;i=i+1)ram[i]=64'h0102030405060708+i;
        repeat(2)step();hard_reset=0;reset=0;
        get(0,16'h0800);
        // Queued youngest partial word may linger indefinitely. A new sample
        // must not reuse the old resident word, nor read through the queue.
        put(0,64'h00000000000000aa,1);
        fetch_address[23:0]=0;fetch=1;step();fetch=0;
        repeat(50)step();
        if(sample_ready[0])$fatal(1,"Resident hit or read escaped queued write fence");
        flush=1;
        waits=0;while(!sample_ready[0] && waits<500)begin step();waits=waits+1;end
        if(samples[15:0]!==16'haa00)$fatal(1,"Post-B guard allowed stale data");checks=checks+1;
        flush=0;
        // Registered snoop and logical fetch on the same clock: the resident
        // check must reject before the valid bit clears on that edge.
        put(0,64'h00000000000000bb,1);
        fetch_address[23:0]=0;fetch=1;step();fetch=0;
        if(sample_ready[0])$fatal(1,"Same-edge resident snoop missed");
        flush=1;get(0,16'hbb00);flush=0;
        // Fill a real REU read line, modify DDR externally under reset/hold,
        // and require invalidate ACK to remove that resident cached value.
        request_addr=32;request_write=0;request_valid=1;
        while(!request_ready)step();step();request_valid=0;
        if(!cache.valid[1])$fatal(1,"REU read line not primed");
        reset=1;playing=0;while(busy)step();ram[4]=64'h1122334455667788;
        invalidate=1;step();invalidate=0;
        waits=0;while(!invalidate_done && waits<100)begin step();waits=waits+1;end
        if(!invalidate_done || cache.valid!=0)$fatal(1,"Invalidate did not clear resident lines");
        request_valid=1;while(!request_ready)step();
        if(response_rdata!==ram[4])$fatal(1,"Post-invalidate REU read stale");
        step();request_valid=0;reset=0;get(32,16'h8800);
        // Invalidate must drain a partial youngest word before reporting done.
        put(64,64'h00000000000000cc,1);invalidate=1;step();invalidate=0;
        waits=0;while(!invalidate_done && waits<100)begin step();waits=waits+1;end
        if(!invalidate_done || cache_pending_valid || cache_inflight_write_valid)$fatal(1,"Invalidate acknowledged dirty queue");
        // Faults cannot turn a failed invalidate into success.
        hp2_fault=1;step();hp2_fault=0;invalidate=1;step();invalidate=0;
        repeat(50)begin step();if(invalidate_done)$fatal(1,"Faulted invalidate acknowledged");end
        if(!cache_fault || !reu_memory_busy)$fatal(1,"Fault lost dirty ownership");
        $display("PASS: UA real-cache coherence samples=%0d issues=%0d invalidate=3",checks,issues);$finish;
    end
    initial begin #1000000;$fatal(1,"UA cache timeout");end
endmodule
