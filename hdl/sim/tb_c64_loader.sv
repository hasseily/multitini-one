// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_loader;
    import multitini_c64_defs_pkg::*;

    logic clk = 1'b0;
    logic reset = 1'b1;
    logic clear_error = 1'b0;
    logic [7:0] load_data = 8'd0;
    logic load_valid = 1'b0;
    logic load_ready;
    logic load_first = 1'b0;
    logic load_last = 1'b0;
    logic [7:0] load_kind = 8'd0;
    logic [31:0] load_offset = 32'd0;
    logic [31:0] load_total = 32'd0;
    logic rom_wr;
    logic rom_bank;
    logic [13:0] rom_addr;
    logic [7:0] rom_data;
    logic [2:0] rom_loaded;
    logic roms_ready;
    logic active;
    logic error;

    always #5 clk = !clk;

    multitini_c64_loader dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic send_beat(
        input logic [7:0] kind,
        input integer offset,
        input integer total,
        input logic first,
        input logic last,
        input logic expect_write,
        input logic expected_bank,
        input logic [13:0] expected_addr
    );
        @(negedge clk);
        load_kind = kind;
        load_offset = offset;
        load_total = total;
        load_data = offset[7:0] ^ 8'hA5;
        load_first = first;
        load_last = last;
        load_valid = 1'b1;
        @(posedge clk);
        #1;
        check_value(rom_wr == expect_write, "ROM write qualification");
        if (expect_write) begin
            check_value(rom_bank == expected_bank, "ROM bank mapping");
            check_value(rom_addr == expected_addr, "ROM address mapping");
            check_value(rom_data == (offset[7:0] ^ 8'hA5), "ROM data mapping");
        end
        @(negedge clk);
        load_valid = 1'b0;
        load_first = 1'b0;
        load_last = 1'b0;
    endtask

    task automatic load_rom(
        input logic [7:0] kind,
        input integer total,
        input logic bank,
        input integer base
    );
        integer index;
        for (index = 0; index < total; index = index + 1) begin
            send_beat(kind, index, total, index == 0, index == total - 1,
                      1'b1, bank, (base + index) & 14'h3FFF);
        end
    endtask

    initial begin
        repeat (3) @(negedge clk);
        reset = 1'b0;
        @(negedge clk);
        check_value(load_ready, "loader ready after reset");
        check_value(!roms_ready && rom_loaded == 3'b000, "ROMs start absent");

        send_beat(8'hFF, 0, 1, 1'b1, 1'b1, 1'b0, 1'b0, 14'd0);
        check_value(error && !active, "unsupported object rejected");
        clear_error = 1'b1;
        @(posedge clk);
        #1;
        clear_error = 1'b0;
        check_value(!error, "error clear");

        send_beat(MT_LOAD_C64_BASIC, 0, 8192, 1'b1, 1'b0,
                  1'b1, 1'b0, 14'h0000);
        send_beat(MT_LOAD_C64_BASIC, 2, 8192, 1'b0, 1'b0,
                  1'b0, 1'b0, 14'd0);
        send_beat(MT_LOAD_C64_BASIC, 8191, 8192, 1'b0, 1'b1,
                  1'b0, 1'b0, 14'd0);
        check_value(error && rom_loaded == 3'b000,
                    "offset discontinuity rejects object");

        clear_error = 1'b1;
        @(posedge clk);
        #1;
        clear_error = 1'b0;
        load_rom(MT_LOAD_C64_BASIC, 8192, 1'b0, 0);
        check_value(rom_loaded == 3'b001, "BASIC accepted");
        load_rom(MT_LOAD_C64_KERNAL, 8192, 1'b0, 16'h2000);
        check_value(rom_loaded == 3'b011, "KERNAL accepted");
        load_rom(MT_LOAD_C64_CHARACTER, 4096, 1'b1, 0);
        check_value(rom_loaded == 3'b111 && roms_ready,
                    "all required ROMs accepted");

        $display("PASS: C64 ROM loader");
        $finish;
    end
endmodule
