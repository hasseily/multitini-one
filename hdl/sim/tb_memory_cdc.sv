// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_memory_cdc;
    logic core_clk = 1'b0;
    logic core_resetn = 1'b0;
    logic [23:0] core_request_addr = 24'd0;
    logic core_request_write = 1'b0;
    logic [63:0] core_request_wdata = 64'd0;
    logic [7:0] core_request_wstrb = 8'd0;
    logic core_request_valid = 1'b0;
    logic core_request_ready;
    logic [63:0] core_response_rdata;
    logic core_response_valid;
    logic memory_clk = 1'b0;
    logic memory_resetn = 1'b0;
    logic [23:0] memory_request_addr;
    logic memory_request_write;
    logic [63:0] memory_request_wdata;
    logic [7:0] memory_request_wstrb;
    logic memory_request_valid;
    logic memory_request_ready = 1'b0;
    logic [63:0] memory_response_rdata = 64'd0;
    logic memory_response_valid = 1'b0;
    integer accepted = 0;

    always #3.5 core_clk = !core_clk;
    always #8 memory_clk = !memory_clk;
    multitini_memory_cdc dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic submit(
        input logic [23:0] address,
        input logic write,
        input logic [63:0] data,
        input logic [7:0] strobes,
        input logic [63:0] expected_response
    );
        while (!core_request_ready) @(negedge core_clk);
        core_request_addr = address;
        core_request_write = write;
        core_request_wdata = data;
        core_request_wstrb = strobes;
        core_request_valid = 1'b1;
        @(negedge core_clk);
        core_request_valid = 1'b0;
        while (!core_response_valid) @(negedge core_clk);
        check_value(core_response_rdata == expected_response,
                    "coherent response crossing");
    endtask

    always @(posedge memory_clk) begin
        memory_response_valid <= 1'b0;
        if (memory_request_valid && memory_request_ready) begin
            if (accepted == 0) begin
                check_value(memory_request_addr == 24'h123450 &&
                            !memory_request_write &&
                            memory_request_wdata == 64'h0123456789abcdef &&
                            memory_request_wstrb == 8'h00,
                            "first coherent request");
                memory_response_rdata <= 64'hfeedfacecafebeef;
            end else begin
                check_value(memory_request_addr == 24'h800008 &&
                            memory_request_write &&
                            memory_request_wdata == 64'h8877665544332211 &&
                            memory_request_wstrb == 8'h03,
                            "second coherent request");
                memory_response_rdata <= 64'h8877665544332211;
            end
            accepted <= accepted + 1;
            memory_response_valid <= 1'b1;
        end
    end

    initial begin
        fork
            begin
                #20000;
                $display("FAIL: memory CDC timeout");
                $fatal(1);
            end
        join_none

        repeat (4) @(negedge core_clk);
        core_resetn = 1'b1;
        repeat (3) @(negedge memory_clk);
        memory_resetn = 1'b1;

        fork
            begin
                repeat (6) @(negedge memory_clk);
                memory_request_ready = 1'b1;
            end
            submit(24'h123450, 1'b0, 64'h0123456789abcdef, 8'h00,
                   64'hfeedfacecafebeef);
        join
        submit(24'h800008, 1'b1, 64'h8877665544332211, 8'h03,
               64'h8877665544332211);
        check_value(accepted == 2, "two one-outstanding transfers complete");

        $display("PASS: common memory CDC");
        $finish;
    end
endmodule
