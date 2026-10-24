// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_host;
    reg clk=0,fastclk=0,reset=1,debug_enable=0,paused=0,loader_write=0;
    always #5 clk=~clk;
    initial begin #1.25;fastclk=1;forever #2.5 fastclk=~fastclk;end
    reg[31:0]peek_word=0,reu_word=0;
    wire peek_request,peek_valid,invalidate;
    wire[15:0]peek_address;
    wire[7:0]peek_data;
    wire[31:0]peek_status,reu_status;
    reg invalidate_done=0,reu_busy=0,ua_held=0;
    reg[15:0]load_addr=0;
    reg[7:0]load_data=0;
    integer checks=0,requests=0,invalidates=0;
    reg[3:0]seq=0;
    multitini_c64_host dut(.*);
    multitini_c64_island_memory ram(
        .clk_cpu(fastclk),.clk_native(clk),.cpu_ce(1'b0),.cpu_ram_we(1'b0),.cpu_colour_we(1'b0),
        .cpu_addr(16'd0),.cpu_wdata(8'd0),.ram_low_q(),.ram_high_q(),.rom_q(),.char_q(),.colour_q(),
        .loader_we(loader_write),.loader_addr(load_addr),.loader_data(load_data),
        .machine_ce(1'b0),.machine_we(1'b0),.machine_store(1'b0),.machine_addr(16'd0),.machine_data(8'd0),.machine_q(),
        .peek_request(peek_request&&debug_enable),.machine_paused(paused),.peek_addr(peek_address),.peek_valid(peek_valid),.peek_data(peek_data),
        .native_colour_addr(10'd0),.native_colour_we(1'b0),.native_colour_data(4'd0),.native_colour_q(),
        .rom_load_addr(14'd0),.rom_load_data(8'd0),.rom_load_basic_kernal(1'b0),.rom_load_char(1'b0));
    always @(posedge clk)begin if(peek_request)requests++;if(invalidate)invalidates++;end
    task check(input bit good,input string label);
        checks++;if(!good)$fatal(1,"host: %s",label);
    endtask
    task request(input[15:0]a,input bit error,input[7:0]d);
        integer timeout_ticks,old_requests;
        old_requests=requests;
        @(negedge clk);seq++;peek_word={12'd0,seq,a};
        timeout_ticks=0;
        while(peek_status[19:16]!=seq&&timeout_ticks<20)begin @(negedge clk);timeout_ticks++;end
        check(timeout_ticks<20,"peek acknowledgement deadline");
        check(peek_status[20]===error,"peek refusal/success bit");
        check(peek_status[7:0]===(error?8'd0:d),"peek byte");
        repeat(7)@(negedge clk);
        check(requests-old_requests==(error?0:1),"held counter produces at most one RAM request");
    endtask
    initial begin
        repeat(4)@(negedge clk);reset=0;
        loader_write=1;
        for(integer i=0;i<256;i++)begin load_addr=16'(i*257);load_data=8'(i^8'h5a);@(negedge clk);end
        loader_write=0;
        request(16'h1515,1,0);debug_enable=1;request(16'h1515,1,0);
        paused=1;loader_write=1;request(16'h1515,1,0);loader_write=0;
        for(integer i=0;i<256;i++)request(16'(i*257),0,8'(i^8'h5a));
        // Loader arrives after permission check but before synchronous RAM:
        // no peek completes with loader data and no stale retry is issued.
        @(negedge clk);seq++;peek_word={12'd0,seq,16'h1515};
        @(negedge clk);loader_write=1;load_addr=16'h1515;load_data=8'hc3;
        repeat(4)@(negedge clk);loader_write=0;
        check(peek_status[19:16]==seq&&peek_status[20],"late loader refuses in-flight peek");
        request(16'h1515,0,8'hc3);
        reu_busy=1;ua_held=1;#1;check(!reu_status[12]&&reu_status[14],"host flags");
        @(negedge clk);reu_word=1;repeat(5)@(negedge clk);
        check(invalidates==1&&reu_status[11:8]==0,"invalidate waits for completion");
        // Firmware retries with a new counter after a bounded timeout. The
        // cache operation is idempotent and must be emitted again.
        reu_word=2;repeat(5)@(negedge clk);
        check(invalidates==2,"new-counter invalidate retry is not lost");
        invalidate_done=1;@(negedge clk);invalidate_done=0;reu_busy=0;
        repeat(5)@(negedge clk);
        check(reu_status[11:8]==2&&reu_status[12],"retry acknowledges latest counter");
        check(invalidates==2,"stable invalidate word has exactly one operation");
        $display("PASS c64_host checks=%0d requests=%0d invalidates=%0d",checks,requests,invalidates);$finish;
    end
    initial begin #2000000;$fatal(1,"host test timeout");end
endmodule
