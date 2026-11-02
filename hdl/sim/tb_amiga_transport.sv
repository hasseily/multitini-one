// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_amiga_transport;
    reg clk=0,control_clk=0;
    always #5 clk=~clk;
    always #7 control_clk=~control_clk;
    reg reset=1,control_resetn=0;
    reg chip_c1=0;
    reg [23:1] chip_addr=0;
    reg [15:0] chip_wdata=0,chip_wdata2=0;
    reg chip_oe_n=1,chip_we_n=1,chip_ub_n=1,chip_lb_n=1,chip_long=0;
    wire [15:0] chip_rdata,chip_rdata2;
    wire [47:0] chip_rdata48;
    reg cpu_req=0;
    reg [28:1] cpu_addr=0;
    reg [15:0] cpu_wdata=0;
    reg [1:0] cpu_state=0;
    reg cpu_ub_n=1,cpu_lb_n=1,cpu_long=0;
    reg [15:0] cpu_wdata2=0;
    wire [15:0] cpu_rdata,cpu_rdata2;
    wire cpu_ready;
    reg load_valid=0;
    reg [23:0] load_addr=0;
    reg [7:0] load_data=0;
    wire load_ready,load_done,run_enable,busy,fault;
    wire [23:0] memory_addr;
    wire memory_write,memory_valid,memory_burst;
    wire [63:0] memory_wdata;
    wire [7:0] memory_wstrb;
    reg memory_ready=1,memory_rvalid=0,memory_fault=0;
    reg [63:0] memory_rdata=0;
    multitini_amiga_memory #(.INDEX_BITS(2)) memory_i(.monitor(),.*);

    reg [63:0] backing[0:2047];
    reg ddr_pending=0;
    integer delay_left=0,ddr_latency=12,ddr_count=0,write_count=0,read_count=0;
    reg [23:0] accepted_addr;
    reg [63:0] accepted_wdata;
    reg [7:0] accepted_mask;
    reg accepted_write,accepted_burst;
    reg [1:0] accepted_beat;
    integer slot,n;
    function integer map_line(input [23:0] address);
        map_line={address[23:21],address[10:3]};
    endfunction
    always @(posedge clk) begin
        memory_rvalid<=0;
        if(reset) begin ddr_pending<=0;ddr_count<=0;write_count<=0;read_count<=0;end
        else begin
            if(memory_valid && memory_ready) begin
                if(ddr_pending)$fatal(1,"overlapping DDR request");
                ddr_pending<=1;delay_left<=ddr_latency;ddr_count<=ddr_count+1;
                accepted_addr<=memory_addr;accepted_wdata<=memory_wdata;
                accepted_mask<=memory_wstrb;accepted_write<=memory_write;
                accepted_burst<=memory_burst;accepted_beat<=0;
                if(memory_addr[2:0]!=0)$fatal(1,"unaligned DDR request");
                if(memory_burst && (memory_write || memory_addr[4:0]!=0))$fatal(1,"line fill must be an aligned read");
                if(memory_write)write_count<=write_count+1;else read_count<=read_count+1;
            end
            if(ddr_pending) begin
                // Posted writes and CPU-only fills never hold native time; a
                // loader write, or a chip fill before its word, always does.
                if(memory_i.owner==2 && run_enable)$fatal(1,"native timeline ran during delayed loader DDR response");
                if(memory_i.owner==0 && chip_c1 && memory_i.chip_seen && !memory_i.chip_done && run_enable)
                    $fatal(1,"native timeline ran while a chip fill was pending");
                if((memory_i.owner==1 || memory_i.owner==3) && !chip_c1 && !load_valid && !fault && !run_enable)
                    $fatal(1,"CPU fill or posted write paused native chipset time");
                if(delay_left!=0) delay_left<=delay_left-1;
                else begin
                    slot=map_line(accepted_addr+{accepted_beat,3'b000});
                    if(accepted_write)
                        for(n=0;n<8;n=n+1)if(accepted_mask[n])backing[slot][n*8+:8]<=accepted_wdata[n*8+:8];
                    memory_rdata<=backing[slot];memory_rvalid<=1;
                    if(accepted_burst && accepted_beat!=2'd3)accepted_beat<=accepted_beat+1'b1;
                    else ddr_pending<=0;
                end
            end
        end
    end
    task tick;begin @(posedge clk);#1;end endtask
    task chip_read(input [23:0] address);
        integer timeout;
        begin
            @(negedge clk);chip_addr=address>>1;chip_c1=1;chip_oe_n=0;chip_we_n=1;
            tick;timeout=0;
            while(busy && timeout<1000)begin tick;timeout=timeout+1;end
            if(timeout==1000)$fatal(1,"chip read timeout");
            @(negedge clk);chip_c1=0;chip_oe_n=1;tick;
        end
    endtask
    task chip_write(input [23:0] address,input [15:0] value,input upper,input lower);
        integer timeout;
        begin
            @(negedge clk);chip_addr=address>>1;chip_wdata=value;chip_c1=1;chip_oe_n=1;chip_we_n=0;chip_ub_n=!upper;chip_lb_n=!lower;
            tick;timeout=0;
            while(busy && timeout<1000)begin tick;timeout=timeout+1;end
            if(timeout==1000)$fatal(1,"chip write timeout");
            @(negedge clk);chip_c1=0;chip_we_n=1;chip_ub_n=1;chip_lb_n=1;tick;
        end
    endtask
    task cpu_access(input [28:0] address,input writing,input [15:0] value);
        integer timeout;
        begin
            @(negedge clk);cpu_addr=address>>1;cpu_wdata=value;cpu_state=writing?3:1;cpu_ub_n=0;cpu_lb_n=0;cpu_req=1;
            tick;timeout=0;
            while(!cpu_ready && timeout<1000)begin tick;timeout=timeout+1;end
            if(timeout==1000)$fatal(1,"CPU request timeout");
            @(negedge clk);cpu_req=0;tick;
        end
    endtask
    task load_byte(input [23:0] address,input [7:0] value);
        integer timeout;
        begin
            @(negedge clk);load_addr=address;load_data=value;load_valid=1;
            tick;timeout=0;
            while(!load_done && timeout<1000)begin tick;timeout=timeout+1;end
            if(timeout==1000)$fatal(1,"loader timeout");
            @(negedge clk);load_valid=0;tick;
        end
    endtask

    reg write=0;
    reg [31:0] command=0;
    wire [31:0] status;
    wire [64:0] native_rtc;
    wire [15:0] native_display_request;
    wire [4:0] display_read_address;
    wire [15:0] display_read_data=0;
    wire [1:0] capture_mode;
    wire display_switch_pending;
    wire display_quiescent=1;
    wire io_fpga,io_uio,io_strobe;
    wire [15:0] io_din;
    reg io_wait=0;
    reg [15:0] io_dout=16'hcafe;
    reg [5:0] ide_req=6'b100101;
    wire [4:0] ide_address;
    wire ide_read,ide_write;
    wire [15:0] ide_wdata;
    reg [15:0] ide_rdata=16'h7654;
    reg host_ticks=1;
    reg [1:0] host_phase=0;
    wire chip_tick=host_ticks;
    always @(posedge clk)host_phase<=host_phase+1'b1;
    multitini_amiga_host host_i(
        .block_write(1'b0),.block_read(1'b0),.block_address(16'd0),.block_read_address(16'd0),
        .block_ram_read_address(7'd0),.block_wstrb(4'd0),.block_rdata(),
        .guest_reset(1'b0),.native_config(16'h0663),.diag_cpu_reset_n(1'b1),
        .diag_cpu_wait(1'b0),.diag_cpu_write(1'b0),.diag_ide(6'd0),.diag_memory(8'd0),.*);
    integer io_pulses=0,ide_reads=0,ide_writes=0;
    reg prior_strobe=0,prior_read=0,prior_write=0;
    always @(posedge clk)begin
        prior_strobe<=io_strobe;prior_read<=ide_read;prior_write<=ide_write;
        if(io_strobe && !prior_strobe)begin
            io_pulses<=io_pulses+1;
            if(io_din!=16'hface)$fatal(1,"UIO data changed");
        end
        if(ide_read && !prior_read)begin
            ide_reads<=ide_reads+1;
            if(ide_address!=31)$fatal(1,"IDE read address truncated");
        end
        if(ide_write && !prior_write)begin
            ide_writes<=ide_writes+1;
            if(ide_address!=17 || ide_wdata!=16'habcd)$fatal(1,"IDE write payload");
        end
    end
    task host_send(input [31:0] value);
        begin @(negedge control_clk);command=value;write=1;@(negedge control_clk);write=0;end
    endtask
    task host_complete;
        integer timeout;
        begin
            timeout=0;
            while(status[31] && timeout<80000)begin tick;timeout=timeout+1;end
            if(timeout==80000)$fatal(1,"host command exceeded bounded completion");
            repeat(4)tick;
        end
    endtask
    task host_op(input [31:0] value);begin host_send(value);host_complete;end endtask

    task request_snapshot(input [5:0] value,input integer phase_offset);
        reg [5:0] previous;
        integer age;
        begin
            previous=status[21:16];
            // Both controller fields change between service sampling edges.
            // Deliberate bit skew can be visible to the asynchronous control
            // clock, but only a complete old/new snapshot may be published.
            @(posedge clk);#(0.2+phase_offset);
            ide_req[2]=value[2];ide_req[5]=value[5];
            #1.5;ide_req=value;
            age=0;
            while(status[21:16]!==value && age<250)begin
                @(negedge control_clk);age=age+1;
                if(status[21:16]!==previous && status[21:16]!==value)
                    $fatal(1,"torn IDE request snapshot old%h new%h got%h",previous,value,status[21:16]);
            end
            if(age==250)$fatal(1,"IDE request snapshot did not become current");
            repeat(60)begin
                @(negedge control_clk);
                if(status[21:16]!==value)$fatal(1,"stable IDE snapshot regressed");
            end
        end
    endtask

    // The DDR model owns n; these checks run while it is responding.
    integer before_reads,before_writes,waited,max_wait,i;
    initial begin
        for(n=0;n<2048;n=n+1)backing[n]=0;
        backing[map_line(24'h000100)]=64'h7766554433221100;
        backing[map_line(24'ha00100)]=64'hfedcba9876543210;
        repeat(5)tick;
        @(negedge clk);reset=0;control_resetn=1;
        repeat(25)tick;
        chip_read(24'h000106);
        if(chip_rdata!==16'h6677 || chip_rdata2!==16'h0011 || chip_rdata48!==48'h001122334455)$fatal(1,"chip burst wrap/endian %h %h %h",chip_rdata,chip_rdata2,chip_rdata48);
        before_reads=ddr_count;
        chip_read(24'h000100);
        if(chip_rdata!==16'h0011 || ddr_count!=before_reads)$fatal(1,"cache hit unexpectedly missed");
        chip_write(24'h000100,16'haabb,1,0);
        chip_read(24'h000100);
        if(chip_rdata!==16'haa11)$fatal(1,"upper big-endian byte lane %h",chip_rdata);
        chip_write(24'h000100,16'hccdd,0,1);
        cpu_access(29'h000100,0,0);
        if(cpu_rdata!==16'haadd)$fatal(1,"CPU/chip coherence %h",cpu_rdata);
        // memory_router encodes Z2 in bit28, with guest bit23 removed.
        cpu_access(29'h10200100,1,16'hbeef);
        cpu_access(29'h10200100,0,0);
        if(cpu_rdata!==16'hbeef)$fatal(1,"Z2 fast RAM write/read");
        cpu_access(29'h10000100,1,16'hca11);
        cpu_access(29'h10000100,0,0);
        if(cpu_rdata!==16'hca11)$fatal(1,"upper Z2 mapping write/read");
        cpu_access(29'h10200100,0,0);
        if(cpu_rdata!==16'hbeef)$fatal(1,"upper Z2 RAM aliased lower Z2 RAM");
        chip_read(24'h000100);
        if(chip_rdata!==16'haadd)$fatal(1,"fast RAM aliased chip RAM");
        cpu_access(29'h000100,1,16'h1234);
        chip_read(24'h000100);
        if(chip_rdata!==16'h1234)$fatal(1,"CPU write left stale chip cache");
        load_byte(24'h000101,8'h56);
        chip_read(24'h000100);
        if(chip_rdata!==16'h1256)$fatal(1,"loader byte lane/cache coherence");
        chip_long=1;chip_wdata2=16'h99aa;
        chip_write(24'h000104,16'h7788,1,1);
        chip_long=0;
        chip_read(24'h000104);
        if(chip_rdata!==16'h7788 || chip_rdata2!==16'h99aa)$fatal(1,"chip longword write endian");
        cpu_long=1;cpu_wdata2=16'h3344;
        cpu_access(29'h10200104,1,16'h1122);
        cpu_access(29'h10200104,0,0);
        if(cpu_rdata!==16'h1122 || cpu_rdata2!==16'h3344)$fatal(1,"CPU longword write/read endian");
        cpu_long=0;
        // TG68K may keep request asserted while moving directly to its next
        // bus transaction. Address/state/data changes must withdraw stale
        // acknowledgement before the next native edge.
        @(negedge clk);cpu_req=1;cpu_addr=29'h000100>>1;cpu_state=1;cpu_wdata=0;
        tick;while(!cpu_ready)tick;
        if(cpu_rdata!==16'h1256)$fatal(1,"continuous CPU first read");
        @(negedge clk);cpu_addr=29'h000104>>1;#1;
        if(cpu_ready)$fatal(1,"changed CPU address retained stale ready");
        tick;while(!cpu_ready)tick;
        if(cpu_rdata!==16'h7788)$fatal(1,"continuous CPU second read");
        @(negedge clk);cpu_state=3;cpu_wdata=16'habcd;#1;
        if(cpu_ready)$fatal(1,"changed CPU state retained stale ready");
        tick;while(!cpu_ready)tick;
        @(negedge clk);cpu_state=1;#1;
        if(cpu_ready)$fatal(1,"write-to-read CPU state retained stale ready");
        tick;while(!cpu_ready)tick;
        if(cpu_rdata!==16'habcd)$fatal(1,"continuous CPU read after write");
        chip_write(24'h000104,16'h7788,1,1);
        while(!cpu_ready)tick;
        if(cpu_rdata!==16'h7788)$fatal(1,"chip DMA left continuous CPU ready stale");
        @(negedge clk);cpu_req=0;tick;
        // Chip and CPU reads precede a simultaneous loader write. A single
        // coherent line must publish the previous bytes, then the new byte.
        @(negedge clk);chip_addr=24'h000100>>1;chip_c1=1;chip_oe_n=0;
        cpu_addr=29'h000102>>1;cpu_state=1;cpu_req=1;cpu_ub_n=0;cpu_lb_n=0;
        load_valid=1;load_addr=24'h000102;load_data=8'h9a;
        tick;
        while(!cpu_ready)tick;
        if(cpu_rdata!==16'h2233)$fatal(1,"loader passed pending CPU read");
        while(!load_done)tick;
        @(negedge clk);chip_c1=0;chip_oe_n=1;cpu_req=0;load_valid=0;tick;
        chip_read(24'h000102);
        if(chip_rdata!==16'h9a33)$fatal(1,"simultaneous client coherence");
        backing[map_line(24'ha001c0)]=64'h8877665544332b1a;
        @(negedge clk);cpu_addr=29'h102001c0>>1;cpu_state=2;cpu_req=1;cpu_ub_n=0;cpu_lb_n=0;
        while(!ddr_pending)tick;
        if(!run_enable)$fatal(1,"CPU miss stopped native time before chip needed RAM");
        // A chip hit during the CPU fill is served without holding time.
        @(negedge clk);chip_addr=24'h000100>>1;chip_c1=1;chip_oe_n=0;#1;
        if(!run_enable)$fatal(1,"new chip request did not permit its first lookup cycle");
        tick;
        if(!run_enable || !memory_i.chip_done || chip_rdata!==16'h1256 || !ddr_pending)
            $fatal(1,"chip hit during CPU fill was held or wrong %h",chip_rdata);
        @(negedge clk);chip_c1=0;chip_oe_n=1;tick;
        // A chip miss during the CPU fill holds native time until it is served.
        backing[map_line(24'h000160)]=64'h0123456789abcdef;
        @(negedge clk);chip_addr=24'h000160>>1;chip_c1=1;chip_oe_n=0;#1;
        if(!run_enable)$fatal(1,"chip miss did not permit its first lookup cycle");
        tick;
        if(run_enable || !ddr_pending)$fatal(1,"chip miss during CPU fill failed to stop native time");
        while(busy)tick;
        if(cpu_rdata!==16'h1a2b || chip_rdata!==16'hefcd)$fatal(1,"CPU/chip miss ordering/data %h %h",cpu_rdata,chip_rdata);
        @(negedge clk);cpu_req=0;chip_c1=0;chip_oe_n=1;tick;
        before_reads=ddr_count;
        memory_ready=0;
        @(negedge clk);chip_addr=24'h000180>>1;chip_c1=1;chip_oe_n=0;
        repeat(12)begin tick;if(run_enable)$fatal(1,"native timeline ran while DDR refused request");end
        if(!memory_valid || ddr_count!=before_reads)$fatal(1,"DDR backpressure request lost");
        memory_ready=1;
        while(busy)tick;
        @(negedge clk);chip_c1=0;chip_oe_n=1;tick;
        // Reset during a delayed transaction must cancel adapter ownership
        // and let a new request proceed without publishing stale data.
        ddr_latency=1000;
        @(negedge clk);chip_addr=24'h0001a0>>1;chip_c1=1;chip_oe_n=0;
        while(!ddr_pending)tick;
        reset=1;chip_c1=0;chip_oe_n=1;repeat(3)tick;
        reset=0;ddr_latency=12;repeat(10)tick;
        chip_read(24'h000100);
        if(chip_rdata!==16'h1256)$fatal(1,"reset during DDR lost backing bytes");
        memory_fault=1;tick;memory_fault=0;tick;
        if(!fault || run_enable)$fatal(1,"memory fault did not hold machine");
        reset=1;repeat(3)tick;reset=0;repeat(25)tick;
        if(fault || !run_enable)$fatal(1,"reset did not recover memory adapter");

        // Posted writes retire on their lookup edge and merge per 64-bit word.
        chip_read(24'h000100);
        if(chip_rdata!==16'h1256)$fatal(1,"line refill after reset");
        ddr_latency=40;before_writes=write_count;
        @(negedge clk);chip_addr=24'h0001e0>>1;chip_wdata=16'h5a5a;chip_c1=1;chip_oe_n=1;chip_we_n=0;chip_ub_n=0;chip_lb_n=0;
        tick;
        if(!memory_i.chip_done || !run_enable)$fatal(1,"chip write waited for DDR");
        @(negedge clk);chip_c1=0;chip_we_n=1;chip_ub_n=1;chip_lb_n=1;tick;
        for(i=0;i<8;i=i+1)begin
            @(negedge clk);cpu_addr=(29'h10200200+2*i)>>1;cpu_wdata=16'h1000+i;cpu_state=3;cpu_ub_n=0;cpu_lb_n=0;cpu_req=1;
            tick;waited=0;
            while(!cpu_ready && waited<4)begin tick;waited=waited+1;end
            if(!cpu_ready)$fatal(1,"posted CPU write waited for DDR");
            @(negedge clk);cpu_req=0;tick;
        end
        // A chip hit is served while posted writes drain.
        if(!ddr_pending || memory_i.owner!=3)$fatal(1,"posted writes were not draining");
        @(negedge clk);chip_addr=24'h000100>>1;chip_c1=1;chip_oe_n=0;tick;
        if(!memory_i.chip_done || !run_enable || chip_rdata!==16'h1256)$fatal(1,"chip hit waited for posted writes");
        @(negedge clk);chip_c1=0;chip_oe_n=1;tick;
        while(busy)tick;
        if(write_count-before_writes!=3)$fatal(1,"CPU stores did not merge per 64-bit word: %0d writes",write_count-before_writes);
        // One burst fills the whole 32-byte line.
        ddr_latency=12;before_reads=read_count;
        for(i=0;i<8;i=i+1)begin
            cpu_access(29'h10200200+2*i,0,0);
            if(cpu_rdata!==16'h1000+i)$fatal(1,"merged CPU write lost at %0d: %h",i,cpu_rdata);
        end
        if(read_count-before_reads!=1)$fatal(1,"32-byte line was not filled by one burst: %0d reads",read_count-before_reads);
        chip_read(24'h0001e0);
        if(chip_rdata!==16'h5a5a)$fatal(1,"posted chip write lost");
        // A read of a line with a buffered write waits for that write.
        ddr_latency=40;
        cpu_access(29'h10200300,1,16'h7777);
        cpu_access(29'h10200300,0,0);
        if(cpu_rdata!==16'h7777)$fatal(1,"line fill passed a buffered write: %h",cpu_rdata);
        while(busy)tick;
        // No write is accepted while a line fill is in flight.
        backing[map_line(24'ha00340)]=64'h0000000000003412;
        @(negedge clk);cpu_addr=29'h10200340>>1;cpu_state=2;cpu_req=1;cpu_ub_n=0;cpu_lb_n=0;
        while(!ddr_pending)tick;
        @(negedge clk);chip_addr=24'h000044>>1;chip_wdata=16'h4444;chip_c1=1;chip_oe_n=1;chip_we_n=0;chip_ub_n=0;chip_lb_n=0;
        tick;tick;
        if(memory_i.chip_done || run_enable)$fatal(1,"write accepted during a line fill");
        while(!memory_i.chip_done)tick;
        if(cpu_rdata!==16'h1234)$fatal(1,"CPU fill data %h",cpu_rdata);
        @(negedge clk);chip_c1=0;chip_we_n=1;chip_ub_n=1;chip_lb_n=1;cpu_req=0;tick;
        while(busy)tick;
        chip_read(24'h000044);
        if(chip_rdata!==16'h4444)$fatal(1,"write held during fill was lost");
        // Distinct words fill the buffer; a further write waits for a drain.
        ddr_latency=30;max_wait=0;
        for(i=0;i<7;i=i+1)begin
            @(negedge clk);cpu_addr=(29'h10200400+8*i)>>1;cpu_wdata=16'h2000+i;cpu_state=3;cpu_ub_n=0;cpu_lb_n=0;cpu_req=1;
            tick;waited=0;
            while(!cpu_ready && waited<1000)begin tick;waited=waited+1;end
            if(waited>max_wait)max_wait=waited;
            @(negedge clk);cpu_req=0;tick;
        end
        if(max_wait<10)$fatal(1,"full write buffer did not apply backpressure");
        while(busy)tick;
        ddr_latency=12;
        for(i=0;i<7;i=i+1)begin
            cpu_access(29'h10200400+8*i,0,0);
            if(cpu_rdata!==16'h2000+i)$fatal(1,"buffered write %0d lost: %h",i,cpu_rdata);
        end
        // Fills publish words carried by beats 1..3 on their own beat.
        ddr_latency=12;
        backing[map_line(24'ha00510)]=64'h0000000000005566;
        @(negedge clk);cpu_addr=29'h10200510>>1;cpu_state=1;cpu_ub_n=0;cpu_lb_n=0;cpu_req=1;
        tick;waited=0;
        while(!cpu_ready && waited<200)begin tick;waited=waited+1;end
        if(!cpu_ready || cpu_rdata!==16'h6655)$fatal(1,"beat-2 CPU word %h",cpu_rdata);
        if(memory_i.state!=2)$fatal(1,"beat-2 CPU word was not published before the fill ended");
        @(negedge clk);cpu_req=0;tick;while(busy)tick;
        backing[map_line(24'h0001b8)]=64'h7788000000000000;
        chip_read(24'h0001be);
        if(chip_rdata!==16'h8877)$fatal(1,"beat-3 chip word %h",chip_rdata);
        // An abandoned CPU fill must not overwrite a newer CPU hit, whether
        // the request was withdrawn or replaced while still asserted.
        backing[map_line(24'ha00620)]=64'h000000000000beba;
        backing[map_line(24'ha00758)]=64'h000000000000dede;
        backing[map_line(24'ha00678)]=64'h000000000000cdab;
        cpu_access(29'h10200620,0,0);
        if(cpu_rdata!==16'hbabe)$fatal(1,"resident line setup %h",cpu_rdata);
        ddr_latency=20;
        for(i=0;i<2;i=i+1)begin
            @(negedge clk);cpu_addr=(i==0 ? 29'h10200758 : 29'h10200678)>>1;cpu_state=1;cpu_ub_n=0;cpu_lb_n=0;cpu_req=1;
            tick;while(!ddr_pending)tick;
            if(i==0)begin @(negedge clk);cpu_req=0;tick;@(negedge clk);cpu_req=1;end
            else @(negedge clk);
            cpu_addr=29'h10200620>>1;
            tick;waited=0;
            while(!cpu_ready && waited<200)begin tick;waited=waited+1;end
            if(!cpu_ready || cpu_rdata!==16'hbabe)$fatal(1,"newer CPU hit during abandoned fill %h",cpu_rdata);
            while(memory_i.state!=0)begin
                tick;
                if(!cpu_ready || cpu_rdata!==16'hbabe)$fatal(1,"abandoned CPU fill overwrote a newer hit (%0d): %h",i,cpu_rdata);
            end
            @(negedge clk);cpu_req=0;tick;while(busy)tick;
        end
        cpu_access(29'h10200758,0,0);
        if(cpu_rdata!==16'hdede)$fatal(1,"abandoned line refill %h",cpu_rdata);
        // A write retiring as its youngest entry launches must not merge into it.
        ddr_latency=12;before_writes=write_count;
        @(negedge clk);chip_addr=24'h0002a0>>1;chip_wdata=16'hab00;chip_c1=1;chip_oe_n=1;chip_we_n=0;chip_ub_n=0;chip_lb_n=1;
        cpu_addr=29'h0002a0>>1;cpu_wdata=16'h00cd;cpu_state=3;cpu_ub_n=1;cpu_lb_n=0;cpu_req=1;
        tick;waited=0;
        while(!(memory_i.chip_done && cpu_ready) && waited<20)begin tick;waited=waited+1;end
        if(!memory_i.chip_done || !cpu_ready)$fatal(1,"simultaneous chip/CPU writes not retired");
        @(negedge clk);chip_c1=0;chip_we_n=1;chip_ub_n=1;cpu_req=0;cpu_lb_n=1;tick;
        while(busy)tick;
        if(write_count-before_writes!=2)$fatal(1,"launching entry merge bookkeeping: %0d writes",write_count-before_writes);
        chip_read(24'h0002a0);
        if(chip_rdata!==16'habcd)$fatal(1,"write merged into a launching entry was lost: %h",chip_rdata);
        // A held, retired CPU write is not repeated after a later DMA write.
        @(negedge clk);cpu_addr=29'h0002c0>>1;cpu_wdata=16'h1111;cpu_state=3;cpu_ub_n=0;cpu_lb_n=0;cpu_req=1;
        tick;while(!cpu_ready)tick;
        chip_write(24'h0002c0,16'h2222,1,1);
        repeat(4)tick;
        @(negedge clk);cpu_req=0;tick;while(busy)tick;
        chip_read(24'h0002c0);
        if(chip_rdata!==16'h2222)$fatal(1,"held CPU write repeated after DMA write: %h",chip_rdata);
        $display("PASS AMIGA memory endian, burst lines, posted merged writes, coherence, Z2 separation, loader, waits and faults");

        request_snapshot(0,0);
        for(n=0;n<8;n=n+1)begin
            request_snapshot(6'b101101,n%5);
            request_snapshot(0,(n+2)%5);
        end

        host_op(32'h00000001);
        if(!io_fpga || io_uio || status[30])$fatal(1,"FPGA selection");
        host_op(32'h0100face);
        if(status[15:0]!=16'hcafe || io_pulses!=1 || io_strobe)$fatal(1,"FPGA transfer result/pulse");
        host_op(32'h00000002);
        if(io_fpga || !io_uio)$fatal(1,"UIO selection");
        io_wait=1;host_send(32'h0100face);repeat(100)tick;
        if(!status[31])$fatal(1,"IO_WAIT completed prematurely");
        io_wait=0;host_complete;
        if(status[30] || status[15:0]!=16'hcafe)$fatal(1,"IO_WAIT release");
        host_op(32'h021f0000);
        if(ide_reads!=1 || status[15:0]!=16'h7654)$fatal(1,"IDE read result");
        host_op(32'h0311abcd);
        if(ide_writes!=1)$fatal(1,"IDE write pulse missing");
        io_wait=1;host_send(32'h0100face);repeat(15)tick;host_send(32'h0311abcd);repeat(15)tick;
        if(!status[30])$fatal(1,"busy collision unreported");
        io_wait=0;host_complete;
        if(ide_writes!=1)$fatal(1,"busy collision executed extra command");
        host_op(32'h04000000);
        if(status[30])$fatal(1,"sticky error clear");
        host_op(32'h00000003);
        if(!status[30] || io_uio || io_fpga)$fatal(1,"dual selection accepted");
        host_op(32'h04000000);
        host_op(32'h0100face);
        if(!status[30])$fatal(1,"unselected transfer accepted");
        host_op(32'h04000000);host_op(32'h00000001);
        io_wait=1;host_send(32'h0100face);host_complete;
        if(!status[30] || io_uio || io_fpga)$fatal(1,"IO_WAIT timeout not reported/deselected");
        io_wait=0;host_op(32'h04000000);host_op(32'h00000001);
        host_ticks=0;host_send(32'h0100face);host_complete;
        if(!status[30] || io_strobe || ide_write || ide_read)$fatal(1,"stopped timeline timeout not recovered");
        $display("PASS AMIGA host coherent6-bit IDE snapshots,selections,IO_WAIT,IDE,collisions andbounded timeouts");
        $finish;
    end
    initial begin #20000000;$fatal(1,"transport watchdog");end
endmodule
