// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_psram_line_service;
    logic clk = 1'b0;
    logic resetn = 1'b0;
    logic initialized;
    logic fault;
    logic [23:0] request_addr = 24'd0;
    logic request_write = 1'b0;
    logic [63:0] request_wdata = 64'd0;
    logic [7:0] request_wstrb = 8'd0;
    logic request_valid = 1'b0;
    logic request_ready;
    logic [63:0] response_rdata;
    logic response_valid;
    logic command_valid;
    logic command_ready = 1'b1;
    logic [7:0] command;
    logic [23:0] command_addr;
    logic [63:0] command_wdata;
    logic driver_response_valid = 1'b0;
    logic [63:0] driver_response_rdata = 64'd0;
    logic pending_response = 1'b0;
    logic [7:0] pending_command = 8'd0;
    logic [63:0] memory_line = 64'h8877665544332211;
    logic [7:0] command_log [0:15];
    integer command_count = 0;

    always #5 clk = !clk;
    multitini_psram_line_service dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    always @(posedge clk) begin
        driver_response_valid <= 1'b0;
        if (pending_response) begin
            driver_response_valid <= 1'b1;
            driver_response_rdata <= memory_line;
            pending_response <= 1'b0;
        end
        if (resetn && command_valid && command_ready) begin
            command_log[command_count] <= command;
            command_count <= command_count + 1;
            pending_command <= command;
            pending_response <= 1'b1;
            if (command == 8'h02)
                memory_line <= command_wdata;
        end
    end

    task automatic request(
        input logic write,
        input logic [23:0] address,
        input logic [63:0] data,
        input logic [7:0] strobes
    );
        while (!request_ready) @(negedge clk);
        request_write = write;
        request_addr = address;
        request_wdata = data;
        request_wstrb = strobes;
        request_valid = 1'b1;
        @(negedge clk);
        request_valid = 1'b0;
        while (!response_valid) @(negedge clk);
    endtask

    initial begin
        repeat (3) @(negedge clk);
        resetn = 1'b1;
        wait (initialized);
        @(negedge clk);
        check_value(command_log[0] == 8'h35 && command_log[1] == 8'hc0,
                    "QPI initialization sequence");

        request(1'b0, 24'h000100, 64'd0, 8'd0);
        check_value(response_rdata == 64'h8877665544332211,
                    "line read response");
        request(1'b1, 24'h000100, 64'h0123456789abcdef, 8'hff);
        check_value(memory_line == 64'h0123456789abcdef,
                    "full line write");
        request(1'b1, 24'h000100, 64'hffeeddccbbaa9988, 8'b00100101);
        check_value(memory_line == 64'h0123dd6789aacd88,
                    "partial write read/merge/write");
        check_value(command_log[2] == 8'heb && command_log[3] == 8'h02 &&
                    command_log[4] == 8'heb && command_log[5] == 8'h02,
                    "data command ordering");

        request(1'b0, 24'h000101, 64'd0, 8'd0);
        check_value(fault, "unaligned request sets sticky fault");

        $display("PASS: PSRAM line service");
        $finish;
    end
endmodule
