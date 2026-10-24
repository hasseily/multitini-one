// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps
module tb_c64_reu_transport;
    logic clk=0; always #5 clk=!clk;
    logic reset=1,enable=1,io_valid=0,io_write=0,ff00_write=0;
    logic [4:0] io_addr=0;
    logic [7:0] io_wdata=0;
    wire [7:0] io_rdata;
    wire dma_req,dma_valid,dma_write,dma_idle,dma_peek,dma_last_write;
    wire [15:0] dma_addr;
    wire [7:0] dma_wdata;
    logic dma_ack=0;
    logic [7:0] dma_rdata=0;
    wire [23:0] memory_addr;
    wire [23:0] memory_hint_addr;
    wire memory_hint_read;
    wire memory_write,memory_valid;
    wire [63:0] memory_wdata;
    wire [7:0] memory_wstrb;
    logic memory_ready=0,memory_rvalid=0,memory_fault=0;
    logic [63:0] memory_rdata=0;
    wire irq,busy,fault;
    multitini_c64_reu dut(.memory_waiting(),.*);
    integer checks=0,accepts=0,writes=0,cycles=0;
    always @(posedge clk) begin
        cycles<=cycles+1;
        if(memory_valid&&memory_ready) accepts=accepts+1;
        if(dma_valid&&dma_ack&&dma_write) writes=writes+1;
        if(cycles>5000)$fatal(1,"Transport timeout");
    end
    task automatic check(input bit good,input string why);
        if(!good)$fatal(1,"%s",why);checks=checks+1;
    endtask
    task automatic wr(input integer a,input integer d);
        @(negedge clk);io_addr=a;io_wdata=d;io_write=1;io_valid=1;
        @(negedge clk);io_valid=0;io_write=0;
    endtask
    task automatic fetch;
        wr(2,'h00);wr(3,'h20);wr(4,0);wr(5,0);wr(6,'h80);
        wr(7,1);wr(8,0);wr(1,'h91);
    endtask
    task automatic accept_read;
        wait(memory_valid);@(negedge clk);
        check(!memory_write&&memory_addr==24'h800000,"Expected high-bank read");
        memory_ready=1;@(negedge clk);memory_ready=0;
    endtask
    task automatic respond(input logic [7:0] value);
        @(negedge clk);memory_rdata={8{value}};memory_rvalid=1;
        @(negedge clk);memory_rvalid=0;
    endtask
    task automatic finish_fetch(input logic [7:0] value);
        wait(dma_valid);@(negedge clk);
        repeat(6)begin
            check(dma_req&&dma_valid&&dma_write&&dma_addr==16'h2000&&dma_wdata==value,"DMA write did not remain stable");
            @(negedge clk);
        end
        dma_ack=1;@(negedge clk);dma_ack=0;
        wait(memory_valid);@(negedge clk);
        check(!memory_write&&memory_addr==24'h800000,"Missing terminal fetch read");
        memory_ready=1;memory_rvalid=1;@(negedge clk);memory_ready=0;memory_rvalid=0;
        wait(!busy);@(negedge clk);
        check(!dma_req&&!dma_valid&&!fault,"Completion failed");
    endtask
    initial begin
        repeat(3)@(negedge clk);reset=0;
        for(integer mode=0;mode<2;mode=mode+1)begin
            fetch();wait(memory_valid);@(negedge clk);
            repeat(7)begin
                check(dma_req&&!dma_valid&&memory_valid&&memory_addr==24'h800000,"Backpressure altered read");
                @(negedge clk);
            end
            accept_read();
            repeat(5)begin
                check(busy&&dma_req&&!dma_valid&&!memory_valid,"Unexpected access while PSRAM is pending");
                @(negedge clk);
            end
            if(mode==0)reset=1;else enable=0;
            repeat(3)@(negedge clk);
            check(busy&&!dma_req&&!dma_valid&&!memory_valid,"Reset/disable did not preserve drain-only state");
            reset=0;enable=1;
            fetch();
            repeat(9)begin
                check(busy&&dma_req&&!dma_valid&&!memory_valid,"New command overtook old completion");
                @(negedge clk);
            end
            respond(8'h61);accept_read();respond(8'h92);finish_fetch(8'h92);
            check(writes==mode+1,"Reset/disable duplicated a C64 write");
        end
        fetch();accept_read();
        @(negedge clk);memory_fault=1;@(negedge clk);memory_fault=0;
        repeat(4)@(negedge clk);
        check(fault&&!busy&&!dma_req&&!dma_valid&&!memory_valid,"Memory fault did not terminate ownership");
        fetch();repeat(5)@(negedge clk);
        check(fault&&!dma_req&&!memory_valid,"Fault did not block new transfer");
        reset=1;repeat(2)@(negedge clk);reset=0;
        check(!fault&&!busy,"Reset failed to clear fault");
        check(accepts==7&&writes==2,"Unexpected transaction count");
        // A backend may present ready on the same edge that aborts its work.
        // Fault must suppress acceptance and must never strand a pending read.
        fetch();wait(memory_valid);@(negedge clk);
        memory_ready=1;memory_fault=1;#1;
        check(!memory_valid&&!dma_req&&!dma_valid,"Live fault allowed an access");
        @(negedge clk);memory_ready=0;memory_fault=0;
        repeat(3)@(negedge clk);
        check(fault&&!busy&&!dma_req&&!dma_valid&&!memory_valid,"Ready/fault collision stranded pending work");
        check(accepts==7&&writes==2,"Ready/fault collision accepted a transaction");
        reset=1;repeat(2)@(negedge clk);reset=0;
        check(!fault&&!busy,"Ready/fault collision failed to recover on reset");
        fetch();accept_read();respond(8'ha7);finish_fetch(8'ha7);
        check(accepts==9&&writes==3,"Recovery transfer was lost or duplicated");
        $display("PASS: C64 REU transport checks=%0d accepted=%0d writes=%0d",checks,accepts,writes);
        $finish;
    end
endmodule
