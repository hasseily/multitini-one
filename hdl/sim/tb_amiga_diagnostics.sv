// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_amiga_diagnostics;
    reg clk=0,control_clk=0;always #5 clk=~clk;always #7 control_clk=~control_clk;
    reg reset=1,guest_reset=0,chip_tick=1;
    reg [15:0] native_config=16'h1663;
    reg [2:0] native_request=0,next_request=0;
    // Model the native controller's registered REQ. Monitor inputs instead
    // describe the bus transaction consumed ON the upcoming native edge.
    always @(posedge clk)if(reset)native_request<=0;else native_request<=next_request;
    reg [5:0] ide_monitor=0;reg cpu_wait=0,cpu_write=0;reg [7:0] memory_monitor=0;
    reg control_valid=0;reg [7:0] control_action=0;reg [15:0] control_sequence=0;
    wire control_done,control_error,frozen,running;wire [15:0] result_sequence,read_word;
    reg [6:0] read_index=0;
    multitini_amiga_diagnostics dut(.*);
    task tick;begin @(posedge clk);#1;end endtask
    task row(input bit accepted,input [5:0] ide,input bit waiting,input bit writing,input [7:0] mem,input [2:0] req);
        begin @(negedge clk);chip_tick=accepted;ide_monitor=ide;cpu_wait=waiting;cpu_write=writing;memory_monitor=mem;next_request=req;tick;end
    endtask
    task ctl(input [7:0] action,input [15:0] seq,input bit err);
        integer age;
        begin
            @(negedge clk);control_action=action;control_sequence=seq;control_valid=1;tick;
            @(negedge clk);control_valid=0;age=0;
            while(!control_done && age<10)begin tick;age=age+1;end
            if(!control_done || control_error!==err || result_sequence!==seq)$fatal(1,"control action%0d seq%0d err%0d got%0d/%0d/%0d",action,seq,err,control_done,control_error,result_sequence);
        end
    endtask
    task word(input integer index,input [15:0] expected);
        begin read_index=index;#1;if(read_word!==expected)$fatal(1,"snapshot word%0d expected%h got%h",index,expected,read_word);end
    endtask
    task count(input integer index,input [63:0] expected);
        integer j;
        begin for(j=0;j<4;j=j+1)word(16+4*index+j,expected[16*j+:16]);end
    endtask
    task idle;begin row(1,0,0,0,0,0);end endtask
    // Host endpoint uses the actual four-stage mailbox model at 5:7 clocks.
    reg control_resetn=0,write=0;reg [31:0] command=0;wire [31:0] status;
    wire [31:0] block_rdata;wire io_fpga,io_uio,io_strobe,ide_read,ide_write;
    wire [15:0] io_din,ide_wdata;wire [4:0] ide_address;wire [64:0] native_rtc;
    wire [15:0] native_display_request;wire [4:0] display_read_address;
    wire [1:0] capture_mode;wire display_switch_pending;
    multitini_amiga_host host_i(.control_clk(control_clk),.control_resetn(control_resetn),.write(write),.command(command),.status(status),
        .block_write(1'b0),.block_read(1'b0),.block_address(16'd0),.block_read_address(16'd0),.block_ram_read_address(7'd0),.block_wstrb(4'd0),.block_rdata(block_rdata),
        .clk(clk),.reset(reset),.chip_tick(chip_tick),.guest_reset(guest_reset),.diag_cpu_reset_n(1'b1),.diag_cpu_wait(cpu_wait),.diag_cpu_write(cpu_write),
        .diag_ide(ide_monitor),.diag_memory(memory_monitor),.native_config(native_config),.ide_req({3'd0,native_request}),
        .io_fpga(io_fpga),.io_uio(io_uio),.io_strobe(io_strobe),.io_din(io_din),.io_wait(1'b0),.io_dout(16'd0),
        .ide_address(ide_address),.ide_read(ide_read),.ide_write(ide_write),.ide_wdata(ide_wdata),.ide_rdata(16'd0),.native_rtc(native_rtc),
        .native_display_request(native_display_request),.display_read_address(display_read_address),.display_read_data(16'd0),.capture_mode(capture_mode),
        .display_switch_pending(display_switch_pending),.display_quiescent(1'b1));
    task host_op(input [31:0] value,input bit err,input [15:0] expected);
        reg oldtoggle;integer age;
        begin
            oldtoggle=status[29];@(negedge control_clk);command=value;write=1;@(negedge control_clk);write=0;
            age=0;while(status[31] || status[29]==oldtoggle)begin @(posedge control_clk);#1;age=age+1;if(age>2000)$fatal(1,"host bounded response");end
            if(status[30]!==err || status[15:0]!==expected)$fatal(1,"host op%h expected err%0d data%h got%h",value,err,expected,status);
            repeat(12)tick;
        end
    endtask
    integer j;reg [63:0] captured;reg [15:0] saved;
    initial begin
        repeat(5)tick;@(negedge clk);reset=0;control_resetn=1;repeat(20)tick;
        ctl(1,0,1);ctl(3,1,1);ctl(2,1,1);ctl(1,1,0);ctl(1,2,1);ctl(2,2,1);
        // First DRQ at D; REQ is registered at D+10. Independent numerical
        // oracle: ten service periods, classified using the input rows1..10.
        row(1,1,0,0,0,0); // D
        row(0,6'b000010,1,1,8'h04,0); // 1 held; stale PIO must NOT count
        row(0,6'b000010,1,0,8'h08,0); // 2 held; stale PIO must NOT count
        row(1,6'b000010,1,1,8'h82,0); // 3 CPU write;1 read word;write transaction
        row(1,6'b000100,1,0,8'h41,0); // 4 CPU read;2 read words;read transaction
        row(1,6'b100000,0,0,8'h10,0); // 5 IDE;CHIP send
        row(1,6'b001000,0,0,8'h20,0); // 6 other;1 write word;CHIP wait
        row(1,6'b010000,0,0,0,0); // 7 other;2 write words
        row(1,0,1,1,8'h04,0); // 8 write;CPU send
        row(1,0,1,0,8'h08,0); // 9 read;CPU wait
        row(1,0,0,0,0,5); // 10 other;REQ publishes now
        // Hold immediately after REQ. This must NOT extend/lose the interval.
        repeat(7)row(0,6'b000010,0,0,0,5);
        idle;idle;ctl(2,1,0);
        word(0,16'h4447);word(1,1);word(2,128);word(3,1);word(4,3);word(5,16'h1663);word(6,16'h1663);
        word(8,16'(28375196));word(9,16'(28375196>>16));word(12,1);word(13,1);word(14,28);
        count(3,1);count(4,1);count(5,10);count(6,2);count(7,2);count(8,2);count(9,1);count(10,3);
        count(11,3);count(12,3);count(13,1);count(14,1);count(15,2);count(16,2);count(17,1);count(18,1);
        count(19,3);count(20,3);count(21,10);count(24,0);count(25,1);count(26,1);count(27,1);
        if(dut.totals[23]-dut.totals[22]!==10)$fatal(1,"corrected endpoint stamps disagree with independent ten-period duration");
        captured=dut.totals[0];if(dut.totals[1]+dut.totals[2]!==captured)$fatal(1,"whole accepted+held identity");
        saved=read_word;repeat(10)idle;ctl(2,1,0);word(4,3);count(5,10);
        // New ARM invalidates old bank; open tail is disclosed and excluded.
        ctl(1,2,0);if(frozen)$fatal(1,"ARM retained prior bank");
        row(1,1,0,0,0,0);repeat(9)idle;ctl(2,2,0);word(4,7);count(3,1);count(4,0);count(5,0);
        // A REQ published on the edge before STOP must close at its actual
        // timestamp even though it is detected in the STOP drain pipeline.
        ctl(1,3,0);row(1,1,0,0,0,0);idle;idle;row(1,0,0,0,0,5);ctl(2,3,0);word(4,3);count(5,3);count(4,1);
        ctl(1,4,0);row(1,1,0,0,0,0);@(negedge clk);guest_reset=1;ide_monitor=0;repeat(3)tick;guest_reset=0;ctl(2,4,0);word(4,9);
        ctl(1,5,0);@(negedge clk);native_config=16'h0663;repeat(3)tick;ctl(2,5,0);word(4,17);word(6,16'h0663);
        ctl(1,6,0);row(1,1,0,0,0,0);idle;row(1,1,0,0,0,0);repeat(3)idle;ctl(2,6,0);word(4,69);
        ctl(1,7,0);@(negedge clk);dut.totals[0]=64'hffffffffffffffff;tick;ctl(2,7,0);word(4,33);count(0,64'hffffffffffffffff);
        // Repeated command sectors must commit each scratch interval once;
        // inter-sector service and the last open tail are not closed work.
        idle;ctl(1,8,0);row(1,1,0,0,0,0);row(1,4,0,0,1,0);row(0,0,0,0,0,5);
        repeat(3)row(0,0,0,0,0,5);idle;idle;
        row(1,1,0,0,0,0);row(1,0,1,1,2,0);row(1,0,0,0,0,4);idle;idle;
        row(1,1,0,0,0,0);repeat(11)idle;ctl(2,8,0);word(4,7);
        count(3,3);count(4,2);count(5,4);count(6,1);count(7,1);count(8,0);count(9,0);count(10,2);
        count(11,2);count(13,1);count(14,1);count(21,2);count(24,1);count(25,1);
        // Reset clears frozen state and timebase, then exercise the public
        // command ABI. A running capture is never readable as a frozen bank.
        @(negedge clk);reset=1;repeat(4)tick;@(negedge clk);reset=0;repeat(20)idle;
        if(!status[22])$fatal(1,"cap22 absent");
        host_op(32'h0d000000,1,0);host_op(32'h04000000,0,0);
        host_op(32'h0c000901,0,9);host_op(32'h0d000003,1,0);host_op(32'h04000000,0,0);
        host_op(32'h0c000a02,1,10);host_op(32'h04000000,0,0);
        host_op(32'h0c000902,0,9);host_op(32'h0d000000,0,16'h4447);host_op(32'h0d000003,0,9);host_op(32'h0d000004,0,3);
        host_op(32'h0d000080,1,0);host_op(32'h04000000,0,0);host_op(32'h0c000902,0,9);
        $display("PASS AMIGA DIAGNOSTICS exact10cycle partition,heldREQ,PIOgating,DDRdetail,STOPdrain,tail,abort,overflow,atomicbank,mailboxABI");$finish;
    end
    initial begin #2000000;$fatal(1,"diagnostic fixture timeout");end
endmodule
