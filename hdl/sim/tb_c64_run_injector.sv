// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_run_injector;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic start = 1'b0;
    logic [10:0] host_ps2_key = 11'h52a;
    logic [10:0] ps2_key;
    logic busy;
    integer event_count = 0;
    logic last_toggle;
    logic monitor_active = 1'b0;

    always #5 clk = !clk;

    multitini_c64_run_injector #(.EVENT_DELAY_CYCLES(2)) dut (.*);

    task automatic check(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    always @(negedge clk) begin
        if (!reset && monitor_active && ps2_key[10] != last_toggle) begin
            case (event_count)
                0, 1: check(ps2_key[7:0] == 8'h2d, "R event");
                2, 3: check(ps2_key[7:0] == 8'h3c, "U event");
                4, 5: check(ps2_key[7:0] == 8'h31, "N event");
                6, 7: check(ps2_key[7:0] == 8'h5a, "Return event");
                default: check(1'b0, "unexpected RUN event");
            endcase
            check(ps2_key[9] == !event_count[0],
                  "alternating press and release");
            event_count = event_count + 1;
            last_toggle = ps2_key[10];
        end
    end

    initial begin
        repeat (2) @(negedge clk);
        reset = 1'b0;
        last_toggle = ps2_key[10];
        @(negedge clk);
        check(ps2_key == host_ps2_key, "host input passes while idle");
        last_toggle = ps2_key[10];
        start = 1'b1;
        @(negedge clk);
        start = 1'b0;
        last_toggle = ps2_key[10];
        @(posedge clk); #1;
        monitor_active = 1'b1;
        wait (!busy);
        @(negedge clk);
        @(posedge clk); #1;
        check(event_count == 8, "RUN generated eight key transitions");
        check(ps2_key == host_ps2_key, "host input restored");
        $display("PASS: C64 PRG RUN injector");
        $finish;
    end
endmodule
