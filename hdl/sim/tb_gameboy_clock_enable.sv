// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_gameboy_clock_enable;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic stall = 1'b0;
    logic pause_request = 1'b0;
    logic ce_cpu;
    logic ce_cpu_n;
    logic ce_cpu_2x;
    logic [2:0] phase;
    integer ce_count = 0;
    integer ce_n_count = 0;
    integer ce_2x_count = 0;

    always #5 clk = !clk;
    multitini_gameboy_clock_enable dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    always @(posedge clk) begin
        if (ce_cpu) ce_count <= ce_count + 1;
        if (ce_cpu_n) ce_n_count <= ce_n_count + 1;
        if (ce_cpu_2x) ce_2x_count <= ce_2x_count + 1;
    end

    initial begin
        repeat (3) @(negedge clk);
        reset = 1'b0;
        repeat (17) @(negedge clk);
        check_value(ce_count == 2 && ce_n_count == 2 && ce_2x_count == 4,
                    "normal 1x and 2x cadence");

        stall = 1'b1;
        // Registered enable pulses are observed by consumers on the following
        // edge; allow the last pre-stall pulse to retire before sampling.
        @(negedge clk);
        begin
            logic [2:0] held_phase;
            integer held_ce;
            integer held_ce_n;
            integer held_ce_2x;
            held_phase = phase;
            held_ce = ce_count;
            held_ce_n = ce_n_count;
            held_ce_2x = ce_2x_count;
            repeat (11) @(negedge clk);
            check_value(phase == held_phase, "stall holds phase");
            check_value(ce_count == held_ce && ce_n_count == held_ce_n &&
                        ce_2x_count == held_ce_2x,
                        "stall suppresses all clock enables");
        end
        stall = 1'b0;
        repeat (8) @(negedge clk);
        check_value(ce_count == 3 && ce_n_count == 3 && ce_2x_count == 6,
                    "cadence resumes from held phase");

        // Enter the menu at every master-clock phase. A pending registered
        // enable may retire once; all later enables stop without resetting
        // phase. An outstanding memory stall must still win when it closes.
        for (integer target = 0; target < 8; target++) begin
            while (phase != target[2:0]) @(negedge clk);
            pause_request = 1'b1;
            @(negedge clk);
            repeat (11) begin
                check_value(phase == target[2:0], "pause preserves phase");
                check_value(!ce_cpu && !ce_cpu_n && !ce_cpu_2x,
                            "pause suppresses all machine enables");
                @(negedge clk);
            end
            stall = 1'b1;
            pause_request = 1'b0;
            repeat (5) begin
                @(negedge clk);
                check_value(phase == target[2:0] && !ce_cpu &&
                            !ce_cpu_n && !ce_cpu_2x,
                            "closing menu cannot bypass a memory stall");
            end
            stall = 1'b0;
            @(negedge clk);
            check_value(phase == ((target + 1) & 7),
                        "resume advances the saved phase exactly once");
            check_value(ce_cpu == (target == 0) &&
                        ce_cpu_n == (target == 4) &&
                        ce_cpu_2x == (target == 0 || target == 4),
                        "resume emits the next scheduled enable");
        end

        $display("PASS: Game Boy memory stalls and menu pause at all phases");
        $finish;
    end
endmodule
