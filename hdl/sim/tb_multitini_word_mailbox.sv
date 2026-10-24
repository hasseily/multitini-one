// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_multitini_word_mailbox #(parameter integer WIDTH=32);
    reg src_clk=0,dst_clk=0,src_resetn=0,dst_resetn=0;
    always #3.5 src_clk=~src_clk;
    always #5.5 dst_clk=~dst_clk;
    function automatic [WIDTH-1:0] pattern(input [15:0] seed);
        for(integer bit_index=0;bit_index<WIDTH;bit_index++)
            pattern[bit_index]=seed[bit_index%16]^((bit_index/16)&1);
    endfunction
    reg[WIDTH-1:0]value=pattern(16'h1357);
    wire[WIDTH-1:0]snapshot;
    localparam[WIDTH-1:0]RESET_VALUE=pattern(16'h2468);
    multitini_word_mailbox #(.WIDTH(WIDTH),.RESET_VALUE(RESET_VALUE))dut(.*);
    reg[WIDTH-1:0]sent[0:4095];integer sent_count=0,checks=0,settled=0;
    reg reset_sampled=1;
    always @(posedge dst_clk)reset_sampled<=!dst_resetn;
    always @(posedge src_clk)if(src_resetn&&dut.changed&&dut.ready)begin
        sent[sent_count]=value;sent_count++;
        if(sent_count>=4096)$fatal(1,"mailbox source bound");
    end
    always @(negedge dst_clk)begin
        if(!dst_resetn&&reset_sampled)begin
            if(snapshot!==RESET_VALUE)$fatal(1,"destination reset value");
        end else if(dst_resetn&&snapshot!==RESET_VALUE)begin
            bit found;found=0;
            for(integer i=0;i<sent_count;i++)if(snapshot===sent[i])found=1;
            if(!found||snapshot!==pattern(snapshot[15:0]))$fatal(1,"incoherent/unsubmitted mailbox word");
        end
        checks++;
    end
    task latest;
        integer limit;limit=0;
        while(snapshot!==value && limit<500)begin @(negedge dst_clk);limit++;end
        if(snapshot!==value)$fatal(1,"latest snapshot failed after reset/drain");
        settled++;
    endtask
    initial begin
        repeat(10)@(negedge src_clk);src_resetn=1;dst_resetn=1;latest();
        for(integer k=0;k<20;k++)begin
            for(integer n=0;n<50;n++)begin
                @(negedge src_clk);value=pattern(16'(k*50+n));
            end
            // Destination-only reset interrupts an occupied XPM handshake;
            // unchanged source values must be resent after recovery.
            #2;dst_resetn=0;repeat(7+k%5)@(negedge dst_clk);dst_resetn=1;latest();
            @(negedge src_clk);src_resetn=0;
            repeat(4+k%3)@(negedge src_clk);src_resetn=1;latest();
            value=pattern(16'(k+2000));latest();
        end
        if(settled!=61||sent_count<40)$fatal(1,"mailbox coverage");
        $display("PASS word_mailbox width=%0d checks=%0d accepted=%0d latest=%0d",WIDTH,checks,sent_count,settled);$finish;
    end
    initial begin #2000000;$fatal(1,"mailbox timeout");end
endmodule
