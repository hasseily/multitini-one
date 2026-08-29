// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_gameboy_loader;
    import multitini_gameboy_defs_pkg::*;

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
    logic ioctl_download;
    logic cart_download;
    logic cgb_boot_download;
    logic ioctl_wr;
    logic [24:0] ioctl_addr;
    logic [15:0] ioctl_dout;
    logic [23:0] memory_addr;
    logic memory_write;
    logic [63:0] memory_wdata;
    logic [7:0] memory_wstrb;
    logic memory_valid;
    logic memory_ready = 1'b1;
    logic [63:0] memory_rdata = 64'd0;
    logic memory_rvalid = 1'b0;
    logic cart_loaded;
    logic boot_override_loaded;
    logic [31:0] cart_size;
    logic active;
    logic error;
    logic response_pending = 1'b0;
    integer memory_writes = 0;
    logic [63:0] first_line;
    logic [63:0] last_line;
    logic [15:0] header_word;

    always #5 clk = !clk;
    multitini_gameboy_loader dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    always @(posedge clk) begin
        memory_rvalid <= 1'b0;
        if (response_pending) begin
            memory_rvalid <= 1'b1;
            response_pending <= 1'b0;
        end
        if (memory_valid && memory_ready) begin
            check_value(memory_write, "loader issues writes only");
            check_value(memory_addr[2:0] == 3'd0, "line address aligned");
            check_value(memory_wstrb == 8'hff, "full cart line strobes");
            if (memory_writes == 0) first_line <= memory_wdata;
            if (memory_addr == MT_SIZE_GB_CART_MIN - 8) last_line <= memory_wdata;
            memory_writes <= memory_writes + 1;
            response_pending <= 1'b1;
        end
        if (ioctl_wr && ioctl_addr == 25'h000142)
            header_word <= ioctl_dout;
    end

    task automatic send_beat(
        input logic [7:0] kind,
        input integer offset,
        input integer total
    );
        while (!load_ready) @(negedge clk);
        load_kind = kind;
        load_offset = offset;
        load_total = total;
        load_data = offset[7:0] ^ 8'ha5;
        load_first = offset == 0;
        load_last = offset == total - 1;
        load_valid = 1'b1;
        @(negedge clk);
        load_valid = 1'b0;
        load_first = 1'b0;
        load_last = 1'b0;
    endtask

    initial begin
        integer index;
        repeat (3) @(negedge clk);
        reset = 1'b0;

        send_beat(MT_LOAD_GB_CART, 0, MT_SIZE_GB_CART_MIN - 1);
        check_value(error && !active, "undersized cartridge rejected");
        clear_error = 1'b1;
        @(negedge clk);
        clear_error = 1'b0;

        for (index = 0; index < MT_SIZE_GBC_BOOT; index = index + 1)
            send_beat(MT_LOAD_GBC_BOOT, index, MT_SIZE_GBC_BOOT);
        wait (!active);
        check_value(boot_override_loaded && !error,
                    "exact bootstrap override accepted");
        check_value(memory_writes == 0, "bootstrap does not use cart memory");

        for (index = 0; index < MT_SIZE_GB_CART_MIN; index = index + 1)
            send_beat(MT_LOAD_GB_CART, index, MT_SIZE_GB_CART_MIN);
        wait (cart_loaded);
        check_value(!active && !error, "minimum cartridge accepted");
        check_value(cart_size == MT_SIZE_GB_CART_MIN,
                    "cartridge size retained");
        check_value(memory_writes == MT_SIZE_GB_CART_MIN / 8,
                    "one write per packed line");
        check_value(first_line == 64'ha2a3a0a1a6a7a4a5,
                    "first line byte packing");
        check_value(last_line == 64'h5a5b58595e5f5c5d,
                    "last line byte packing");
        check_value(header_word == 16'he6e7,
                    "upstream metadata word ordering");

        $display("PASS: Game Boy content loader");
        $finish;
    end
endmodule
