// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_async_mailbox;
    logic src_clk = 1'b0;
    logic src_resetn = 1'b0;
    logic [81:0] src_data = '0;
    logic src_valid = 1'b0;
    logic src_ready;
    logic dst_clk = 1'b0;
    logic dst_resetn = 1'b0;
    logic [81:0] dst_data;
    logic dst_valid;
    logic dst_ready = 1'b0;
    integer received = 0;

    always #3.5 src_clk = !src_clk;
    always #8.0 dst_clk = !dst_clk;

    multitini_async_mailbox #(.WIDTH(82)) dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic submit(input logic [81:0] value);
        while (!src_ready) @(negedge src_clk);
        src_data = value;
        src_valid = 1'b1;
        @(negedge src_clk);
        src_valid = 1'b0;
        src_data = 'x;
    endtask

    always @(posedge dst_clk) begin
        if (dst_valid && dst_ready) begin
            case (received)
                0: check_value(dst_data == 82'h3_01234567_89ABCDEF_5AA5,
                               "first coherent payload");
                1: check_value(dst_data == 82'h2_FEDCBA98_76543210_A55A,
                               "second coherent payload");
                default: check_value(1'b0, "unexpected mailbox transfer");
            endcase
            received <= received + 1;
        end
    end

    initial begin
        fork
            begin
                #10000;
                $display("FAIL: asynchronous mailbox timeout");
                $fatal(1);
            end
        join_none

        repeat (4) @(negedge src_clk);
        src_resetn = 1'b1;
        repeat (3) @(negedge dst_clk);
        dst_resetn = 1'b1;

        submit(82'h3_01234567_89ABCDEF_5AA5);
        repeat (5) @(negedge dst_clk);
        check_value(dst_valid, "destination holds valid under backpressure");
        dst_ready = 1'b1;
        repeat (6) @(negedge dst_clk);
        dst_ready = 1'b0;
        check_value(received == 1,
                    "held ready accepts the first payload exactly once");

        submit(82'h2_FEDCBA98_76543210_A55A);
        repeat (2) @(negedge dst_clk);
        dst_ready = 1'b1;
        wait (received == 2);
        repeat (6) @(negedge dst_clk);
        check_value(received == 2,
                    "held ready accepts the second payload exactly once");
        while (!src_ready) @(negedge src_clk);
        check_value(src_ready, "source acknowledged after two transfers");

        $display("PASS: asynchronous mailbox");
        $finish;
    end
endmodule
