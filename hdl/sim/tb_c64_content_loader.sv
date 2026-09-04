// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_content_loader;
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
    logic [15:0] core_addr = 16'h0000;
    logic prg_ram_wr;
    logic [15:0] prg_ram_addr;
    logic [7:0] prg_ram_data;
    logic prg_loaded;
    logic prg_run_request;
    logic [23:0] loader_memory_addr;
    logic loader_memory_write;
    logic [63:0] loader_memory_wdata;
    logic [7:0] loader_memory_wstrb;
    logic loader_memory_valid;
    logic loader_memory_ready = 1'b1;
    logic memory_fault = 1'b0;
    logic cart_loading;
    logic cart_loaded;
    logic [7:0] cart_id;
    logic cart_exrom;
    logic cart_game;
    logic cart_bank_hi;
    logic cart_bank_16k;
    logic [7:0] cart_bank_num;
    logic [7:0] cart_bank_addr;
    logic cart_bank_wr;
    logic core_reset_hold;
    logic prg_install_pause;
    logic active;
    logic error;
    logic [7:0] ram [0:65535];
    integer cart_write_count = 0;
    integer bank_write_count = 0;
    integer run_request_count = 0;
    localparam logic [8*16-1:0] CRT_MAGIC = "C64 CARTRIDGE   ";

    always #5 clk = !clk;
    multitini_c64_content_loader dut (.*);

    always @(posedge clk) begin
        if (prg_ram_wr) ram[prg_ram_addr] <= prg_ram_data;
        if (loader_memory_valid && loader_memory_ready)
            cart_write_count <= cart_write_count + 1;
        if (cart_bank_wr) bank_write_count <= bank_write_count + 1;
        if (prg_run_request) run_request_count <= run_request_count + 1;
    end

    task automatic check(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic send_byte(
        input logic [7:0] kind,
        input integer offset,
        input integer total,
        input logic [7:0] value
    );
        while (!load_ready) @(negedge clk);
        load_kind = kind;
        load_offset = offset;
        load_total = total;
        load_data = value;
        load_first = offset == 0;
        load_last = offset == total - 1;
        load_valid = 1'b1;
        @(negedge clk);
        load_valid = 1'b0;
        load_first = 1'b0;
        load_last = 1'b0;
    endtask

    function automatic logic [7:0] crt_byte(input integer offset);
        begin
            crt_byte = 8'd0;
            if (offset < 16)
                crt_byte = CRT_MAGIC[(15-offset)*8 +: 8];
            case (offset)
                19: crt_byte = 8'h40;
                20: crt_byte = 8'h01;
                23: crt_byte = MT_CRT_MAGIC_DESK;
                25: crt_byte = 8'h01;
                64: crt_byte = "C";
                65: crt_byte = "H";
                66: crt_byte = "I";
                67: crt_byte = "P";
                69: crt_byte = 8'h00;
                70: crt_byte = 8'h20;
                71: crt_byte = 8'h10;
                76: crt_byte = 8'h80;
                78: crt_byte = 8'h20;
                default:
                    if (offset >= 80) crt_byte = offset[7:0] ^ 8'ha5;
            endcase
        end
    endfunction

    initial begin : test
        integer index;
        localparam integer CRT_SIZE = 80 + 8192;

        repeat (2) @(negedge clk);
        reset = 1'b0;

        send_byte(MT_LOAD_C64_PRG_RUN, 0, 1, 8'h00);
        check(error && run_request_count == 0,
              "explicit RUN rejected before a PRG is loaded");
        clear_error = 1'b1;
        @(negedge clk);
        clear_error = 1'b0;

        send_byte(MT_LOAD_C64_PRG, 0, 5, 8'h01);
        repeat (260) @(negedge clk);
        check(active && !load_ready,
              "PRG payload waits for KERNAL readiness");
        core_addr = 16'hffcf;
        send_byte(MT_LOAD_C64_PRG, 1, 5, 8'h08);
        check(prg_install_pause,
              "ordinary PRG pauses the core while installing payload");
        send_byte(MT_LOAD_C64_PRG, 2, 5, 8'ha9);
        send_byte(MT_LOAD_C64_PRG, 3, 5, 8'h00);
        send_byte(MT_LOAD_C64_PRG, 4, 5, 8'h60);
        wait (!active);
        @(negedge clk);
        check(prg_loaded, "PRG committed");
        check(ram[16'h0801] == 8'ha9 && ram[16'h0803] == 8'h60,
              "PRG payload placed at header address");
        check(ram[16'h002d] == 8'h04 && ram[16'h002e] == 8'h08 &&
              ram[16'h00ae] == 8'h04 && ram[16'h00af] == 8'h08,
              "BASIC end pointers initialized");
        check(prg_run_request,
              "ordinary PRG emits its automatic RUN pulse");
        @(negedge clk);
        check(run_request_count == 1,
              "ordinary PRG retains automatic RUN request");

        core_addr = 16'h0000;
        send_byte(MT_LOAD_C64_PRG_STAGE, 0, 5, 8'h01);
        repeat (260) @(negedge clk);
        check(active && !load_ready,
              "staged PRG payload waits for KERNAL readiness");
        core_addr = 16'hffcf;
        send_byte(MT_LOAD_C64_PRG_STAGE, 1, 5, 8'h09);
        check(prg_install_pause,
              "staged PRG pauses the core while installing payload");
        send_byte(MT_LOAD_C64_PRG_STAGE, 2, 5, 8'ha2);
        send_byte(MT_LOAD_C64_PRG_STAGE, 3, 5, 8'h2a);
        send_byte(MT_LOAD_C64_PRG_STAGE, 4, 5, 8'h60);
        wait (!active);
        @(negedge clk);
        check(prg_loaded, "staged PRG committed");
        check(ram[16'h0901] == 8'ha2 && ram[16'h0903] == 8'h60,
              "staged PRG payload placed at header address");
        check(ram[16'h002d] == 8'h04 && ram[16'h002e] == 8'h09 &&
              ram[16'h00ae] == 8'h04 && ram[16'h00af] == 8'h09,
              "staged PRG repairs BASIC end pointers");
        check(run_request_count == 1,
              "staged PRG does not request RUN");

        send_byte(MT_LOAD_C64_PRG_RUN, 0, 1, 8'h01);
        check(error && run_request_count == 1,
              "explicit RUN requires a zero token");
        clear_error = 1'b1;
        @(negedge clk);
        clear_error = 1'b0;
        send_byte(MT_LOAD_C64_PRG_RUN, 0, 1, 8'h00);
        check(!active && !prg_install_pause && !core_reset_hold,
              "explicit RUN token does not reset or pause the core");
        @(negedge clk);
        check(!error && run_request_count == 2,
              "explicit RUN accepted after staged PRG");

        for (index = 0; index < CRT_SIZE; index = index + 1)
            send_byte(MT_LOAD_C64_CRT, index, CRT_SIZE, crt_byte(index));
        wait (!active);
        @(negedge clk);
        check(cart_loaded && cart_id == MT_CRT_MAGIC_DESK,
              "supported CRT committed");
        check(cart_write_count == 8192 && bank_write_count == 1,
              "one CHIP payload normalized to external memory");

        send_byte(MT_LOAD_C64_CRT_DETACH, 0, 1, 8'h00);
        check(!cart_loaded, "CRT detach is atomic");

        clear_error = 1'b1;
        @(negedge clk);
        clear_error = 1'b0;
        for (index = 0; index < 80; index = index + 1)
            send_byte(MT_LOAD_C64_CRT, index, 80,
                      index == 23 ? 8'd32 : crt_byte(index));
        check(error && !cart_loaded, "unsupported CRT rejected detached");

        $display("PASS: C64 PRG and CRT content loader");
        $finish;
    end
endmodule
