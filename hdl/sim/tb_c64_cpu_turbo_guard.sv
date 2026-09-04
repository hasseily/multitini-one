// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_cpu_turbo_guard;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic turbo_request = 1'b0;
    logic c64_iec_atn = 1'b1;
    logic c64_iec_data = 1'b1;
    logic c64_iec_clk = 1'b1;
    logic drive_iec_data = 1'b1;
    logic drive_iec_clk = 1'b1;
    logic turbo_active;

    always #5 clk = !clk;

    multitini_c64_cpu_turbo_guard #(
        .CORE_CLOCK_HZ(32),
        .IEC_HOLDOFF_CYCLES(4)
    ) dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic step;
        @(posedge clk);
        #1;
    endtask

    initial begin
        repeat (2) step();
        reset = 1'b0;
        step();
        check_value(!turbo_active, "turbo defaults off");

        turbo_request = 1'b1;
        #1;
        check_value(turbo_active,
                    "idle bus permits independently requested turbo");

        @(negedge clk);
        c64_iec_clk = 1'b0;
        #1;
        check_value(!turbo_active,
                    "an IEC edge immediately suspends CPU turbo");
        repeat (3) begin
            step();
            check_value(!turbo_active,
                        "an asserted IEC line keeps CPU turbo suspended");
        end

        c64_iec_clk = 1'b1;
        repeat (4) begin
            step();
            check_value(!turbo_active,
                        "CPU turbo observes the full post-IEC holdoff");
        end
        step();
        check_value(turbo_active,
                    "CPU turbo resumes after the idle holdoff");

        @(negedge clk);
        drive_iec_data = 1'b0;
        #1;
        check_value(!turbo_active,
                    "drive-side IEC activity also suspends CPU turbo");
        turbo_request = 1'b0;
        drive_iec_data = 1'b1;
        repeat (6) step();
        check_value(!turbo_active,
                    "IEC state cannot implicitly request CPU turbo");

        $display("PASS: independent guarded C64 CPU turbo");
        $finish;
    end
endmodule
