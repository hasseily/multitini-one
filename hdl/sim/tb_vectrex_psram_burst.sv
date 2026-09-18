// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_vectrex_psram_burst;
    parameter real OUTPUT_DELAY=6.0;
    parameter integer WARM_QPI=0;
    parameter integer WARM_QPI_B=WARM_QPI;
    parameter real FORWARD_DELAY=0.0;
    parameter real CE_OUTPUT_DELAY=0.0;
    parameter real CAPTURE_PHASE_NS=8.072917;
    reg clk=0,resetn=0;
    always #5.208333 clk=~clk;
    reg capture_clk=0;
    initial begin
        #(CAPTURE_PHASE_NS);
        forever #5.208333 capture_clk=~capture_clk;
    end
    reg request_valid=0,request_write=0;
    reg [23:0] request_addr=0;
    reg [511:0] request_wdata=0;
    wire request_ready,response_done,read_word_valid,initialized,fault;
    wire [4:0] read_word_index;
    wire [15:0] read_word;
    tri [3:0] io_a,io_b;
    wire ce_n,sck;
    wire memory_ce_n,memory_clk;
    assign #(FORWARD_DELAY+CE_OUTPUT_DELAY) memory_ce_n=ce_n;
    assign #(FORWARD_DELAY) memory_clk=sck;
    reg allow_abort=0;
    wire qa,qb,wa,wb;
    wire [31:0] reads_a,writes_a,resets_a,reads_b,writes_b,resets_b;
    wire [23:0] address_a,address_b;
    multitini_vectrex_psram_burst dut(.clk(clk),.capture_clk(capture_clk),.resetn(resetn),
        .request_valid(request_valid),.request_write(request_write),
        .request_addr(request_addr),.request_wdata(request_wdata),
        .request_ready(request_ready),.response_done(response_done),
        .read_word_valid(read_word_valid),.read_word_index(read_word_index),
        .read_word(read_word),.initialized(initialized),.fault(fault),
        .psram_a_io(io_a),.psram_b_io(io_b),.psram_ce_n(ce_n),.psram_clk(sck));
    ly68l6400_model #(.OUTPUT_DELAY(OUTPUT_DELAY),.WARM_QPI(WARM_QPI)) a(
        .clk(memory_clk),.ce_n(memory_ce_n),.allow_abort(allow_abort),.io(io_a),
        .qpi(qa),.wrapped(wa),.reads(reads_a),.writes(writes_a),.resets(resets_a),.last_address(address_a));
    ly68l6400_model #(.OUTPUT_DELAY(OUTPUT_DELAY),.WARM_QPI(WARM_QPI_B)) b(
        .clk(memory_clk),.ce_n(memory_ce_n),.allow_abort(allow_abort),.io(io_b),
        .qpi(qb),.wrapped(wb),.reads(reads_b),.writes(writes_b),.resets(resets_b),.last_address(address_b));
    integer cycles=0,accepted_at,first_at,done_at,ready_at,count;
    integer read_latency,write_latency,accept_gap;
    integer n,j;
    reg [511:0] expected;
    reg checking=0;
    realtime input_changed_at=0,capture_hold_until=0;
    realtime capture_setup_min=1.0e9,capture_hold_min=1.0e9,last_capture_at=0;
    // Check actual receiving IBUF data, including clock/output/flight delay.
    // Setup/hold use the routed primitive's conservative0.012/0.068ns bounds.
    always @(dut.psram_a_i or dut.psram_b_i) begin
        if(checking && $realtime<capture_hold_until)
            $fatal(1,"Capture input hold violation");
        if(checking && last_capture_at!=0 && $realtime-last_capture_at<capture_hold_min)
            capture_hold_min=$realtime-last_capture_at;
        input_changed_at=$realtime;
    end
    always @(posedge capture_clk) begin
        if(checking && dut.capture_tape[95]) begin
            if($realtime-input_changed_at<0.012)
                $fatal(1,"Capture input setup violation");
            if($realtime-input_changed_at<capture_setup_min)
                capture_setup_min=$realtime-input_changed_at;
            capture_hold_until=$realtime+0.068;
            last_capture_at=$realtime;
        end
    end
    always @(posedge clk) begin
        cycles=cycles+1;
        #0.001;
        if(checking && read_word_valid) begin
            if(read_word_index!==count[4:0]) $fatal(1,"Word index %0d expected %0d",read_word_index,count);
            if(read_word!==expected[count*16+:16])
                $fatal(1,"Read word %0d got %h expected %h delay=%0.1f",count,read_word,expected[count*16+:16],OUTPUT_DELAY);
            if(count==0) first_at=cycles;
            count=count+1;
        end
    end
    task wait_ready;
        begin
            while(!request_ready) @(negedge clk);
        end
    endtask
    task operation(input bit writing,input [23:0] addr,input [511:0] data);
        begin
            wait_ready();
            request_valid=1;request_write=writing;request_addr=addr;request_wdata=data;
            expected=data;count=0;checking=!writing;
            @(posedge clk);#0.002;accepted_at=cycles;
            @(negedge clk);request_valid=0;request_addr=24'hffffff;request_wdata=~data;
            while(!response_done) @(negedge clk);
            done_at=cycles;
            if(writing) write_latency=done_at-accepted_at;
            else begin
                if(count!=32) $fatal(1,"Incomplete read count=%0d",count);
                read_latency=done_at-accepted_at;
                if(first_at-accepted_at!=17 || read_latency!=80)
                    $fatal(1,"Unexpected read latency first=%0d done=%0d",first_at-accepted_at,read_latency);
            end
            checking=0;
            wait_ready(); ready_at=cycles;accept_gap=ready_at-accepted_at;
            if(address_a!==addr>>1 || address_b!==addr>>1) $fatal(1,"Physical address mismatch");
            if(fault) $fatal(1,"Unexpected PHY fault");
        end
    endtask
    task abort_operation(input bit writing);
        begin
            wait_ready();request_valid=1;request_addr=0;request_write=writing;
            request_wdata={64{8'hab}};
            @(posedge clk);@(negedge clk);request_valid=0;
            repeat(30) @(negedge clk);
            allow_abort=1;resetn=0;
            repeat(3) @(negedge clk);
            if(!ce_n || sck || response_done || read_word_valid) $fatal(1,"Reset did not quiesce PHY");
            resetn=1;allow_abort=0;wait_ready();
        end
    endtask
    initial begin
        #100;resetn=1;
        wait_ready();
        if($realtime<150000 || !qa || !qb || !wa || !wb || resets_a==0 || resets_b==0)
            $fatal(1,"Initialization/QPI/reset/wrap failed");
        for(n=0;n<6;n=n+1) begin
            for(j=0;j<64;j=j+1) expected[j*8+:8]=(j*73+n*37)^8'h96;
            operation(1,n==5 ? 24'hffffc0 : n*64,expected);
            operation(0,n==5 ? 24'hffffc0 : n*64,expected);
        end
        $display("TIMING first=%0d read_done=%0d write_done=%0d read_accept_gap=%0d cycles, CE_high_min=%0.3f CE_low_max=%0.3f CE_hold_min=%0.3f ns",
            first_at-accepted_at,read_latency,write_latency,accept_gap,a.min_high,a.max_low,a.min_hold);
        $display("CAPTURE phase=%0.6f ns setup_min=%0.3f hold_min=%0.3f ns",CAPTURE_PHASE_NS,capture_setup_min,capture_hold_min);
        // Misalignment is rejected before touching any chip-select/data pins.
        wait_ready();request_valid=1;request_addr=1;request_write=0;
        @(posedge clk);@(negedge clk);request_valid=0;
        if(!response_done || !fault || !ce_n) $fatal(1,"Alignment rejection failed");
        // Reset aborts a read, then recovers chips left in QPI mode.
        resetn=0;repeat(3) @(negedge clk);resetn=1;
        abort_operation(0);
        abort_operation(1);
        for(j=0;j<64;j=j+1) expected[j*8+:8]=j^8'h53;
        operation(1,24'h07c0,expected);operation(0,24'h07c0,expected);
        if(reads_a!=reads_b || writes_a!=writes_b) $fatal(1,"Chips lost alignment");
        $display("PASS: pin-level Vectrex PSRAM tACLK=%0.3f forward=%0.3f return=%0.3f clock_out=%0.3f input=%0.3f data_out=%0.3f ce_out=%0.3f warm=%0d/%0d, reads=%0d writes=%0d resets=%0d",
            OUTPUT_DELAY,FORWARD_DELAY,`MT_PSRAM_RETURN_DELAY,`MT_PSRAM_CLOCK_OUTPUT_DELAY,
            `MT_PSRAM_INPUT_DELAY,`MT_PSRAM_DATA_OUTPUT_DELAY,CE_OUTPUT_DELAY,
            WARM_QPI,WARM_QPI_B,reads_a,writes_a,resets_a);
        $finish;
    end
    initial begin #3000000;$fatal(1,"PSRAM test timeout");end
endmodule
