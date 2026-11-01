// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// The runner extracts the actual source register and synchronizer instance
// from multitini_one_top. This bench drives independently phased clocks and
// deliberately places short reconvergence pulses across destination edges.
module tb_amiga_quiet_cdc;
    reg control_clk=0,c64_clk=0;
    always #5 control_clk=~control_clk;
    always #17.62 c64_clk=~c64_clk;
    reg control_aresetn=0,c64_aresetn=0;
    reg genesis_source_quiescent=0,framebuffer_display_quiescent=0,amiga_switch_control=0;
    wire amiga_quiet_source;
    amiga_quiet_production dut(.*);

    reg expected_control=0,expected_meta=0,expected_source=0;
    reg forbid_ack=1;
    integer checked_edges=0,glitches=0,owner_cases=0;
    always @(posedge control_clk)
        if(!control_aresetn)expected_control<=0;
        else expected_control<=genesis_source_quiescent && framebuffer_display_quiescent && amiga_switch_control;
    always @(posedge c64_clk)begin
        if(!c64_aresetn)begin expected_meta<=0;expected_source<=0;end
        else begin expected_meta<=expected_control;expected_source<=expected_meta;end
        #0.001;
        checked_edges=checked_edges+1;
        if(forbid_ack && amiga_quiet_source)
            $fatal(1,"Unregistered quiescent glitch reached destination acknowledgement");
        if(amiga_quiet_source!==expected_source)
            $fatal(1,"Quiescent CDC latency/reset mismatch: got%b expected%b",amiga_quiet_source,expected_source);
    end

    task automatic owner(input integer which,input reg value);
        case(which)
            0:genesis_source_quiescent=value;
            1:framebuffer_display_quiescent=value;
            2:amiga_switch_control=value;
        endcase
    endtask
    task automatic settle(input reg value);
        begin
            repeat(4)@(negedge c64_clk);
            if(amiga_quiet_source!==value)$fatal(1,"Quiescence did not settle to%b",value);
        end
    endtask
    task automatic runt(input integer which);
        real phase;
        reg found;
        begin
            found=0;
            // Find a destination rising edge at least2ns from either source
            // rising edge. A2ns pulse spans that edge, with no source sample.
            while(!found)begin
                @(negedge c64_clk);
                phase=$realtime+17.62-5.0;
                while(phase>=10.0)phase=phase-10.0;
                if(phase>=2.0 && phase<=8.0)found=1;
            end
            #16.62;owner(which,1);
            #2;owner(which,0);
            glitches=glitches+1;
            settle(0);
        end
    endtask

    integer i;
    initial begin
        repeat(4)@(negedge c64_clk);
        control_aresetn=1;c64_aresetn=1;
        settle(0);
        for(i=0;i<3;i=i+1)begin
            @(negedge control_clk);
            genesis_source_quiescent=1;framebuffer_display_quiescent=1;amiga_switch_control=1;
            owner(i,0);forbid_ack=1;
            settle(0);
            runt(i);
            @(negedge control_clk);forbid_ack=0;owner(i,1);
            settle(1);owner_cases=owner_cases+1;
            @(negedge control_clk);owner(i,0);
            settle(0);forbid_ack=1;
        end
        // A source reset withdraws the acknowledgement while all owners are
        // quiet; destination reset independently clears its two stages.
        @(negedge control_clk);
        genesis_source_quiescent=1;framebuffer_display_quiescent=1;amiga_switch_control=1;forbid_ack=0;
        settle(1);
        @(negedge control_clk);control_aresetn=0;
        settle(0);forbid_ack=1;
        repeat(3)@(negedge c64_clk);
        @(negedge control_clk);control_aresetn=1;forbid_ack=0;
        settle(1);
        @(negedge c64_clk);c64_aresetn=0;
        settle(0);forbid_ack=1;
        @(negedge c64_clk);c64_aresetn=1;forbid_ack=0;
        settle(1);
        if(glitches!=3 || owner_cases!=3 || checked_edges<50)$fatal(1,"Incomplete CDC coverage");
        $display("PASS Amiga registered quiescence CDC:3owners,3destination-edge glitches,source/destination reset,%0d checked edges",checked_edges);
        $finish;
    end
    initial begin #100000;$fatal(1,"CDC fixture timeout");end
endmodule
