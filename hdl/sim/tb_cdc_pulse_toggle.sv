// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_cdc_pulse_toggle;
    logic src_clk = 1'b0;
    logic dst_clk = 1'b0;
    logic src_resetn = 1'b0;
    logic dst_resetn = 1'b0;
    logic src_pulse = 1'b0;
    logic dst_pulse;
    integer received = 0;

    always #7 src_clk = !src_clk;
    always #11 dst_clk = !dst_clk;

    multitini_cdc_pulse_toggle dut (
        .src_clk(src_clk), .src_resetn(src_resetn), .src_pulse(src_pulse),
        .dst_clk(dst_clk), .dst_resetn(dst_resetn), .dst_pulse(dst_pulse)
    );

    task automatic send_pulse;
        @(negedge src_clk);
        src_pulse = 1'b1;
        @(negedge src_clk);
        src_pulse = 1'b0;
    endtask

    always @(posedge dst_clk) begin
        if (dst_pulse)
            received <= received + 1;
    end

    initial begin
        repeat (3) @(negedge src_clk);
        src_resetn = 1'b1;
        repeat (3) @(negedge dst_clk);
        dst_resetn = 1'b1;

        send_pulse();
        wait (received == 1);
        repeat (4) @(negedge dst_clk);
        send_pulse();
        wait (received == 2);
        repeat (4) @(negedge dst_clk);

        if (received != 2) begin
            $display("FAIL: pulse count");
            $fatal(1);
        end
        $display("PASS: CDC pulse toggle");
        $finish;
    end

    initial begin
        #10000;
        $display("FAIL: timeout");
        $fatal(1);
    end
endmodule
