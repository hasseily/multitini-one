// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_island_governor;
    reg clk=0,reset=1,turbo=0,policy_changed=0,c64u_top=0;
    reg [3:0] rate_index=0;
    reg [5:0] slot_next=0;
    reg frame_run_next=1,forward_wait=0,completion_enable=0;
    wire grant_q,credit_q;
    multitini_c64_island_governor dut(.*);
    always #5 clk=~clk;
    integer checks=0,count,n,previous,spacing;
    reg [63:0] mask;
    task automatic check(input bit ok,input string why);
        checks++;if(!ok)$fatal(1,"GOVERNOR FAIL %s index=%d slot=%d",why,rate_index,slot_next);
    endtask
    task automatic tick; @(posedge clk);#0.01;endtask
    initial begin
        repeat(3) @(negedge clk);reset=0;turbo=1;
        for(integer u=0;u<2;u++)for(integer idx=0;idx<16;idx++)begin
            @(negedge clk);c64u_top=u;rate_index=idx;
            case(idx)
                0:n=1;1:n=2;2:n=3;3:n=4;4:n=6;5:n=8;6:n=10;7:n=12;
                8:n=14;9:n=16;10:n=20;11:n=24;12:n=32;13:n=40;14:n=u?47:48;15:n=u?63:64;
            endcase
            count=0;mask=0;
            for(integer slot=0;slot<64;slot++)begin
                @(negedge clk);slot_next=slot;tick();
                mask[slot]=grant_q;if(grant_q)count++;
            end
            check(count==n,"exactly N grants per 64 slots");
            check(mask[5],"rotation includes slot5");
            if(n<64)check(!mask[4],"only 64 uses slot4");
            previous=5;
            for(integer slot=6;slot<=69;slot++)if(mask[slot%64])begin
                spacing=slot-previous;
                check(spacing==64/n || spacing==(64+n-1)/n,"Bresenham spacing");
                previous=slot;
            end
        end
        // A completion can use a previous waiting grant, exactly once.
        @(negedge clk);rate_index=0;c64u_top=0;slot_next=5;forward_wait=1;
        tick();check(grant_q,"grant registered for slot5");
        @(negedge clk);slot_next=6;tick();check(credit_q,"unused waiting grant becomes credit");
        repeat(5)begin @(negedge clk);slot_next++;tick();check(credit_q,"credit retained across wait");end
        @(negedge clk);completion_enable=1;tick();check(!credit_q,"completion consumes credit");
        @(negedge clk);completion_enable=0;policy_changed=1;slot_next=5;tick();
        check(!grant_q && !credit_q,"policy change clears both budgets");
        @(negedge clk);policy_changed=0;frame_run_next=0;tick();check(!grant_q,"frozen frame grants none");
        $display("GOVERNOR PASS checks=%0d",checks);$finish;
    end
endmodule
