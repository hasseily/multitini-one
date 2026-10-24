// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Acceptance and completion scoreboard uses public inputs only. It deliberately
// queues ordinary entries behind acknowledges to exercise ordered completion.
module tb_c64_turbo_fio;
    parameter integer DEPTH = 4;
    parameter integer ACK_EDGES = 2;
    localparam integer PW = $clog2(DEPTH)+1;
    reg clk=0; always #5 clk=~clk;
    reg reset=1;
    reg [DEPTH*22-1:0] fio_entries=0;
    reg [PW-1:0] fio_wp=0;
    wire [PW-1:0] fio_done;
    reg vic_busy=0, vic_hold_d011=0, vic_hold_spr=0;
    wire vic_wr, vic_clr_m2m, vic_clr_m2d, sid_wr, sid_latch, u64_wr;
    wire [5:0] vic_wa;
    wire [7:0] vic_wd, sid_d, u64_d;
    wire [9:0] sid_a, u64_a;
    multitini_c64_fio_port #(.FIO_DEPTH(DEPTH),.FIO_ACK_EDGES(ACK_EDGES)) dut(.*);
    integer cycle=0, pushed=0, accepted=0, completed=0, last_due=-1;
    integer due[2048];
    integer busy_blocks=0, d011_blocks=0, sprite_blocks=0, ack_count=0, wraps=0;
    integer accepts_before_reset=0;
    reg [21:0] head;
    reg expect_accept, held, ack;
    reg [5:0] expected_strobes;
    integer expected_done;
    function automatic [21:0] entry(input integer index);
        reg target; reg [1:0] kind; reg [9:0] address;
        begin
            target = index%5 == 4;
            kind = target ? ((index%2) ? 3 : 0) : ((index%7 == 5) ? 1 : ((index%7 == 6) ? 2 : 0));
            case(index%8)
            0: address=10'h011;
            1: address=10'h015;
            2: address=10'h003;
            3: address=10'h019;
            4: address=10'h41;
            5: address=10'h01a;
            6: address=10'h1ff;
            default: address=10'h020;
            endcase
            entry={target,kind,index[0],address,index[7:0] ^ 8'ha5};
        end
    endfunction
    always @(negedge clk) begin
        if (!reset) begin
            vic_busy = cycle%19 >= 14;
            vic_hold_d011 = cycle%32 >= 10 && cycle%32 <= 13;
            vic_hold_spr = cycle%43 >= 10 && cycle%43 <= 26;
            if (pushed < 1200 && pushed-completed < DEPTH) begin
                fio_entries[(pushed%DEPTH)*22 +:22]=entry(pushed);
                pushed=pushed+1;
                fio_wp=pushed;
            end
        end
    end
    always @(posedge clk) begin
        cycle=cycle+1;
        if (reset) begin
            pushed=0; accepted=0; completed=0; last_due=-1;
            #1;
            if (fio_done !== 0 || {vic_wr,vic_clr_m2m,vic_clr_m2d,sid_wr,sid_latch,u64_wr} !== 0)
                $fatal(1,"reset did not flush strobes/completion");
        end else begin
            head = fio_entries[(accepted%DEPTH)*22 +:22];
            held = !head[21] && head[20:19]==0 &&
                ((head[13:8]==6'h11 && vic_hold_d011) ||
                 ((((head[13:8]<=6'h0f)&&head[8]) || head[13:8]==6'h15) && vic_hold_spr));
            expect_accept=accepted<pushed && !vic_busy && !held;
            if (accepted<pushed && vic_busy) busy_blocks=busy_blocks+1;
            if (accepted<pushed && held && head[13:8]==6'h11) d011_blocks=d011_blocks+1;
            if (accepted<pushed && held && head[13:8]!=6'h11) sprite_blocks=sprite_blocks+1;
            expected_strobes=0;
            if (expect_accept) begin
                expected_strobes = {
                    !head[21] && head[20:19]==0,
                    !head[21] && head[20:19]==1,
                    !head[21] && head[20:19]==2,
                    head[21] && head[20:19]==0,
                    head[21] && head[20:19]==3,
                    !head[21] && head[20:19]==0};
                ack=!head[21] && head[20:19]==0 && (head[13:8]==6'h19 || head[13:8]==6'h1a);
                due[accepted]=cycle+1+(ack ? ACK_EDGES : 1);
                if (due[accepted]<=last_due) due[accepted]=last_due+1;
                last_due=due[accepted];
                if (ack) ack_count=ack_count+1;
                accepted=accepted+1;
            end
            if (completed<accepted && due[completed]==cycle) completed=completed+1;
            expected_done=completed% (2*DEPTH);
            #1;
            if ({vic_wr,vic_clr_m2m,vic_clr_m2d,sid_wr,sid_latch,u64_wr} !== expected_strobes)
                $fatal(1,"accept strobes cycle=%0d expected=%b actual=%b",cycle,expected_strobes,{vic_wr,vic_clr_m2m,vic_clr_m2d,sid_wr,sid_latch,u64_wr});
            if (fio_done !== expected_done[PW-1:0])
                $fatal(1,"ordered done cycle=%0d expected=%0d actual=%0d",cycle,expected_done,fio_done);
            if (expect_accept && ({vic_wa,vic_wd,sid_a,sid_d,u64_a,u64_d} !==
                {head[13:8],head[7:0],head[17:8],head[7:0],head[17:8],head[7:0]}))
                $fatal(1,"landing payload changed");
        end
    end
    initial begin
        repeat(3) @(negedge clk);
        #1 reset=0;
        wait(accepted>=17);
        @(negedge clk); #1;
        accepts_before_reset=accepted;
        reset=1; fio_wp=0; fio_entries=0;
        repeat(3) @(negedge clk);
        #1 reset=0;
        wait(completed==1200);
        @(negedge clk);
        if (busy_blocks==0 || d011_blocks==0 || sprite_blocks==0 || ack_count==0 || accepts_before_reset<17)
            $fatal(1,"missing coverage");
        $display("PASS FIO depth=%0d ack=%0d entries=1200 busy=%0d d011=%0d sprite=%0d acknowledges=%0d",DEPTH,ACK_EDGES,busy_blocks,d011_blocks,sprite_blocks,ack_count);
        $finish;
    end
    initial begin #1000000; $fatal(1,"FIO timeout"); end
endmodule
