// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_iec_transport;
    import multitini_regs_pkg::*;
    logic host_clk=0,core_clk=0,host_resetn=0,core_resetn=0,guest_reset=0;
    always #3.75 host_clk=~host_clk;
    always #15.861 core_clk=~core_clk;
    logic wr_en=0,rd_en=0;
    logic [11:0] wr_addr=0,rd_addr=0;
    logic [31:0] wr_data=0;
    logic [3:0] wr_strb=0;
    wire [31:0] rd_data,status,rx_data,tx_data;
    wire [1:0] control;
    wire rx_pop,tx_push,run,quiesce,event_ready,reply_valid;
    logic [31:0] event_data=0;
    logic event_valid=0,reply_ready=0;
    wire [31:0] reply_data;
    logic endpoint_busy=0,endpoint_quiescent=0;
    logic [15:0] endpoint_token=16'h1234;
    wire [17:0] control_value,control_snapshot;
    wire [35:0] status_value,status_snapshot;
    logic [101:0] other_control=0;
    logic [191:0] other_status=0;
    wire [101:0] other_control_snapshot;
    wire [191:0] other_status_snapshot;
    // Match the production shell's one snapshot per direction, with unrelated
    // fields changing while IEC requests and acknowledgements are in flight.
    always @(posedge host_clk) other_control<=other_control+1'b1;
    always @(posedge core_clk) other_status<=other_status+1'b1;
    multitini_word_mailbox #(.WIDTH(120)) controls_i(
        .src_clk(host_clk),.src_resetn(host_resetn),.value({control_value,other_control}),
        .dst_clk(core_clk),.dst_resetn(core_resetn),.snapshot({control_snapshot,other_control_snapshot}));
    multitini_word_mailbox #(.WIDTH(228)) status_i(
        .src_clk(core_clk),.src_resetn(core_resetn),.value({status_value,other_status}),
        .dst_clk(host_clk),.dst_resetn(host_resetn),.snapshot({status_snapshot,other_status_snapshot}));
    multitini_control_regs #(.C64_IEC_CAPS(MT_C64_IEC_CAPS_DEVICE9)) regs_i(
        .clk(host_clk),.resetn(host_resetn),.wr_en(wr_en),.wr_addr(wr_addr),
        .wr_data(wr_data),.wr_strb(wr_strb),.rd_en(rd_en),.rd_addr(rd_addr),.rd_data(rd_data),
        .c64_iec_control(control),.c64_iec_status(status),.c64_iec_rx(rx_data),
        .c64_iec_rx_pop(rx_pop),.c64_iec_tx(tx_data),.c64_iec_tx_push(tx_push));
    multitini_c64_iec_transport dut(.*);
    task automatic host_cycles(input integer n);repeat(n)@(negedge host_clk);endtask
    task automatic write_reg(input logic[11:0] address,input logic[31:0] data,input logic[3:0] strobes=15);
        @(negedge host_clk);wr_addr=address;wr_data=data;wr_strb=strobes;wr_en=1;
        @(negedge host_clk);wr_en=0;wr_strb=0;
    endtask
    task automatic push_event(input logic[31:0] data);
        @(negedge core_clk);event_data=data;event_valid=1;
        do @(posedge core_clk);while(!event_ready);
        @(negedge core_clk);event_valid=0;
    endtask
    task automatic pop_event(input logic[31:0] expected);
        integer limit;begin
            limit=0;while(!status[0]&&limit<1000)begin host_cycles(1);limit++;end
            if(!status[0])$fatal(1,"RX timeout");
            @(negedge host_clk);rd_addr=MT_REG_C64_IEC_RX;rd_en=1;#1;
            if(rd_data!==expected)$fatal(1,"RX order/data %08x expected%08x",rd_data,expected);
            @(negedge host_clk);rd_en=0;
        end
    endtask
    task automatic take_reply(input logic[31:0] expected);
        integer limit;begin
            limit=0;while(!reply_valid&&limit<1000)begin @(negedge core_clk);limit++;end
            if(!reply_valid||reply_data!==expected)$fatal(1,"TX order/data %08x expected%08x",reply_data,expected);
            @(negedge core_clk);reply_ready=1;@(negedge core_clk);reply_ready=0;
        end
    endtask
    integer n;
    initial begin
        host_cycles(10);
        if(dut.guest_reset_launch!==1'b1)$fatal(1,"guest reset launch not asserted during native reset");
        host_resetn=1;core_resetn=1;host_cycles(100);
        if(run||status[0]||status[1])$fatal(1,"disabled transport active");
        rd_addr=MT_REG_C64_IEC_CAPS;#1;if(rd_data!==32'h80000009)$fatal(1,"CAPS");
        write_reg(MT_REG_C64_IEC_CONTROL,1);host_cycles(200);
        if(!run||!status[1]||!status[2])$fatal(1,"enable failed");
        for(n=0;n<17;n++)push_event(32'h10000000+n);
        host_cycles(200);
        if(event_ready)$fatal(1,"RX queue did not backpressure at capacity");
        rd_addr=MT_REG_C64_IEC_RX;rd_en=0;host_cycles(30);
        if(!status[0]||rd_data!==32'h10000000)$fatal(1,"RX popped on address without accepted read");
        // Quiesce retains every accepted event and must not report drained.
        write_reg(MT_REG_C64_IEC_CONTROL,3);endpoint_quiescent=1;host_cycles(200);
        if(!quiesce||status[5])$fatal(1,"false quiescence with queued RX");
        for(n=0;n<17;n++)pop_event(32'h10000000+n);
        host_cycles(300);if(!status[5])$fatal(1,"quiesce never completed");
        write_reg(MT_REG_C64_IEC_CONTROL,1);endpoint_quiescent=0;host_cycles(300);
        if(status[5])$fatal(1,"quiesce active with request clear");
        // A second quiesce cannot reuse the previous acknowledgement.
        write_reg(MT_REG_C64_IEC_CONTROL,3);
        if(status[5])$fatal(1,"stale quiesce acknowledgement reused");
        host_cycles(100);endpoint_quiescent=1;host_cycles(300);
        if(!status[5])$fatal(1,"second quiesce failed");
        write_reg(MT_REG_C64_IEC_CONTROL,1);endpoint_quiescent=0;host_cycles(200);
        // TX queue capacity, accepted full strobes, exact ordering and fault.
        write_reg(MT_REG_C64_IEC_TX,32'hdeadbeef,1);host_cycles(30);
        if(reply_valid)$fatal(1,"partial TX write accepted");
        for(n=0;n<17;n++)begin
            while(!status[1])host_cycles(1);
            write_reg(MT_REG_C64_IEC_TX,32'h20000000+n);host_cycles(20);
        end
        host_cycles(50);if(status[1])$fatal(1,"TX queue not full");
        write_reg(MT_REG_C64_IEC_TX,32'hbad);host_cycles(5);
        if(!status[4])$fatal(1,"TX overrun not sticky");
        for(n=0;n<17;n++)take_reply(32'h20000000+n);
        host_cycles(100);
        if(reply_valid)$fatal(1,"replayed TX word");
        // Reset while both mailboxes own payloads; old data must not escape.
        push_event(32'hdead0001);write_reg(MT_REG_C64_IEC_TX,32'hdead0002);
        host_cycles(100);guest_reset=1;host_cycles(100);guest_reset=0;host_cycles(300);
        if(status[0]||reply_valid||status[4])$fatal(1,"guest reset failed to flush queues/mailboxes");
        push_event(32'h12345678);pop_event(32'h12345678);
        write_reg(MT_REG_C64_IEC_TX,32'h87654321);take_reply(32'h87654321);
        push_event(32'hdead0003);write_reg(MT_REG_C64_IEC_TX,32'hdead0004);
        host_cycles(100);write_reg(MT_REG_C64_IEC_CONTROL,0);host_cycles(150);
        write_reg(MT_REG_C64_IEC_CONTROL,1);host_cycles(300);
        if(status[0]||reply_valid)$fatal(1,"disable/restart retained stale data");
        push_event(32'h55aa55aa);pop_event(32'h55aa55aa);
        // Reset either physical domain with both directions occupied. Shared
        // snapshots must recover and resend while the unrelated fields change.
        push_event(32'hdead0005);write_reg(MT_REG_C64_IEC_TX,32'hdead0006);
        host_cycles(100);core_resetn=0;host_cycles(100);core_resetn=1;host_cycles(400);
        if(!run||status[0]||reply_valid)$fatal(1,"native reset recovery retained stale data or lost enable");
        push_event(32'habcd1234);pop_event(32'habcd1234);
        write_reg(MT_REG_C64_IEC_TX,32'h1234abcd);take_reply(32'h1234abcd);
        push_event(32'hdead0007);write_reg(MT_REG_C64_IEC_TX,32'hdead0008);
        host_cycles(100);host_resetn=0;host_cycles(100);host_resetn=1;host_cycles(400);
        if(run||status[0]||reply_valid)$fatal(1,"host reset recovery retained stale data or enable");
        write_reg(MT_REG_C64_IEC_CONTROL,1);host_cycles(400);
        if(!run||status[0]||reply_valid)$fatal(1,"host reset restart failed");
        push_event(32'hcafe0123);pop_event(32'hcafe0123);
        write_reg(MT_REG_C64_IEC_TX,32'hface3210);take_reply(32'hface3210);
        if(other_control_snapshot==0||other_status_snapshot==0)$fatal(1,"shared snapshot fields did not progress");
        $display("PASS IEC9 transport actual-XPM RX/TX17 quiesce resets MMIO shared120/228");$finish;
    end
    initial begin #10000000;$fatal(1,"transport timeout");end
endmodule
