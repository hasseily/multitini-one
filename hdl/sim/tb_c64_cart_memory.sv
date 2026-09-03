// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_cart_memory;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic loader_active = 1'b0;
    logic [23:0] loader_addr = 24'd0;
    logic loader_write = 1'b0;
    logic [63:0] loader_wdata = 64'd0;
    logic [7:0] loader_wstrb = 8'd0;
    logic loader_valid = 1'b0;
    logic loader_ready;
    logic core_request = 1'b0;
    logic core_write = 1'b0;
    logic [23:0] core_addr = 24'd0;
    logic [7:0] core_wdata = 8'd0;
    logic [7:0] core_rdata;
    logic core_stall;
    logic busy;
    logic loader_busy;
    logic fault;
    logic clear_fault = 1'b0;
    logic [23:0] memory_addr;
    logic memory_write;
    logic [63:0] memory_wdata;
    logic [7:0] memory_wstrb;
    logic memory_valid;
    logic memory_ready = 1'b0;
    logic [63:0] memory_rdata = 64'h7766554433221100;
    logic memory_rvalid = 1'b0;
    logic memory_fault = 1'b0;

    always #5 clk = !clk;
    multitini_c64_cart_memory dut (.*);

    task automatic check(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    initial begin
        repeat (2) @(negedge clk);
        reset = 1'b0;

        loader_active = 1'b1;
        loader_addr = 24'h200008;
        loader_write = 1'b1;
        loader_wdata = 64'h000000000000aa00;
        loader_wstrb = 8'h02;
        loader_valid = 1'b1;
        @(posedge clk); #1;
        check(!loader_ready && memory_valid && memory_write,
              "loader write waits for backend acceptance");
        check(memory_addr == loader_addr && memory_wstrb == 8'h02,
              "loader payload forwarded");
        memory_ready = 1'b1;
        @(negedge clk);
        loader_valid = 1'b0;
        memory_rvalid = 1'b1;
        @(negedge clk);
        memory_rvalid = 1'b0;
        loader_active = 1'b0;
        check(!loader_busy, "loader completion released");

        core_addr = 24'h200003;
        core_request = 1'b1;
        memory_ready = 1'b0;
        @(posedge clk); #1;
        check(core_stall, "cache miss stalls core");
        @(posedge clk); #1;
        check(memory_valid && !memory_write &&
              memory_addr == 24'h200000, "aligned read issued");
        memory_ready = 1'b1;
        @(negedge clk);
        memory_rvalid = 1'b1;
        @(negedge clk);
        memory_rvalid = 1'b0;
        #1;
        check(!core_stall && core_rdata == 8'h33,
              "response fills byte-select cache");

        @(negedge clk);
        core_request = 1'b0;
        @(negedge clk);
        core_addr = 24'h20000e;
        core_wdata = 8'ha5;
        core_write = 1'b1;
        core_request = 1'b1;
        memory_ready = 1'b0;
        @(posedge clk); #1;
        check(core_stall, "external cartridge write stalls core");
        @(posedge clk); #1;
        check(memory_valid && memory_write &&
              memory_addr == 24'h200008 && memory_wstrb == 8'h40 &&
              memory_wdata[55:48] == 8'ha5,
              "external cartridge byte write is aligned and strobed");
        memory_ready = 1'b1;
        @(negedge clk);
        memory_rvalid = 1'b1;
        @(negedge clk);
        memory_rvalid = 1'b0;
        #1;
        check(!core_stall, "write response releases held request");
        core_request = 1'b0;
        core_write = 1'b0;

        memory_fault = 1'b1;
        @(negedge clk);
        memory_fault = 1'b0;
        check(fault, "memory fault is sticky");
        clear_fault = 1'b1;
        @(negedge clk);
        clear_fault = 1'b0;
        check(!fault, "fault clears explicitly");
        $display("PASS: C64 cartridge memory bridge");
        $finish;
    end
endmodule
