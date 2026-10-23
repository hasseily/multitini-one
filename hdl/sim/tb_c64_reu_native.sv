// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Native-slot regression for the production REU, cache and HP2 DDR bridge.
// The AXI slave is an independent 16 MiB store with bounded delayed replies.
// MAME charges one native cycle for stash/fetch/verify, two for swap, and
// pauses while BA is low. No real DDR service-time or board claim is made.
module tb_c64_reu_native #(
    parameter integer ADDRESS_BITS=24,
    parameter integer NTSC=0,
    parameter integer LONG_LATENCY=0
);
    localparam [23:0] MASK=ADDRESS_BITS==19 ? 24'h07ffff : 24'hffffff;
    localparam integer LATENCY_LIMIT=LONG_LATENCY ? 96 : 64;
    reg clk=0; always #(NTSC ? 15.263 : 15.858) clk=!clk;
    reg hard_reset=1,reset=1,enable=1;
    reg io_valid=0,io_write=0,ff00_write=0;
    reg [4:0] io_addr=0; reg [7:0] io_wdata=0;
    wire [7:0] io_rdata;
    wire dma_req,dma_valid,dma_write,irq,busy,fault;
    wire [15:0] dma_addr; wire [7:0] dma_wdata;
    wire [23:0] memory_addr,memory_hint_addr;
    wire memory_write,memory_valid,memory_hint_read,memory_ready,memory_rvalid;
    wire [63:0] memory_wdata,memory_rdata; wire [7:0] memory_wstrb;
    wire memory_fault;
    reg [4:0] phase=0; integer cycles=0,frames=0;
    reg slot_valid=0;
    reg active_case=0,holes=0,negative_gap=0;
    integer first_frame=0,accepted=0,total_slots=0,op=0;
    wire ba=!active_case || !holes || !((frames-first_frame<2) ||
        ((frames-first_frame)%17==9) || ((frames-first_frame)%17==10));
    wire dma_ack=phase==31 && slot_valid && dma_req && ba &&
        !(negative_gap && active_case && accepted==3);
    reg [7:0] c64[0:65535];
    wire [7:0] dma_rdata=c64[dma_addr];
    multitini_c64_reu #(.ADDRESS_BITS(ADDRESS_BITS)) reu(.*);

    wire [23:0] ddr_addr; wire ddr_write,ddr_valid,ddr_burst,ddr_busy;
    wire [63:0] ddr_wdata,ddr_rdata; wire [7:0] ddr_wstrb;
    wire ddr_ready,ddr_rvalid,ddr_fault,cache_fault,quiescent;
    multitini_c64_reu_cache #(.ADDRESS_BITS(ADDRESS_BITS)) cache(
        .clk(clk),.reset(hard_reset),.enable(enable&&!reset),.flush(!busy||reset||!enable),
        .hint_addr(memory_hint_addr),.hint_read(memory_hint_read),
        .request_addr(memory_addr),.request_write(memory_write),.request_wdata(memory_wdata),
        .request_wstrb(memory_wstrb),.request_valid(memory_valid),.request_ready(memory_ready),
        .response_rdata(memory_rdata),.response_valid(memory_rvalid),
        .memory_addr(ddr_addr),.memory_write(ddr_write),.memory_wdata(ddr_wdata),
        .memory_wstrb(ddr_wstrb),.memory_valid(ddr_valid),.memory_burst(ddr_burst),
        .memory_ready(ddr_ready),.memory_rdata(ddr_rdata),.memory_rvalid(ddr_rvalid),
        .memory_fault(ddr_fault),.busy(ddr_busy),.fault(cache_fault));
    assign memory_fault=ddr_fault||cache_fault;
    wire [31:0] araddr,awaddr; wire [3:0] arlen,awlen;
    wire [2:0] arsize,awsize; wire [1:0] arburst,awburst;
    wire arvalid,rready,awvalid,wvalid,wlast,bready;
    wire [63:0] wdata; wire [7:0] wstrb;
    reg arready=0,awready=0,wready=0,rvalid=0,rlast=0,bvalid=0;
    reg [63:0] rdata=0; reg [1:0] rresp=0,bresp=0;
    multitini_c64_ddr_path bridge(
        .core_clk(clk),.core_resetn(!hard_reset),.control_clk(clk),.control_resetn(!hard_reset),
        .control_reset_request(reset),.core_reset_request(reset),
        .core_memory_addr(ddr_addr),.core_memory_write(ddr_write),
        .core_memory_wdata(ddr_wdata),.core_memory_wstrb(ddr_wstrb),
        .core_memory_valid(ddr_valid),.core_memory_burst(ddr_burst),.core_memory_busy(ddr_busy),
        .core_memory_ready(ddr_ready),.core_memory_rdata(ddr_rdata),
        .core_memory_rvalid(ddr_rvalid),.core_memory_fault(ddr_fault),.quiescent(quiescent),
        .araddr(araddr),.arlen(arlen),.arsize(arsize),.arburst(arburst),.arvalid(arvalid),.arready(arready),
        .rdata(rdata),.rresp(rresp),.rlast(rlast),.rvalid(rvalid),.rready(rready),
        .awaddr(awaddr),.awlen(awlen),.awsize(awsize),.awburst(awburst),.awvalid(awvalid),.awready(awready),
        .wdata(wdata),.wstrb(wstrb),.wlast(wlast),.wvalid(wvalid),.wready(wready),
        .bresp(bresp),.bvalid(bvalid),.bready(bready));

    // A default address-dependent pattern makes every bank/lane distinguishable.
    // Written bits select actual storage rather than initializing 16 MiB of RAM.
    reg [63:0] ddr[0:2097151]; reg [2097151:0] written=0;
    reg [7:0] expected[0:16777215]; reg [16777215:0] expected_written=0;
    function automatic [7:0] pattern(input [23:0] a);
        pattern=8'hc7 ^ a[7:0] ^ {a[11:8],a[15:12]} ^ a[23:16];
    endfunction
    function automatic [63:0] ddr_word(input [23:0] a);
        integer j; begin
            if(written[a[23:3]]) ddr_word=ddr[a[23:3]];
            else for(j=0;j<8;j=j+1)ddr_word[j*8+:8]=pattern({a[23:3],3'd0}+j);
        end
    endfunction
    function automatic [7:0] reference_byte(input [23:0] a);
        reference_byte=expected_written[a] ? expected[a] : pattern(a);
    endfunction
    task automatic reference_write(input [23:0] a,input [7:0] value);
        expected[a]=value; expected_written[a]=1;
    endtask
    reg read_active=0,have_aw=0,have_w=0;
    reg [23:0] read_addr=0,write_addr=0;
    reg [63:0] write_data=0; reg [7:0] write_strobe=0;
    integer read_delay=0,read_beat=0,read_len=0,write_delay=0;
    integer axi_reads=0,axi_writes=0;
    reg inject_read_fault=0,inject_write_fault=0,hold_response=0;
    always @(posedge clk)begin : axi_model
        reg [63:0] value;
        arready<=!read_active&&!have_aw&&!have_w&&!bvalid&&(cycles%5!=1);
        awready<=!read_active&&!have_aw&&!bvalid&&(cycles%7!=2);
        wready<=!read_active&&!have_w&&!bvalid&&(cycles%3!=1);
        if(arvalid&&arready)begin
            if(araddr[31:24]!=8'h25||arsize!=3||arburst!=1||arlen!=3||araddr[4:0]!=0)
                $fatal(1,"AXI read aperture/burst/alignment");
            if(read_active||have_aw||have_w||bvalid)$fatal(1,"AXI overlapping read");
            read_active<=1;read_addr<=araddr[23:0];read_beat<=0;read_len<=arlen;
            read_delay<=(LONG_LATENCY ? 66 : 35)+(axi_reads%7);axi_reads<=axi_reads+1;
        end
        if(rvalid&&rready)begin
            rvalid<=0;read_beat<=read_beat+1;read_delay<=3+(read_beat%3);
            if(read_beat==read_len)begin read_active<=0;inject_read_fault<=0;end
        end else if(read_active&&!rvalid&&!hold_response)begin
            if(read_delay!=0)read_delay<=read_delay-1;
            else begin rvalid<=1;rdata<=ddr_word(read_addr+8*read_beat);
                rlast<=read_beat==read_len;rresp<=inject_read_fault ? 2 : 0;end
        end
        if(awvalid&&awready)begin
            if(awaddr[31:24]!=8'h25||awsize!=3||awburst!=1||awlen!=0||awaddr[2:0]!=0)
                $fatal(1,"AXI write aperture/burst/alignment");
            if(read_active||have_aw||bvalid)$fatal(1,"AXI overlapping write");
            have_aw<=1;write_addr<=awaddr[23:0];write_delay<=(LONG_LATENCY ? 78 : 47)+(axi_writes%11);
        end
        if(wvalid&&wready)begin
            if(have_w||!wlast)$fatal(1,"AXI duplicated write or missing last");
            have_w<=1;write_data<=wdata;write_strobe<=wstrb;
        end
        if(have_aw&&have_w&&!bvalid&&!hold_response)begin
            if(write_delay!=0)write_delay<=write_delay-1;
            else begin
                value=ddr_word(write_addr);
                for(integer j=0;j<8;j=j+1)if(write_strobe[j])value[j*8+:8]=write_data[j*8+:8];
                ddr[write_addr[23:3]]<=value;written[write_addr[23:3]]<=1;
                have_aw<=0;have_w<=0;bvalid<=1;bresp<=inject_write_fault ? 2 : 0;
                axi_writes<=axi_writes+1;
            end
        end
        if(bvalid&&bready)begin bvalid<=0;inject_write_fault<=0;end
    end
    integer transaction_start=-1,beats_left=0,max_latency=0,ddr_trace;
    reg bounded_latency=1;
    always @(posedge clk)begin
        if(ddr_valid&&ddr_ready)begin
            if(transaction_start>=0)$fatal(1,"New DDR request before prior response");
            transaction_start=cycles;beats_left=ddr_write ? 1 : 4;
            $fdisplay(ddr_trace,"physical,%0d,%h,%0d,%h,%h",cycles,ddr_addr,ddr_write,ddr_wdata,ddr_wstrb);
        end
        if(memory_valid&&memory_ready)$fdisplay(ddr_trace,"logical,%0d,%h,%0d,%h,%h",cycles,memory_addr,memory_write,memory_wdata,memory_wstrb);
        if(ddr_rvalid)begin
            if(transaction_start<0)$fatal(1,"Unsolicited DDR completion");
            beats_left=beats_left-1;
            if(beats_left==0)begin
                if(bounded_latency&&cycles-transaction_start>max_latency)max_latency=cycles-transaction_start;
                if(bounded_latency&&cycles-transaction_start>LATENCY_LIMIT)$fatal(1,"DDR latency budget exceeded");
                transaction_start=-1;
            end
        end
    end

    integer first_ack=-1,last_ack=-1,release_deadline=-1,case_count=0,total_acks=0,ba_stalls=0;
    integer count_bytes=0;
    reg [15:0] c64_start; reg [23:0] reu_start;
    reg fixed_c64=0,fixed_reu=0;
    reg [7:0] saved_c64;
    reg issued=0,issued_write=0;
    reg [15:0] issued_addr=0;
    reg [7:0] issued_data=0;
    string case_name;
    integer trace;
    always @(posedge clk)begin : native_monitor
        integer index;
        reg [15:0] ca; reg [23:0] ra; reg should_write;
        cycles<=cycles+1;phase<=phase+1'b1;
        if(phase==31)frames<=frames+1;
        if(phase==15)slot_valid<=dma_req&&dma_valid;
        if(phase==28)begin
            issued<=slot_valid&&dma_req&&ba;
            issued_addr<=dma_addr;issued_write<=dma_write;issued_data<=dma_wdata;
            if(slot_valid&&dma_req&&ba&&!dma_valid)$fatal(1,"Ready DMA slot lost validity before issue");
        end
        if(phase>=29&&issued&&(dma_addr!==issued_addr||dma_write!==issued_write||
            (issued_write&&dma_wdata!==issued_data)))$fatal(1,"DMA payload changed after native request");
        if(phase==31)issued<=0;
        if(dma_ack&&!ba)$fatal(1,"DMA accepted with BA low");
        if(active_case&&phase==31&&accepted<total_slots)begin
            if(ba)begin
                if(!dma_ack)$fatal(1,"native slot missing case=%s frame=%0d accepted=%0d phase15_valid=%0d current_valid=%0d state=%0d max_ddr_clocks=%0d",case_name,frames-first_frame,accepted,slot_valid,dma_valid,reu.state,max_latency);
                index=op==2 ? accepted/2 : accepted;
                ca=c64_start+(fixed_c64 ? 0 : index);
                ra=(reu_start+(fixed_reu ? 0 : index))&MASK;
                should_write=op==1 || (op==2&&accepted%2==1);
                if(dma_addr!==ca||dma_write!==should_write)$fatal(1,"DMA address/direction case=%s index=%0d got=%h/%b expected=%h/%b",case_name,index,dma_addr,dma_write,ca,should_write);
                if(should_write)begin
                    if(dma_wdata!==reference_byte(ra))$fatal(1,"DMA byte case=%s index=%0d got=%h expected=%h REU=%h",case_name,index,dma_wdata,reference_byte(ra),ra);
                    c64[ca]<=dma_wdata;
                    if(op==2)reference_write(ra,saved_c64);
                end else begin
                    saved_c64=dma_rdata;
                    if(op==0)reference_write(ra,dma_rdata);
                    if(op==3&&dma_rdata!==reference_byte(ra))$fatal(1,"Verify fixture differs");
                end
                $fdisplay(trace,"%s,%0d,%0d,%0d,%h,%0d,%h,%0d",case_name,cycles,frames,accepted,dma_addr,dma_write,should_write?dma_wdata:dma_rdata,ba);
                if(first_ack<0)first_ack=cycles;
                last_ack=cycles;accepted=accepted+1;total_acks=total_acks+1;
                if(accepted==total_slots)release_deadline=cycles+6;
            end else begin
                ba_stalls=ba_stalls+1;
                if(dma_ack)$fatal(1,"DMA crossed a VIC-owned slot");
            end
        end else if(dma_ack)$fatal(1,"Unexpected extra DMA ACK");
        if(release_deadline==cycles)begin
            if(dma_req||busy)$fatal(1,"DMA ownership did not release by phase5 case=%s state=%0d",case_name,reu.state);
            release_deadline=-1;
        end
        if(cycles>1000000)$fatal(1,"Native REU timeout case=%s",case_name);
    end

    // Register writes at native retirement, four native cycles apart, as a
    // real CPU's store instruction would permit. This also exercises address
    // hints during ordinary register setup, without test-only cache preload.
    task automatic wr(input integer a,input integer value);
        repeat(3)begin @(negedge clk);while(phase!=31)@(negedge clk);end
        @(negedge clk);while(phase!=31)@(negedge clk);
        io_valid=1;io_write=1;io_addr=a;io_wdata=value;
        @(negedge clk);io_valid=0;io_write=0;
    endtask
    task automatic drain;
        integer n;
        begin
            n=0;
            while((ddr_busy||!ddr_ready||read_active||have_aw||have_w||bvalid||rvalid)&&n<2000)begin @(negedge clk);n=n+1;end
            if(n==2000)$fatal(1,"Posted DDR writes failed to drain");
        end
    endtask
    task automatic run_case(input string name,input integer operation,input [15:0] ca,input [23:0] ra,
        input integer length,input integer acr,input bit steal,input bit deferred,
        input bit bank_last=0,input integer soft_stop=0);
        integer j; reg [23:0] address; reg [63:0] word_value;
        begin
            case_name=name;op=operation;c64_start=ca;reu_start=ra&MASK;
            fixed_c64=(acr&128)!=0;fixed_reu=(acr&64)!=0;count_bytes=length;
            if(operation==3)for(j=0;j<length;j=j+1)c64[(ca+(fixed_c64?0:j))&65535]=reference_byte((ra+(fixed_reu?0:j))&MASK);
            wr(2,ca&255);wr(3,ca>>8);
            if(!bank_last)begin wr(4,ra&255);wr(5,(ra>>8)&255);wr(6,ra>>16);end
            wr(7,length&255);wr(8,length>>8);wr(9,8'he0);wr(10,acr);
            if(bank_last)begin wr(4,ra&255);wr(5,(ra>>8)&255);wr(6,ra>>16);end
            wr(1,(deferred?8'h80:8'h90)|operation);
            if(deferred)begin
                repeat(64)@(negedge clk);
                if(dma_req||busy)$fatal(1,"Deferred FF00 command began early");
                while(phase!=31)@(negedge clk);ff00_write=1;
                @(negedge clk);ff00_write=0;
            end
            first_frame=frames;accepted=0;total_slots=length*(operation==2?2:1);
            first_ack=-1;last_ack=-1;holes=steal;active_case=1;
            wait(accepted==total_slots);repeat(8)@(negedge clk);
            if(dma_req||busy||fault)$fatal(1,"Transfer completion state");
            io_addr=0;#1;if(io_rdata!==8'hd0||!irq)$fatal(1,"Completion/IRQ status %h",io_rdata);
            active_case=0;holes=0;
            if(soft_stop!=0)begin
                if(!ddr_busy)$fatal(1,"Soft-stop fixture missed posted write");
                if(soft_stop==1)reset=1;else enable=0;
            end
            drain();
            if(operation==0||operation==2)for(j=0;j<length;j=j+1)begin
                address=(ra+(fixed_reu?0:j))&MASK;word_value=ddr_word(address);
                if(word_value[address[2:0]*8+:8]!==reference_byte(address))
                    $fatal(1,"DDR backing byte case=%s address=%h got=%h expected=%h",name,address,word_value[address[2:0]*8+:8],reference_byte(address));
            end
            case_count=case_count+1;
            $display("CASE: %s slots=%0d first=%0d last=%0d",name,accepted,first_ack,last_ack);
            reset=0;enable=1;
        end
    endtask
    // Fault coverage intentionally suspends the latency budget: a held AXI
    // response keeps an active fetch waiting, then a real RRESP error must
    // revoke ownership without issuing a C64 byte. Hard reset recovers it.
    task automatic active_read_fault;
        integer n;
        begin
            case_name="active-read-fault";
            reset=1;drain();hard_reset=1;repeat(5)@(negedge clk);
            hold_response=1;bounded_latency=0;inject_read_fault=1;
            hard_reset=0;reset=0;
            wr(7,1);wr(8,0);wr(1,8'h91);
            if(!dma_req||!busy||dma_valid)$fatal(1,"Read-fault fixture lacks an active waiting command");
            hold_response=0;n=0;
            while(!fault&&n<200)begin @(negedge clk);n=n+1;end
            if(!fault||dma_req||dma_valid||memory_valid)$fatal(1,"DDR read error did not terminate DMA");
            n=0;while(transaction_start>=0&&n<200)begin @(negedge clk);n=n+1;end
            if(transaction_start>=0)$fatal(1,"Faulted burst failed to finish its response beats");
            repeat(5)@(negedge clk);
            if(!cache_fault||!ddr_fault||!ddr_busy)$fatal(1,"Fault was not retained at the cache/DDR boundary");
            hard_reset=1;reset=1;repeat(5)@(negedge clk);
            bounded_latency=1;hard_reset=0;reset=0;
            case_count=case_count+1;
            $display("CASE: active-read-fault ownership_aborted=1 c64_bytes=0 hard_reset_recovery=1");
        end
    endtask
    initial begin
        trace=$fopen("native.csv","w");if(!trace)$fatal(1,"Cannot create native trace");
        ddr_trace=$fopen("ddr.csv","w");if(!ddr_trace)$fatal(1,"Cannot create DDR trace");
        $fdisplay(ddr_trace,"kind,clock,address,write,data,strobe");
        $fdisplay(trace,"case,clock,frame,index,address,write,data,ba");
        negative_gap=$test$plusargs("INJECT_GAP");
        for(integer j=0;j<65536;j=j+1)c64[j]=j^(j>>8)^8'h5a;
        repeat(5)@(negedge clk);hard_reset=0;reset=0;
        run_case("fetch-one",1,16'h2000,24'h123400,1,0,0,0);
        run_case("fetch-two",1,16'h2000,24'h12341f,2,0,0,0);
        run_case("fetch-tail-next-line",1,16'h2000,24'h12347d,3,0,0,0);
        run_case("cold-bank-written-last",1,16'h2000,24'h612341,2,0,0,0,1);
        for(integer j=0;j<32;j=j+1)
            run_case($sformatf("cold-address-sweep-%02d",j),1,16'h2000,
                ((64+j%8)<<16)|((8'hab^j)<<8)|(8'ha0+j),2,0,0,0,1);
        run_case("fetch-stream",1,16'h2100,24'h23441d,513,0,0,0);
        run_case("stash-stream",0,16'h4000,24'h34561d,513,0,0,0);
        run_case("fetch-stash-roundtrip",1,16'h6000,24'h34561d,513,0,0,0);
        run_case("fixed-c64-character-stream",1,16'hd018,24'h45671f,257,128,0,0);
        run_case("fixed-reu-stash",0,16'h8000,24'h23445f,257,64,0,0);
        run_case("fixed-reu-fetch",1,16'h9000,24'h23445f,257,64,0,0);
        run_case("both-fixed",1,16'hd018,24'h23445f,129,192,0,0);
        run_case("verify-stream",3,16'h3000,24'h56781d,257,0,0,0);
        run_case("swap-stream",2,16'ha000,24'h67891d,129,0,0,0);
        run_case("wrap-fetch",1,16'hfffe,MASK-2,259,0,0,0);
        run_case("wrap-stash",0,16'hfffe,MASK-2,259,0,0,0);
        run_case("cross-512k-fetch",1,16'h5000,24'h07fff0,65,0,0,0);
        run_case("cross-512k-stash",0,16'h5100,24'h07fff0,65,0,0,0);
        run_case("fetch-vic-steals",1,16'hb000,24'h34561d,257,0,1,0);
        run_case("stash-vic-steals",0,16'hc000,24'h12345d,257,0,1,0);
        run_case("swap-vic-steals",2,16'ha000,24'h13579d,65,0,1,0);
        run_case("delayed-ff00-fixed-font",1,16'hd018,24'h24681d,257,128,0,1);
        run_case("posted-write-guest-reset",0,16'h2000,24'h512347,1,0,0,0,0,1);
        run_case("posted-write-disable",0,16'h2001,24'h612349,1,0,0,0,0,2);
        active_read_fault();
        run_case("fault-recovery-fetch",1,16'h2000,24'h712341,2,0,0,0,1);
        if(max_latency<(LONG_LATENCY ? 80 : 60))$fatal(1,"Long DDR latency profile was not exercised");
        $display("PASS: C64 REU native bits=%0d ntsc=%0d cases=%0d acks=%0d ba_stalls=%0d max_ddr_clocks=%0d reads=%0d writes=%0d",ADDRESS_BITS,NTSC,case_count,total_acks,ba_stalls,max_latency,axi_reads,axi_writes);
        $fclose(trace);$fclose(ddr_trace);$finish;
    end
endmodule
