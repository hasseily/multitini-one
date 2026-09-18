// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_video_frame_engine;
    parameter integer WIDTH=13,HEIGHT=9,JOBS=68,FAULT_TESTS=1;
    localparam integer STRIDE=((WIDTH+63)/64)*128,PIXELS=WIDTH*HEIGHT;
    localparam integer FRAME_WORDS=((WIDTH+3)/4)*HEIGHT,HISTORY_WORDS=(PIXELS+1)/2;
    reg clk=0,resetn=0,clear_errors=0,start=0,history_reset=0;
    always #3.75 clk=!clk;
    reg [1:0] input_slot=0,output_slot=0;
    reg [1:0] owned_input_slot=0,owned_output_slot=0;
    reg [5:0] options=0;
    wire busy,done,error;
    wire [31:0] error_count,cycles,max_stall;
    wire [31:0] araddr,awaddr;
    wire [3:0] arlen,awlen;
    wire [2:0] arsize,awsize;
    wire [1:0] arburst,awburst;
    wire arvalid,arready,rready,awvalid,awready,wvalid,wready,wlast,bready;
    reg [63:0] rdata=0;
    reg [1:0] rresp=0,bresp=0;
    reg rlast=0,rvalid=0,bvalid=0;
    wire [63:0] wdata;
    wire [7:0] wstrb;
    multitini_video_frame_engine #(.WIDTH(WIDTH),.HEIGHT(HEIGHT),.STRIDE(STRIDE)) dut(.*);

    reg [15:0] inputs[0:JOBS*PIXELS-1],expected[0:JOBS*PIXELS-1];
    reg [31:0] old[0:JOBS*PIXELS-1],expected_history[0:JOBS*PIXELS-1];
    reg [31:0] history[0:PIXELS-1];
    reg [15:0] output_memory[0:PIXELS-1];
    reg [6:0] configurations[0:JOBS-1];
    string directory;
    integer job=0,ticks=0,total=0,read_beats_total=0,write_beats_total=0;
    integer source_reads=0,history_reads=0,history_writes=0,output_writes=0,write_responses=0;
    integer read_remaining=0,read_beat=0,write_remaining=0,write_beat=0,response_delay=0;
    integer fault=0;
    reg fault_injected=0,read_active=0,write_active=0,response_pending=0;
    reg [31:0] read_address=0,write_address=0;
    reg force_read_stall=0,force_write_stall=0,force_response_stall=0;
    reg compare_values=1;
    wire [31:0] source_base=32'h22000000+{10'd0,owned_input_slot,20'd0};
    wire [31:0] output_base=32'h22400000+{10'd0,owned_output_slot,20'd0};
    // Declared adverse schedule: AR blocked up to8 consecutive clocks,
    // AW up to8, R offers separated by at least one cycle plus periodic
    // stalls, W stalls one in four clocks, B delayed10 clocks. Read/write
    // channels otherwise run independently. No claim covers unbounded DDR.
    assign arready=resetn && !read_active && !rvalid && ticks%13>=8 && !force_read_stall;
    assign awready=resetn && !write_active && !response_pending && !bvalid && ticks%17>=8 && !force_write_stall;
    assign wready=resetn && write_active && ticks%4!=0 && !force_write_stall;

    function automatic [63:0] memory_read(input [31:0] address);
        integer offset,row,column,index,lane;
        reg [63:0] value;
        begin
            value=0;
            if(address>=source_base && address<source_base+STRIDE*HEIGHT)begin
                offset=address-source_base;row=offset/STRIDE;column=(offset%STRIDE)/2;
                if(column>=WIDTH)$fatal(1,"Read in source row padding: %h",address);
                for(lane=0;lane<4;lane=lane+1)if(column+lane<WIDTH)
                    value[lane*16+:16]=inputs[job*PIXELS+row*WIDTH+column+lane];
            end else if(address>=32'h22800000 && address<32'h22800000+PIXELS*4)begin
                index=(address-32'h22800000)/4;
                value[31:0]=history[index];
                if(index+1<PIXELS)value[63:32]=history[index+1];
            end else $fatal(1,"Read outside owned source/history: %h",address);
            memory_read=value;
        end
    endfunction
    task memory_write(input [31:0] address,input [63:0] data,input [7:0] strobe);
        integer offset,row,column,index,lane;
        reg [7:0] expected_strobe;
        begin
            if(address>=output_base && address<output_base+STRIDE*HEIGHT)begin
                offset=address-output_base;row=offset/STRIDE;column=(offset%STRIDE)/2;expected_strobe=0;
                if(column>=WIDTH)$fatal(1,"Write in output row padding: %h",address);
                for(lane=0;lane<4;lane=lane+1)if(column+lane<WIDTH)begin
                    expected_strobe[lane*2+:2]=2'b11;index=row*WIDTH+column+lane;
                    if(compare_values && data[lane*16+:16]!==expected[job*PIXELS+index])
                        $fatal(1,"Output mismatch job%0d pixel%0d got%h expected%h",job,index,data[lane*16+:16],expected[job*PIXELS+index]);
                    output_memory[index]=data[lane*16+:16];output_writes=output_writes+1;
                end
                if(strobe!==expected_strobe)$fatal(1,"Bad output byte strobes %h expected%h",strobe,expected_strobe);
            end else if(address>=32'h22800000 && address<32'h22800000+PIXELS*4)begin
                index=(address-32'h22800000)/4;expected_strobe=index+1<PIXELS?8'hff:8'h0f;
                if(strobe!==expected_strobe)$fatal(1,"Bad history byte strobes");
                for(lane=0;lane<2;lane=lane+1)if(index+lane<PIXELS)begin
                    if(compare_values && data[lane*32+:32]!==expected_history[job*PIXELS+index+lane])
                        $fatal(1,"History mismatch job%0d pixel%0d got%h expected%h",job,index+lane,data[lane*32+:32],expected_history[job*PIXELS+index+lane]);
                    history[index+lane]=data[lane*32+:32];history_writes=history_writes+1;
                end
            end else $fatal(1,"Write outside owned output/history: %h",address);
        end
    endtask

    reg held_ar=0,held_aw=0,held_w=0;
    reg [31:0] held_araddr=0,held_awaddr=0;
    reg [3:0] held_arlen=0,held_awlen=0;
    reg [72:0] held_wvalue=0;
    always @(posedge clk)begin
        ticks=ticks+1;
        if(!resetn)begin
            rvalid<=0;bvalid<=0;read_active<=0;write_active<=0;response_pending<=0;
            held_ar=0;held_aw=0;held_w=0;
        end else begin
            if(held_ar && (!arvalid || araddr!==held_araddr || arlen!==held_arlen))$fatal(1,"AR changed while stalled");
            if(held_aw && (!awvalid || awaddr!==held_awaddr || awlen!==held_awlen))$fatal(1,"AW changed while stalled");
            if(held_w && (!wvalid || {wlast,wstrb,wdata}!==held_wvalue))$fatal(1,"W changed while stalled");
            held_ar=arvalid && !arready;held_aw=awvalid && !awready;held_w=wvalid && !wready;
            held_araddr=araddr;held_arlen=arlen;held_awaddr=awaddr;held_awlen=awlen;held_wvalue={wlast,wstrb,wdata};
            if(arvalid && arready)begin
                if(arsize!=3 || arburst!=1 || araddr[2:0]!=0 || araddr[11:0]+(arlen+1)*8>4096)
                    $fatal(1,"Illegal read burst");
                if(read_active || rvalid)$fatal(1,"Multiple outstanding reads");
                read_address<=araddr;read_remaining<=arlen+1;read_beat<=0;read_active<=1;
                // Protocol-fault cases truncate/extend exactly one response.
                if(!fault_injected && fault==5 && arlen>0)begin read_remaining<=arlen;fault_injected<=1;end
                if(!fault_injected && fault==6)begin read_remaining<=arlen+2;fault_injected<=1;end
            end
            if(rvalid && rready)begin
                read_beats_total=read_beats_total+1;
                if(read_address>=32'h22800000)history_reads=history_reads+1;else source_reads=source_reads+1;
                rvalid<=0;read_remaining<=read_remaining-1;read_beat<=read_beat+1;
                read_address<=read_address+8;
                if(rlast)read_active<=0;
            end
            if(read_active && !rvalid && ticks%4!=0 && !force_read_stall)begin
                // A deliberately late RLAST beat is discarded by the engine;
                // avoid inventing an actual out-of-bounds memory access here.
                rdata<=fault==6 && fault_injected && read_remaining==1?64'd0:memory_read(read_address);
                rresp<=0;
                if(!fault_injected && ((fault==1 && read_address<32'h22800000) ||
                    (fault==2 && read_address>=32'h22800000)))begin rresp<=2;fault_injected<=1;end
                rlast<=read_remaining==1;rvalid<=1;
            end
            if(awvalid && awready)begin
                if(awsize!=3 || awburst!=1 || awaddr[2:0]!=0 || awaddr[11:0]+(awlen+1)*8>4096)
                    $fatal(1,"Illegal write burst");
                if(write_active || response_pending || bvalid)$fatal(1,"Multiple outstanding writes");
                write_address<=awaddr;write_remaining<=awlen+1;write_beat<=0;write_active<=1;
            end
            if(wvalid && wready)begin
                if(wlast!==(write_remaining==1))$fatal(1,"WLAST wrong");
                memory_write(write_address,wdata,wstrb);write_beats_total=write_beats_total+1;
                write_address<=write_address+8;write_remaining<=write_remaining-1;write_beat<=write_beat+1;
                if(wlast)begin
                    write_active<=0;response_pending<=1;response_delay<=10;bresp<=0;
                    if(!fault_injected && ((fault==3 && write_address>=32'h22800000) ||
                        (fault==4 && write_address<32'h22800000)))begin bresp<=2;fault_injected<=1;end
                end
            end
            if(response_pending && !force_response_stall)begin
                if(response_delay!=0)response_delay<=response_delay-1;
                else begin bvalid<=1;response_pending<=0;end
            end
            if(bvalid && bready)begin bvalid<=0;write_responses=write_responses+1;end
            if(done && (read_active || rvalid || write_active || response_pending || bvalid))$fatal(1,"Done before AXI drain");
            if(dut.source_pop && dut.source_empty || dut.old_pop && dut.old_empty ||
               dut.new_pop && dut.new_empty || dut.output_pop && dut.output_empty)$fatal(1,"FIFO underflow");
            if(dut.source_push && dut.source_full || dut.old_push && dut.old_full ||
               dut.new_push && dut.new_full || dut.output_push && dut.output_full)$fatal(1,"FIFO overflow");
        end
    end
    task launch;
        integer i;
        begin
            @(negedge clk);
            source_reads=0;history_reads=0;history_writes=0;output_writes=0;write_responses=0;
            for(i=0;i<PIXELS;i=i+1)begin history[i]=old[job*PIXELS+i];output_memory[i]=16'hdead;end
            input_slot=job%4;output_slot=3-job%4;options=configurations[job][5:0];history_reset=configurations[job][6];
            owned_input_slot=input_slot;owned_output_slot=output_slot;
            start=1;@(negedge clk);start=0;
            // Ownership and controls must have been sampled once at START.
            input_slot=~input_slot;output_slot=~output_slot;options=~options;history_reset=!history_reset;
        end
    endtask
    task finish_job;
        input expect_error;
        begin
            wait(done);#1;
            if(busy || error!==expect_error)$fatal(1,"Completion state error=%b expected%0d",error,expect_error);
            if(history_writes!=PIXELS || output_writes!=PIXELS)$fatal(1,"Write count mismatch %0d %0d",history_writes,output_writes);
            if(fault<5 && source_reads!=FRAME_WORDS)$fatal(1,"Source read count mismatch");
            if(fault<5 && history_reads!=((configurations[job][6] || configurations[job][1:0]==0)?0:HISTORY_WORDS))$fatal(1,"History read count mismatch");
            if(cycles>=2216666)$fatal(1,"60 Hz deadline missed under declared stalls: %0d cycles",cycles);
            total=total+PIXELS;
            repeat(4)@(negedge clk);
            if(done || busy || arvalid || awvalid || wvalid)$fatal(1,"Job restarted or completion repeated");
        end
    endtask
    integer f;
    initial begin
        if(!$value$plusargs("vectors=%s",directory))$fatal(1,"Missing vector directory");
        $readmemh({directory,"/input.hex"},inputs);$readmemh({directory,"/old.hex"},old);
        $readmemh({directory,"/expected.hex"},expected);$readmemh({directory,"/history.hex"},expected_history);
        $readmemh({directory,"/options.hex"},configurations);
        repeat(5)@(negedge clk);resetn=1;
        for(job=0;job<JOBS;job=job+1)begin launch();finish_job(0);end
        $display("PASS frame engine oracle/deadline: %0dx%0d jobs=%0d pixels=%0d cycles=%0d max_stall=%0d bound=2216666",
            WIDTH,HEIGHT,JOBS,total,cycles,max_stall);
        if(FAULT_TESTS)begin
            job=63;compare_values=0;
            for(f=1;f<=6;f=f+1)begin
                fault=f;fault_injected=0;launch();finish_job(1);
                if(!fault_injected || error_count==0)$fatal(1,"Fault was not observed");
                @(negedge clk);clear_errors=1;@(negedge clk);clear_errors=0;
                if(error || error_count || max_stall)$fatal(1,"Idle diagnostics clear failed");
            end
            // Keep an accepted read stalled, then request another job and
            // clear diagnostics. Neither command may erase ownership.
            fault=0;launch();wait(read_active);
            @(negedge clk);force_read_stall=1;clear_errors=1;start=1;
            @(negedge clk);start=0;repeat(100)@(negedge clk);
            if(!busy || done || error_count==0)$fatal(1,"Busy start/clear cancelled accepted work");
            force_read_stall=0;clear_errors=0;finish_job(1);
            // Hold a write response after all W beats. Completion must wait.
            launch();wait(response_pending);@(negedge clk);force_response_stall=1;
            repeat(100)@(negedge clk);if(!busy || done)$fatal(1,"Did not retain B ownership");
            force_response_stall=0;finish_job(0);
            $display("PASS frame engine faults: R/B errors, early/late RLAST, busy start, ignored busy clear, stalled R/B drain");
        end
        $finish;
    end
    initial begin #100000000;$fatal(1,"Frame engine simulation timeout");end
endmodule
