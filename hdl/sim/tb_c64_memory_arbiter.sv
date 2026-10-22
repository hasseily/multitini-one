// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_memory_arbiter;
    logic clk=0,reset=1;
    logic[23:0] cart_addr=24'h123456,reu_addr=24'hfedcba;
    logic cart_write=1,reu_write=0;
    logic[63:0] cart_wdata=64'h0123456789abcdef,reu_wdata=64'hfedcba9876543210;
    logic[7:0] cart_wstrb=8'ha5,reu_wstrb=8'h5a;
    logic cart_valid=0,reu_valid=0;
    wire cart_ready,reu_ready,cart_rvalid,reu_rvalid;
    wire[23:0] memory_addr;
    wire memory_write,memory_valid;
    wire[63:0] memory_wdata;
    wire[7:0] memory_wstrb;
    logic memory_ready=0,memory_rvalid=0,memory_fault=0;
    integer accepts=0,completes=0,checks=0;
    always #5 clk=~clk;
    multitini_c64_memory_arbiter dut(.*);
    task automatic check(input logic okay,input string why);
        checks++;if(okay !==1'b1)$fatal(1,"arbiter: %s",why);
    endtask
    task automatic tick; @(posedge clk);#1;@(negedge clk);#1;endtask
    always @(posedge clk) begin
        if(memory_valid && memory_ready)accepts++;
        if(cart_rvalid || reu_rvalid)completes++;
        if(cart_rvalid && reu_rvalid)$fatal(1,"arbiter duplicate owner");
    end
    initial begin
        tick();reset=0;memory_rvalid=1;#1;
        check(!cart_rvalid && !reu_rvalid,"stray idle response");
        memory_rvalid=0;cart_valid=1;reu_valid=1;tick();
        repeat(4)begin
            #1;check(memory_valid && !cart_ready && !reu_ready,"backpressure held");
            check(memory_addr==cart_addr && memory_write && memory_wdata==cart_wdata && memory_wstrb==cart_wstrb,"cart payload selected");
            reu_addr++;reu_wdata++;tick();
        end
        memory_ready=1;#1;check(cart_ready && !reu_ready,"cart priority acceptance");tick();
        cart_valid=0;memory_ready=0;cart_addr=0;
        repeat(4)begin check(!memory_valid && !reu_ready,"no interleaved request while waiting");tick();end
        memory_rvalid=1;#1;check(cart_rvalid && !reu_rvalid,"late response retains original owner");tick();memory_rvalid=0;
        tick();memory_ready=1;memory_rvalid=1;#1;
        check(reu_ready && reu_rvalid && !cart_ready && !cart_rvalid,"zero-latency REU response");
        check(memory_addr==reu_addr && memory_wdata==reu_wdata && memory_wstrb==reu_wstrb && !memory_write,"REU request payload");
        tick();reu_valid=0;memory_rvalid=0;memory_ready=0;
        cart_valid=1;tick();cart_valid=0;tick();
        check(!memory_valid,"cancel unaccepted client");
        reu_valid=1;tick();memory_ready=1;tick();reu_valid=0;memory_ready=0;
        memory_fault=1;memory_rvalid=1;#1;
        check(!memory_valid && !cart_ready && !reu_ready && !cart_rvalid && !reu_rvalid,"fault abort suppresses completion");
        tick();memory_fault=0;#1;
        check(!cart_rvalid && !reu_rvalid,"late fault response discarded");
        memory_rvalid=0;cart_valid=1;tick();memory_ready=1;tick();cart_valid=0;memory_ready=0;
        reset=1;memory_rvalid=1;#1;
        check(!memory_valid && !cart_ready && !reu_ready && !cart_rvalid && !reu_rvalid,"reset abort suppresses completion");
        tick();reset=0;#1;check(!cart_rvalid && !reu_rvalid,"late reset response discarded");
        memory_rvalid=0;tick();
        check(accepts==4 && completes==2,"accepted/completed/aborted accounting");
        $display("PASS: C64 memory arbiter checks=%0d accepted=%0d completed=%0d aborted=2",checks,accepts,completes);$finish;
    end
    initial begin #10000;$fatal(1,"arbiter timeout");end
endmodule
