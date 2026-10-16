// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Cache unit test for memory interface v2: posted merging writes, 32-byte
// burst fills, drain-before-read ordering, bypass, loader and fault hold,
// the one-cycle retire slot, read-after-write and fault drain.
module tb_apple_iigs_cache;
    wire [63:0] bus_rword;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,invalidate=0,bus_valid=0,bus_write=0,bus_commit=0,commit_staged=0,bypass=0;
    // phased=1 models HDD DMA: lookup on phase1, request on phase2, retire
    // on the following phase0 after the bus has already moved on.
    reg phased=0;reg [1:0] phase=0;
    wire lookup_tick=!phased || phase==1;
    wire request_tick=!phased || phase==2;
    wire write_launch=1;
    reg [23:0] bus_addr=0;reg [7:0] bus_wdata=0;
    wire [7:0] bus_rdata;wire stall,fault;
    wire [31:0] hits,misses,wait_cycles;
    reg loader_valid=0,loader_write=1;
    reg [23:0] loader_addr=0;reg [63:0] loader_wdata=0;reg [7:0] loader_wstrb=0;
    wire loader_ready,loader_response;wire [63:0] loader_rdata;
    wire [23:0] memory_addr;wire memory_write,memory_valid,memory_burst,memory_busy;
    wire [63:0] memory_wdata;wire [7:0] memory_wstrb;
    reg memory_ready=0,memory_rvalid=0,memory_fault=0;
    reg [63:0] memory_rdata=0;
    multitini_apple_iigs_cache dut(.*);
    reg [7:0] ram[0:65535],expected[0:65535];
    reg active=0;
    reg [23:0] saved_addr;reg [63:0] saved_data;reg [7:0] saved_strobe;reg saved_write;
    integer delay_count=0,beats_left=0,transactions=0,writes=0,reads=0,bursts=0,cycle=0,slow=0;
    always @(posedge clk)phase<=phase+1'b1;
    always @(posedge clk)begin
        cycle<=cycle+1;memory_rvalid<=0;
        memory_ready<=!active && cycle%3!=0;
        if(memory_valid && memory_ready)begin
            if(active)$fatal(1,"overlapping memory requests");
            if(memory_burst && (memory_write || memory_addr[4:0]!=0))$fatal(1,"invalid burst request %h",memory_addr);
            if(!memory_burst && memory_addr[2:0]!=0)$fatal(1,"unaligned request %h",memory_addr);
            if(!memory_write && dut.wb_count!=0)$fatal(1,"read issued before posted writes drained");
            if(memory_write && dut.owner_loader==0 && memory_addr>=24'h800000)$fatal(1,"CPU write outside fast RAM");
            active<=1;saved_addr<=memory_addr;saved_data<=memory_wdata;
            saved_strobe<=memory_wstrb;saved_write<=memory_write;beats_left<=memory_burst ? 4 : 1;
            delay_count<=(slow ? 40 : 7)+transactions%19;transactions<=transactions+1;
            if(!memory_write)reads<=reads+1;
            if(memory_burst)bursts<=bursts+1;
        end
        if(active)begin
            if(delay_count>0)delay_count<=delay_count-1;
            else begin
                for(integer lane=0;lane<8;lane=lane+1)begin
                    if(saved_write && saved_strobe[lane])ram[(saved_addr+lane)&65535]<=saved_data[lane*8+:8];
                    memory_rdata[lane*8+:8]<=ram[(saved_addr+lane)&65535];
                end
                if(saved_write)writes<=writes+1;
                memory_rvalid<=1;
                if(beats_left>1)begin
                    // Later beats may follow immediately or after idle cycles.
                    beats_left<=beats_left-1;saved_addr<=saved_addr+8;delay_count<=(transactions+beats_left)%3;
                end else begin active<=0;memory_ready<=0;end
            end
        end
    end
    task read_byte(input [23:0] a);
        integer n;
        begin
            @(negedge clk);bus_addr=a;bus_write=0;bus_valid=1;
            @(negedge clk);n=0;
            while(stall && n<4000)begin @(negedge clk);n=n+1;end
            if(stall || fault || bus_rdata!==expected[a&65535])
                $fatal(1,"read %h got %h expected %h stall=%b",a,bus_rdata,expected[a&65535],stall);
            bus_commit=1;@(negedge clk);bus_commit=0;bus_valid=0;
        end
    endtask
    integer last_wait;
    task write_byte(input [23:0] a,input [7:0] value);
        integer n;
        begin
            @(negedge clk);bus_addr=a;bus_wdata=value;bus_write=1;bus_valid=1;#1;
            n=0;
            while(stall && n<4000)begin @(negedge clk);n=n+1;end
            if(stall || fault)$fatal(1,"write %h never accepted",a);
            last_wait=n;
            // Retire on the accepting cycle, like a fast CPU edge.
            bus_commit=1;expected[a&65535]=value;@(negedge clk);bus_commit=0;bus_valid=0;bus_write=0;
        end
    endtask
    // A native-path write is accepted on one cycle and retired a few later.
    task held_write_byte(input [23:0] a,input [7:0] value);
        integer n;
        begin
            @(negedge clk);bus_addr=a;bus_wdata=value;bus_write=1;bus_valid=1;
            @(negedge clk);n=0;
            while(stall && n<4000)begin @(negedge clk);n=n+1;end
            if(stall)$fatal(1,"held write never accepted");
            repeat(3)@(negedge clk);
            bus_commit=1;expected[a&65535]=value;@(negedge clk);bus_commit=0;bus_valid=0;bus_write=0;
        end
    endtask
    // HDD DMA style: valid for phase1/2, stall sampled at phase2, moved at
    // phase3, retired by commit_staged at phase0. Retries a stalled window.
    integer dma_retries=0;
    task dma_write_byte(input [23:0] a,input [7:0] value);
        reg done;
        begin
            done=0;
            while(!done)begin
                while(phase!=1)@(negedge clk);
                bus_addr=a;bus_wdata=value;bus_write=1;bus_valid=1;
                @(negedge clk);
                if(phase!=2)$fatal(1,"phase model");
                done=!stall;
                if(!done)dma_retries=dma_retries+1;
                @(negedge clk);
                bus_addr=24'h00fff0;bus_wdata=~value;bus_write=0;bus_valid=0;
                @(negedge clk);
                if(done)begin commit_staged=1;bus_commit=1;expected[a&65535]=value;end
                @(negedge clk);commit_staged=0;bus_commit=0;
            end
        end
    endtask
    task dma_read_byte(input [23:0] a);
        reg done;integer n;
        begin
            done=0;n=0;
            while(!done)begin
                while(phase!=1)@(negedge clk);
                bus_addr=a;bus_write=0;bus_valid=1;
                @(negedge clk);
                done=!stall;
                if(done && bus_rdata!==expected[a&65535])$fatal(1,"DMA read %h got %h",a,bus_rdata);
                @(negedge clk);@(negedge clk);
                if(done)bus_commit=1;
                bus_valid=0;
                @(negedge clk);bus_commit=0;n=n+1;
                if(n>2000)$fatal(1,"DMA read never completed");
            end
        end
    endtask
    // A write retires and the very next cycle reads back (CPU back to back).
    // The first cycles after retirement must stall rather than hit stale data.
    task write_then_read(input [23:0] wa,input [7:0] value,input [23:0] ra);
        integer n;
        begin
            @(negedge clk);bus_addr=wa;bus_wdata=value;bus_write=1;bus_valid=1;#1;
            n=0;while(stall && n<4000)begin @(negedge clk);#1;n=n+1;end
            if(stall || fault)$fatal(1,"write %h never accepted",wa);
            bus_commit=1;expected[wa&65535]=value;
            @(negedge clk);bus_commit=0;bus_addr=ra;bus_write=0;bus_valid=1;#1;
            if(ra[23:3]==wa[23:3] && !stall)$fatal(1,"same-word read hit while the retire slot was pending");
            n=0;while(stall && n<4000)begin @(negedge clk);#1;n=n+1;end
            if(stall || bus_rdata!==expected[ra&65535])
                $fatal(1,"read %h right after write %h got %h expected %h",ra,wa,bus_rdata,expected[ra&65535]);
            bus_commit=1;@(negedge clk);bus_commit=0;bus_valid=0;
        end
    endtask
    // Consecutive-cycle writes, each retired on its accepting cycle.
    integer b2b_stalls;
    task back_to_back_writes(input [23:0] base,input integer count,input [7:0] seed);
        integer k,w;
        begin
            for(k=0;k<count;k=k+1)begin
                @(negedge clk);bus_commit=0;bus_addr=base+k;bus_wdata=seed+k;bus_write=1;bus_valid=1;#1;
                w=0;while(stall && w<4000)begin @(negedge clk);#1;w=w+1;b2b_stalls=b2b_stalls+1;end
                if(stall || fault)$fatal(1,"back-to-back write %0d stuck",k);
                bus_commit=1;expected[(base+k)&65535]=seed+k;
            end
            @(negedge clk);bus_commit=0;bus_valid=0;bus_write=0;
        end
    endtask
    task drain;
        integer n;
        begin
            n=0;
            while(memory_busy && n<20000)begin @(negedge clk);n=n+1;end
            @(negedge clk);
            if(memory_busy || dut.wb_count!=0 || dut.state!=0)$fatal(1,"posted writes did not drain");
        end
    endtask
    task check_ram;
        begin
            for(integer i=0;i<65536;i=i+1)if(ram[i]!==expected[i])
                $fatal(1,"DDR byte %h holds %h expected %h",i,ram[i],expected[i]);
        end
    endtask
    integer first_count,tx_before,before_reads,before_writes,stalls,i,max_wait;
    initial begin
        for(i=0;i<65536;i=i+1)begin ram[i]=(i*7)^(i>>8);expected[i]=ram[i];end
        repeat(3)@(negedge clk);reset=0;
        if(memory_busy)$fatal(1,"busy after reset");
        repeat(1100)@(negedge clk);
        // One 4-beat burst serves the whole 32-byte line.
        tx_before=transactions;read_byte(24'h001200);first_count=transactions;
        if(first_count!=tx_before+1 || bursts!=1 || misses!=1)$fatal(1,"line miss was not one burst");
        for(i=1;i<32;i=i+1)read_byte(24'h001200+i);
        if(transactions!=first_count)$fatal(1,"same 32-byte line missed");
        if(hits<31)$fatal(1,"hit counter");
        // Posted write: retires before DDR sees it and reads back from L1.
        before_writes=writes;write_byte(24'h001203,8'ha5);
        if(writes!=before_writes)$fatal(1,"write was not posted");
        if(!memory_busy)$fatal(1,"memory_busy low with a posted write");
        tx_before=transactions;read_byte(24'h001203);
        if(dut.misses!=1)$fatal(1,"write-update read missed");
        drain();
        if(writes!=before_writes+1 || ram[24'h1203]!==8'ha5)$fatal(1,"posted write reached DDR %0d times",writes-before_writes);
        // Merge: eight bytes of one word while the first is in flight.
        slow=1;before_writes=writes;
        for(i=0;i<8;i=i+1)write_byte(24'h001208+i,8'h10+i);
        for(i=0;i<8;i=i+1)read_byte(24'h001208+i);
        drain();slow=0;
        if(writes!=before_writes+2)$fatal(1,"same-word writes not merged: %0d DDR writes",writes-before_writes);
        check_ram();
        // Back-pressure: slow DDR, ten distinct words; the buffer holds four
        // plus one in flight, so later writes must wait for space.
        slow=1;before_writes=writes;max_wait=0;
        for(i=0;i<10;i=i+1)begin
            write_byte(24'h002000+i*8,8'h80+i);
            if(last_wait>max_wait)max_wait=last_wait;
        end
        if(max_wait<20)$fatal(1,"full buffer did not back-pressure (%0d)",max_wait);
        // Drain before miss: a non-resident line written then read.
        write_byte(24'h003005,8'h3c);
        before_reads=reads;read_byte(24'h003005);read_byte(24'h003004);
        if(reads!=before_reads+1)$fatal(1,"drain-before-miss read count");
        slow=0;drain();
        if(writes!=before_writes+11)$fatal(1,"distinct writes lost or duplicated: %0d",writes-before_writes);
        check_ram();
        // Held (native) retirement accepted early still enqueues once.
        before_writes=writes;held_write_byte(24'h001210,8'h66);read_byte(24'h001210);drain();
        if(writes!=before_writes+1)$fatal(1,"held write duplicated");
        // Retire slot: back-to-back writes, then immediate same-word and
        // same-line reads must never return pre-write data.
        read_byte(24'h001220);b2b_stalls=0;before_writes=writes;
        back_to_back_writes(24'h001220,16,8'h40);
        if(b2b_stalls!=0)$fatal(1,"back-to-back writes stalled with buffer space (%0d)",b2b_stalls);
        for(i=0;i<16;i=i+1)read_byte(24'h001220+i);
        write_then_read(24'h001224,8'h9a,24'h001224);
        write_then_read(24'h001225,8'h9b,24'h00122d);
        write_then_read(24'h003100,8'h9c,24'h003100);
        drain();check_ram();
        // Direct-mapped conflict, then refill, must return DDR's updated data.
        read_byte(24'h009200);read_byte(24'h001203);read_byte(24'h001210);
        write_byte(24'h009201,8'h77);read_byte(24'h009201);read_byte(24'h001201);read_byte(24'h009201);
        // Bypass keeps single-beat reads, one per completed read, and posts writes.
        bypass=1;
        tx_before=transactions;before_reads=reads;read_byte(24'h001203);
        if(reads!=before_reads+1 || transactions!=tx_before+1 || memory_burst)$fatal(1,"bypass read not single beat");
        write_byte(24'h001203,8'h31);read_byte(24'h001203);
        before_reads=reads;for(i=0;i<4;i=i+1)read_byte(24'h001204+i);
        if(reads!=before_reads+4)$fatal(1,"bypass reads per completed read: %0d",reads-before_reads);
        bypass=0;
        // L1 stayed coherent through bypass-mode writes.
        read_byte(24'h001203);read_byte(24'h001200);drain();check_ram();
        // HDD DMA style phase-qualified writes and reads.
        phased=1;before_writes=writes;
        for(i=0;i<40;i=i+1)dma_write_byte(24'h004000+i,8'hc0+i);
        for(i=0;i<40;i=i+1)dma_read_byte(24'h004000+i);
        for(i=0;i<8;i=i+1)dma_write_byte(24'h001208+i,8'he0+i);
        for(i=0;i<8;i=i+1)dma_read_byte(24'h001208+i);
        if(dma_retries!=0)$fatal(1,"DMA write waited with buffer space");
        // The last DMA byte retires on phase0. In that same cycle the CPU's
        // held read is back on the bus with lookup and request forced high,
        // and the lookup register still holds the pre-DMA word. It must
        // stall, then return the DMA byte.
        phased=0;read_byte(24'h001238);drain();phased=1;
        while(phase!=1)@(negedge clk);
        bus_addr=24'h001238;bus_wdata=8'h5e;bus_write=1;bus_valid=1;
        @(negedge clk);if(phase!=2 || stall)$fatal(1,"last DMA byte not accepted");
        @(negedge clk);
        @(negedge clk);if(phase!=0)$fatal(1,"phase model");
        phased=0;bus_addr=24'h001238;bus_write=0;bus_valid=1;commit_staged=1;bus_commit=1;expected[16'h1238]=8'h5e;#1;
        if(!stall)$fatal(1,"CPU read hit during the staged DMA commit, data %h",bus_rdata);
        @(negedge clk);commit_staged=0;bus_commit=0;#1;
        i=0;while(stall && i<4000)begin @(negedge clk);#1;i=i+1;end
        if(stall || bus_rdata!==8'h5e)$fatal(1,"CPU read after the last DMA byte got %h",bus_rdata);
        bus_commit=1;@(negedge clk);bus_commit=0;bus_valid=0;phased=1;
        // Count only the next section's DMA writes.
        drain();
        // A full buffer holds DMA at its PH2 sample; the byte retries intact.
        slow=1;before_writes=writes;
        for(i=0;i<12;i=i+1)dma_write_byte(24'h006000+i*8,8'h90+i);
        if(dma_retries==0)$fatal(1,"full buffer did not hold DMA");
        for(i=0;i<12;i=i+1)dma_read_byte(24'h006000+i*8);
        slow=0;
        if(writes!=before_writes+12)$fatal(1,"DMA writes lost or duplicated: %0d DDR writes, retries %0d",writes-before_writes,dma_retries);
        phased=0;
        for(i=0;i<8;i=i+1)read_byte(24'h001208+i);
        drain();check_ram();
        // Uploads queue behind posted writes.
        slow=1;
        for(i=0;i<4;i=i+1)write_byte(24'h005000+i*8,8'h50+i);
        loader_addr=24'hfc0000;loader_wdata=64'h0807060504030201;loader_wstrb=8'hff;loader_valid=1;
        @(negedge clk);
        if(loader_ready)$fatal(1,"loader ready with posted writes pending");
        while(!loader_ready)@(negedge clk);
        if(dut.wb_count!=0 || ram[24'h5018]!==8'h53 || dut.state!=0)$fatal(1,"loader accepted before posted writes drained");
        @(negedge clk);loader_valid=0;
        while(!loader_response)@(negedge clk);
        @(negedge clk);
        for(i=0;i<8;i=i+1)expected[(24'hfc0000+i)&65535]=i+1;
        // An upload is visible through the cache after the loader's sweep,
        // including into a line that was resident before the upload.
        @(negedge clk);invalidate=1;@(negedge clk);invalidate=0;repeat(1100)@(negedge clk);
        for(i=0;i<8;i=i+1)read_byte(24'hfc0000+i);
        loader_addr=24'hfc0000;loader_wdata=64'h1817161514131211;loader_wstrb=8'hff;loader_valid=1;
        while(!loader_ready)@(negedge clk);
        @(negedge clk);loader_valid=0;
        while(!loader_response)@(negedge clk);
        @(negedge clk);
        for(i=0;i<8;i=i+1)expected[(24'hfc0000+i)&65535]=8'h11+i;
        @(negedge clk);invalidate=1;@(negedge clk);invalidate=0;repeat(1100)@(negedge clk);
        for(i=0;i<8;i=i+1)read_byte(24'hfc0000+i);
        if(ram[0]!==8'h11)$fatal(1,"second upload did not reach DDR");
        // Invalidate never drops posted writes.
        before_writes=writes;
        for(i=0;i<4;i=i+1)write_byte(24'h005100+i*8,8'h60+i);
        @(negedge clk);invalidate=1;@(negedge clk);invalidate=0;
        drain();slow=0;
        if(writes!=before_writes+4)$fatal(1,"invalidate dropped posted writes");
        check_ram();
        repeat(1100)@(negedge clk);
        for(i=0;i<4;i=i+1)begin read_byte(24'h005000+i*8);read_byte(24'h005100+i*8);end
        drain();check_ram();
        // Memory faults are latched and prohibit successful bus completion.
        // The held machine's posted writes are dropped so memory_busy clears
        // once the transaction in flight completes (quiescence still works).
        slow=1;
        for(i=0;i<4;i=i+1)write_byte(24'h005200+i*8,8'h70+i);
        if(!memory_busy || dut.wb_count==0)$fatal(1,"no posted writes before the fault");
        memory_fault=1;@(negedge clk);memory_fault=0;repeat(2)@(negedge clk);
        if(!fault || !stall)$fatal(1,"DDR error not latched");
        i=0;while(memory_busy && i<4000)begin @(negedge clk);i=i+1;end
        if(memory_busy || dut.wb_count!=0 || dut.rs_valid)$fatal(1,"memory_busy stuck after a fault");
        if(!fault || !stall)$fatal(1,"fault not sticky");
        slow=0;
        $display("PASS: IIgs cache bursts, posted merging writes, back-pressure, drain-before-read, DMA staging, bypass, upload and fault hold; transactions=%0d writes=%0d bursts=%0d misses=%0d",
            transactions,writes,bursts,misses);
        $finish;
    end
    initial begin #20000000;$fatal(1,"timeout");end
endmodule
