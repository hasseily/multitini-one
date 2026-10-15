// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
module tb_apple_iigs_native_clock;
    reg core_clk=0,reset=1;
    always #8.730 core_clk=~core_clk;
    wire [1:0] phase;
    reg [1:0] reference_phase=0;
    reg original_divider=0;
    reg [11:0] reset_count=12'hfff;
    reg native_reset=1;
    wire candidate;
    wire unused_clk28;
    native_clock_extracted dut(.core_clk(core_clk),.reset(reset),.clk14(candidate),.clk28(unused_clk28),.phase_value(phase));
    always @(posedge core_clk)begin
        if(reset)begin reference_phase<=0;original_divider<=0;reset_count<=12'hfff;end
        else begin
            reference_phase<=reference_phase+1'b1;
            original_divider<=(reference_phase==3 || reference_phase==0);
            if(reset_count!=0)reset_count<=reset_count-1'b1;
        end
    end
    always @(posedge core_clk or posedge reset)begin
        if(reset)native_reset<=1;
        else if(phase==0)native_reset<=(reset_count!=0);
    end
    integer original_edges=0,candidate_edges=0,original_live_edges=0,candidate_live_edges=0;
    integer live_comparisons=0,pulse_checks=0,omitted_startup=0,reset_tests=0;
    integer epoch_original=0,epoch_candidate=0;
    realtime original_rise=-100,candidate_rise=-100;
    always @(posedge original_divider)begin
        original_rise=$realtime;original_edges++;
        epoch_original++;
        if(!native_reset)original_live_edges++;
    end
    always @(posedge candidate)begin
        candidate_rise=$realtime;candidate_edges++;
        epoch_candidate++;
        if(!native_reset)candidate_live_edges++;
    end
    always @(negedge candidate)begin
        if(candidate_rise>=0)begin
            if($realtime-candidate_rise<8.729 || $realtime-candidate_rise>8.731)
                $fatal(1,"candidate pulse width changed: %0t",$realtime-candidate_rise);
            pulse_checks++;
        end
    end
    reg [1:0] sampled_phase;
    reg sampled_reset,sampled_native_reset;
    integer previous_original,previous_candidate;
    always @(posedge core_clk)begin
        sampled_phase=phase;sampled_reset=reset;sampled_native_reset=native_reset;
        previous_original=original_edges;previous_candidate=candidate_edges;
        #0.003;
        if(phase!==reference_phase)$fatal(1,"production phase counter differs from reference");
        if(!sampled_reset && !sampled_native_reset)begin
            if((original_edges-previous_original)!=(candidate_edges-previous_candidate))
                $fatal(1,"live edge mismatch phase=%0d original=%0d candidate=%0d",sampled_phase,original_edges-previous_original,candidate_edges-previous_candidate);
            if((candidate_edges-previous_candidate)!=(sampled_phase==3))
                $fatal(1,"candidate native positive edge was not phase3");
            if(sampled_phase==3 && (candidate_rise-original_rise < -0.001 || candidate_rise-original_rise > 0.002))
                $fatal(1,"native edge does not align with primary phase3");
            live_comparisons++;
        end
    end
    task release_and_check;
        integer start_original,start_candidate;
        begin
            epoch_original=0;epoch_candidate=0;
            @(negedge core_clk);reset=0;
            repeat(4204)@(negedge core_clk);
            if(native_reset)$fatal(1,"native reset did not release");
            // Current divider has one reset-release phase0 edge. The SYNC
            // gate omits only this edge while native reset remains asserted.
            if(epoch_original!=epoch_candidate+1)
                $fatal(1,"startup difference exceeded one reset-held edge: %0d %0d",epoch_original,epoch_candidate);
            if(epoch_candidate<1024)$fatal(1,"insufficient native reset settling edges");
            omitted_startup++;reset_tests++;
        end
    endtask
    integer selected;
    initial begin
        repeat(3)@(negedge core_clk);
        release_and_check();
        for(selected=0;selected<4;selected++)begin
            while(phase!=selected)@(negedge core_clk);
            reset=1;
            repeat(3)@(negedge core_clk);
            release_and_check();
        end
        #1;
        if(original_live_edges!=candidate_live_edges || live_comparisons<500 || pulse_checks<5000)
            $fatal(1,"edge/pulse coverage incomplete");
        $display("PASS: phase3 positive edges match after held reset; original startup phase0 edge explicitly omitted");
        $display("CLOCK_SUMMARY reset_tests=%0d startup_omissions=%0d live_edges=%0d live_comparisons=%0d pulse_width_checks=%0d",reset_tests,omitted_startup,candidate_live_edges,live_comparisons,pulse_checks);
        $finish;
    end
    initial begin #1000000;$fatal(1,"watchdog");end
endmodule
