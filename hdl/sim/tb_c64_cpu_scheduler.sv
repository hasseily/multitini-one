// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_cpu_scheduler;
    logic clk = 0, reset = 1, hold = 0;
    logic [4:0] phase = 0;
    logic memory_access = 1, io_enable = 1;
    logic [2:0] extra_mask = 0;
    logic dense_mode = 0, extra_allowed = 1;
    wire request, retire;
    integer checks = 0, requests = 0, retirements = 0;

    always #5 clk = !clk;
    multitini_c64_cpu_scheduler dut (.*);

    task automatic tick(input integer next_phase, input logic expected_request,
                        input logic expected_retire);
        @(negedge clk); phase = next_phase;
        #1;
        checks++;
        if (request !== expected_request || retire !== expected_retire)
            $fatal(1, "FAIL: scheduler phase=%0d request=%b expected=%b retire=%b expected=%b",
                   phase, request, expected_request, retire, expected_retire);
        if (request) requests++;
        if (retire) retirements++;
        @(posedge clk); #1;
    endtask

    task automatic flush;
        reset = 1; hold = 0;
        tick(0, 0, 0); tick(0, 0, 0);
        reset = 0;
    endtask

    task automatic frame(input logic [31:0] issue_phases,
                         input logic [31:0] complete_phases);
        for (integer index = 0; index < 32; index++)
            tick(index, issue_phases[index], complete_phases[index]);
    endtask

    initial begin
        flush();
        // Independent published phase schedules: the stock machine always
        // issues at CPUC (28) and completes at CPUF (31), including I/O.
        frame(32'h10000000, 32'h80000000);
        extra_mask = 1; frame(32'h10010000, 32'h80080000);
        extra_mask = 2; frame(32'h10100000, 32'h80800000);
        extra_mask = 4; frame(32'h11000000, 32'h88000000);
        extra_mask = 7; frame(32'h11110000, 32'h88880000);
        dense_mode = 1; frame(32'h15550000, 32'h8AAA0000);

        // DMA, VIC bus ownership, CPU port and cartridge gating is supplied
        // through extra_allowed. Each must remove extras without removing
        // the native opportunity. I/O cannot gain additional side effects.
        extra_allowed = 0; frame(32'h10000000, 32'h80000000);
        extra_allowed = 1; memory_access = 0;
        frame(32'h10000000, 32'h80000000);
        io_enable = 0; frame(32'h00000000, 32'h00000000);
        memory_access = 1; dense_mode = 0; extra_mask = 0;
        frame(32'h10000000, 32'h80000000);
        io_enable = 1;

        // Waiting at an issue edge suppresses it. Releasing the same phase
        // accepts exactly one request, then retirement follows once.
        flush(); hold = 1; tick(28, 0, 0); tick(28, 0, 0);
        hold = 0; tick(28, 1, 0); tick(29, 0, 0); tick(30, 0, 0); tick(31, 0, 1);
        tick(0, 0, 0);

        // Wait after issue freezes the accepted transaction. It may neither
        // disappear nor retire repeatedly while the machine phase is held.
        flush(); tick(28, 1, 0);
        hold = 1; repeat (4) tick(29, 0, 0);
        hold = 0; tick(29, 0, 0); tick(30, 0, 0);
        hold = 1; repeat (4) tick(31, 0, 0);
        hold = 0; tick(31, 0, 1); tick(0, 0, 0);

        // A fast transaction already issued survives a live mode/gate change
        // and a wait. Subsequent opportunities use the new settings.
        flush(); dense_mode = 1; extra_mask = 7;
        tick(16, 1, 0);
        hold = 1; extra_allowed = 0; dense_mode = 0;
        repeat (3) tick(17, 0, 0);
        hold = 0; tick(17, 0, 1); tick(18, 0, 0);
        tick(20, 0, 0); tick(24, 0, 0);

        // Switching to dense mode cannot overlap a slow request that was
        // already accepted. Its intermediate fast slot must be skipped.
        extra_allowed = 1; dense_mode = 0; extra_mask = 1;
        flush(); tick(16, 1, 0);
        dense_mode = 1; tick(17, 0, 0); tick(18, 0, 0); tick(19, 0, 1);
        tick(20, 1, 0); tick(21, 0, 1); tick(22, 1, 0); tick(23, 0, 1);

        // Reset cancels both latency pipelines, including a held retirement.
        extra_allowed = 1; dense_mode = 1;
        flush(); tick(16, 1, 0);
        hold = 1; reset = 1; tick(17, 0, 0);
        reset = 0; hold = 0; tick(17, 0, 0);
        dense_mode = 0; extra_mask = 0;
        tick(28, 1, 0); tick(29, 0, 0);
        reset = 1; tick(30, 0, 0);
        reset = 0; tick(31, 0, 0); tick(0, 0, 0);
        if (requests - retirements != 2)
            $fatal(1, "FAIL: exactly two reset-cancelled transactions expected");
        $display("PASS: C64 CPU scheduler checks=%0d requests=%0d retired=%0d reset_cancelled=2",
                 checks, requests, retirements);
        $finish;
    end
    initial begin #10000; $fatal(1, "FAIL: scheduler timeout"); end
endmodule
