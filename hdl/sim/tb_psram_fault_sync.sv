// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_psram_fault_sync;
    reg control_clk=0, core_clk=0, control_resetn=0, core_resetn=0;
    reg fault_request=0, source_fault=0, core_running=1;
    real core_half=15.858;
    always #3.7495 control_clk=~control_clk;
    always begin #(core_half); if(core_running) core_clk=~core_clk; end
    always @(posedge control_clk)
        if(!control_resetn) source_fault<=0;
        else if(fault_request) source_fault<=1;

    wire memory_fault;
    multitini_gameboy_psram_path dut(
        .core_clk(core_clk), .core_resetn(core_resetn), .control_clk(control_clk),
        .control_resetn(control_resetn), .memory_fault(memory_fault));
    // Isolate the crossing from the physical memory protocol. The source
    // models the line service's sticky control-clock fault output.
    initial force dut.memory_fault_control=source_fault;

    reg previous_sample=0, expected_fault=0;
    realtime last_core_edge=-1;
    integer checks=0, scenarios=0;
    always @(posedge core_clk) begin
        last_core_edge=$realtime;
        if(!core_resetn) begin previous_sample=0; expected_fault=0; end
        else begin expected_fault=previous_sample; previous_sample=source_fault; end
        #0.001;
        if(memory_fault!==expected_fault)
            $fatal(1,"fault crossing changed synchronous reset or two-edge behavior: actual=%b expected=%b",memory_fault,expected_fault);
        checks++;
    end
    always @(memory_fault) if($time>0 && $realtime!=last_core_edge)
        $fatal(1,"fault output changed away from a core edge");
    task automatic settle;
        repeat(4) @(negedge core_clk);
    endtask

    real periods[6];
    initial begin
        periods[0]=15.858; periods[1]=14.901; periods[2]=5.208;
        periods[3]=4.699; periods[4]=4.656; periods[5]=3.700;
        #0.001;
        if(memory_fault!==0) $fatal(1,"fault power-up value is not zero");
        settle();
        for(int rate=0;rate<6;rate++) begin
            core_half=periods[rate];
            for(int phase=0;phase<8;phase++) begin
                core_resetn=0; control_resetn=0; fault_request=0;
                settle();
                #(core_half*(phase+0.25)/8.0);
                control_resetn=1; core_resetn=1;
                settle();
                if(memory_fault) $fatal(1,"spurious fault after reset");
                #(core_half*(phase+0.75)/8.0);
                fault_request=1;
                repeat(2) @(negedge control_clk);
                fault_request=0;
                settle();
                if(!memory_fault) $fatal(1,"sticky source fault was lost");
                // Destination-only reset clears at the next core edge, then
                // the unchanged sticky source reappears in two samples.
                #(core_half/4.0); core_resetn=0;
                #0.001;
                if(!memory_fault) $fatal(1,"destination reset became asynchronous");
                settle();
                core_resetn=1; settle();
                if(!memory_fault) $fatal(1,"held source fault lost across core reset");
                // Reset remains synchronous while the destination is stopped.
                @(negedge core_clk); core_running=0;
                #1; core_resetn=0;
                #(4*core_half);
                if(!memory_fault) $fatal(1,"stopped-clock reset changed behavior");
                core_running=1; settle();
                core_resetn=1; settle();
                if(!memory_fault) $fatal(1,"resume lost the sticky fault");
                control_resetn=0; settle();
                if(memory_fault) $fatal(1,"source reset fault-clear did not cross");
                scenarios++;
            end
        end
        $display("PASS PSRAM fault crossing: %0d clock/phase scenarios, %0d sampled-edge checks; synchronous reset, sticky reassertion, stopped core",scenarios,checks);
        $finish;
    end
    initial begin #1000000; $fatal(1,"fault test timeout"); end
endmodule
