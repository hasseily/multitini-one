// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_psram_bist;
    logic clk = 1'b0;
    logic resetn = 1'b0;
    logic start = 1'b0;
    logic [20:0] line_count = 21'd16;
    logic [31:0] seed = 32'h12345678;
    logic busy;
    logic done;
    logic passed;
    logic [31:0] error_count;
    logic [23:0] first_bad_addr;
    logic [63:0] first_bad_expected;
    logic [63:0] first_bad_actual;
    logic [31:0] max_read_latency;
    logic [31:0] max_write_latency;
    logic [31:0] max_turnaround;
    logic command_valid;
    logic command_ready;
    logic [7:0] command;
    logic [23:0] command_addr;
    logic [63:0] command_wdata;
    logic response_valid = 1'b0;
    logic [63:0] response_rdata = 64'd0;

    logic [63:0] memory [0:31];
    logic backend_busy = 1'b0;
    logic [3:0] delay_count = 4'd0;
    logic [2:0] cooldown = 3'd0;
    logic [7:0] accepted_command = 8'd0;
    logic [23:0] accepted_addr = 24'd0;
    logic [63:0] accepted_wdata = 64'd0;
    logic corrupt_reads = 1'b0;

    always #5 clk = !clk;
    assign command_ready = !backend_busy && cooldown == 3'd0;

    always_ff @(posedge clk) begin
        response_valid <= 1'b0;
        if (!resetn) begin
            backend_busy <= 1'b0;
            delay_count <= 4'd0;
            cooldown <= 3'd0;
        end else if (!backend_busy && cooldown != 3'd0) begin
            cooldown <= cooldown - 3'd1;
        end else if (!backend_busy && command_valid && command_ready) begin
            backend_busy <= 1'b1;
            accepted_command <= command;
            accepted_addr <= command_addr;
            accepted_wdata <= command_wdata;
            delay_count <= command == 8'hEB ? 4'd5 : 4'd3;
        end else if (backend_busy && delay_count != 4'd0) begin
            delay_count <= delay_count - 4'd1;
        end else if (backend_busy) begin
            if (accepted_command == 8'h02)
                memory[accepted_addr[7:3]] <= accepted_wdata;
            if (accepted_command == 8'hEB)
                response_rdata <= memory[accepted_addr[7:3]] ^
                    ((corrupt_reads && accepted_addr == 24'd40) ? 64'd1 : 64'd0);
            response_valid <= 1'b1;
            backend_busy <= 1'b0;
            cooldown <= 3'd2;
        end
    end

    multitini_psram_bist dut (
        .clk(clk), .resetn(resetn), .start(start), .line_count(line_count),
        .seed(seed), .busy(busy), .done(done), .passed(passed),
        .error_count(error_count), .first_bad_addr(first_bad_addr),
        .first_bad_expected(first_bad_expected),
        .first_bad_actual(first_bad_actual),
        .max_read_latency(max_read_latency),
        .max_write_latency(max_write_latency),
        .max_turnaround(max_turnaround), .command_valid(command_valid),
        .command_ready(command_ready), .command(command),
        .command_addr(command_addr), .command_wdata(command_wdata),
        .response_valid(response_valid), .response_rdata(response_rdata)
    );

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic run_test;
        @(negedge clk);
        start = 1'b1;
        @(negedge clk);
        start = 1'b0;
        wait (done);
        @(negedge clk);
    endtask

    initial begin
        repeat (4) @(negedge clk);
        resetn = 1'b1;
        run_test();
        check_value(passed && error_count == 0, "clean memory passes");
        check_value(max_read_latency >= 6 && max_write_latency >= 4,
                    "operation latency measured");
        check_value(max_turnaround > 0, "turnaround measured");

        corrupt_reads = 1'b1;
        seed = 32'h89ABCDEF;
        run_test();
        check_value(!passed && error_count == 1, "corruption fails test");
        check_value(first_bad_addr == 24'd40, "first bad address captured");
        check_value(first_bad_actual != first_bad_expected,
                    "bad values captured");

        line_count = 21'd0;
        run_test();
        check_value(!passed && error_count == 0, "zero-length request rejected");
        $display("PASS: PSRAM BIST sequencer");
        $finish;
    end
endmodule
