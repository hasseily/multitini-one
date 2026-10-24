// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_island_nmi;
    reg clk=0,reset=1,turbo=0,t65_enable=0,live_nmi_n=1,exact_nmi_n=1;
    wire t65_nmi_n,exact_switch_safe,pending_rearm,carry_active,used_hold;
    multitini_c64_nmi_delivery dut(.*);
    always #5 clk=~clk;
    integer checks=0,edges=0;
    reg t65_old=1;
    task automatic step(input bit enable,input bit live,input bit expected,input bit safe);
        @(negedge clk);t65_enable=enable;live_nmi_n=live;
        #0.01;
        if(enable) begin
            checks++;
            if(t65_nmi_n!==expected)
                $fatal(1,"NMI FAIL sample shown=%b expected=%b safe=%b expected=%b",t65_nmi_n,expected,exact_switch_safe,safe);
        end
        @(posedge clk);
        if(!reset && enable) begin
            if(t65_old && !t65_nmi_n) edges++;
            t65_old=t65_nmi_n;
        end
        #0.01;t65_enable=0;
    endtask
    task automatic edge_count(input integer expected);
        checks++;if(edges!=expected)$fatal(1,"NMI FAIL edge count %d expected %d",edges,expected);
    endtask
    initial begin
        repeat(3) @(negedge clk);reset=0;
        step(1,1,1,1); // Exact sample initializes the same unreset history as T65.
        @(negedge clk);turbo=1;
        step(0,0,0,0);step(0,1,0,0);step(1,1,0,0);edge_count(1);
        step(1,1,1,1);edge_count(1);
        // Last sample LOW: high/low wholly inside a stall needs HIGH rearm.
        step(1,0,0,1);edge_count(2);
        step(0,1,0,0);step(0,0,0,0);step(1,0,1,0);edge_count(2);
        step(1,0,0,1);edge_count(3);
        repeat(6) step(1,0,0,1);edge_count(3);
        // A short pulse after LOW, now live HIGH, still needs the rearm.
        step(0,1,0,0);step(0,0,0,0);step(0,1,0,0);
        step(1,1,1,0);step(1,1,0,0);edge_count(4);
        step(1,1,1,1);edge_count(4);
        // Same-edge falling input is included, not replayed a second time.
        step(1,0,0,1);step(1,0,0,1);edge_count(5);
        step(0,1,0,0);step(1,0,1,0);step(1,0,0,1);edge_count(6);
        // Multiple falls in one stall coalesce into the one pending NMI.
        step(1,1,1,1);step(0,0,0,0);step(0,1,0,0);step(0,0,0,0);
        step(1,0,0,1);step(1,0,0,1);edge_count(7);
        // S9b: held LOW/live HIGH switches immediately, exact restores B0.
        @(negedge clk);turbo=0;exact_nmi_n=1;
        step(1,1,1,0);edge_count(7);
        if(carry_active)$fatal(1,"carry must settle when showing B0");
        // S9/S10: a pending forced LOW crosses the normal switch boundary.
        @(negedge clk);turbo=1;
        step(1,0,0,0);edge_count(8);
        step(0,1,0,0);step(0,0,0,0);step(0,1,0,0);
        step(1,1,1,0);
        if(!pending_rearm)$fatal(1,"rearm must be pending");
        @(negedge clk);turbo=0;exact_nmi_n=1;
        step(1,1,0,0);edge_count(9);
        if(!carry_active)$fatal(1,"forced low over B0 high must retain carry");
        // S11: a new falling edge during carry extends it beyond two enables.
        step(1,0,1,0);
        if(!pending_rearm || !carry_active)$fatal(1,"new edge must arm carry");
        step(1,0,0,0);edge_count(10);
        if(!carry_active)$fatal(1,"carry cannot end while differing from B0");
        @(negedge clk);exact_nmi_n=0;
        step(1,0,0,0);
        if(carry_active)$fatal(1,"carry must end at matching B0");
        // S12: exact->turbo preserves force created by another carry edge.
        @(negedge clk);turbo=1;
        step(0,1,0,0);step(0,0,0,0);step(0,1,0,0);step(1,1,1,0);
        @(negedge clk);turbo=0;exact_nmi_n=1;
        step(1,1,0,0);edge_count(11);step(1,0,1,0);
        @(negedge clk);turbo=1;
        step(1,0,0,0);edge_count(12);
        checks++;if(carry_active)$fatal(1,"turbo adoption must clear carry bookkeeping");
        step(1,1,1,0);
        @(negedge clk);turbo=0;exact_nmi_n=1;
        step(1,1,1,0);edge_count(12);
        if(carry_active)$fatal(1,"carry must settle after return");
        $display("NMI PASS checks=%0d",checks);$finish;
    end
endmodule
