// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// EasyFlash flash front end against a byte-exact model of the canonical 1 MiB
// cartridge image, its byte port and its sector-erase engine.
module tb_c64_ef_flash;
    logic clk = 1'b0;
    always #5 clk = !clk;
    logic reset = 1'b1, enable = 1'b1, ultimax = 1'b1;
    logic access_ce = 1'b0, access_we = 1'b0, rom_l = 1'b0, rom_h = 1'b0;
    logic [12:0] address = 13'd0;
    logic [7:0] write_data = 8'd0;
    logic [5:0] bank = 6'd0;
    wire read_override;
    wire [7:0] read_data;
    wire mem_valid, mem_write;
    wire [23:0] mem_addr;
    wire [7:0] mem_wdata;
    wire mem_accept;
    logic mem_done = 1'b0;
    logic [7:0] mem_rdata = 8'd0;
    wire erase_valid;
    wire [2:0] erase_group;
    wire erase_high;
    wire erase_accept;
    logic erase_done = 1'b0;
    wire [30:0] journal_data;
    wire journal_valid;
    logic journal_ready = 1'b1;
    wire busy;

    multitini_c64_ef_flash #(.JOURNAL_DEPTH(4)) dut (.*);

    logic [7:0] image[0:1048575];
    logic mem_busy = 1'b0, erase_busy = 1'b0, pending_write = 1'b0, pending_high = 1'b0;
    logic [23:0] pending_addr = 24'd0;
    logic [7:0] pending_data = 8'd0;
    logic [2:0] pending_group = 3'd0;
    integer mem_delay = 0, mem_latency = 3, erase_delay = 0, erase_count = 0;
    logic [30:0] journal_log[0:63];
    integer journal_count = 0, checks = 0;

    assign mem_accept = mem_valid && !mem_busy;
    assign erase_accept = erase_valid && !erase_busy;
    always @(posedge clk) begin
        mem_done <= 1'b0;
        erase_done <= 1'b0;
        if (mem_accept) begin
            mem_busy <= 1'b1;
            mem_delay <= mem_latency;
            pending_addr <= mem_addr;
            pending_write <= mem_write;
            pending_data <= mem_wdata;
        end else if (mem_busy) begin
            if (mem_delay == 0) begin
                mem_busy <= 1'b0;
                mem_done <= 1'b1;
                if (pending_write) image[pending_addr - 24'h200000] <= pending_data;
                mem_rdata <= image[pending_addr - 24'h200000];
            end else mem_delay <= mem_delay - 1;
        end
        if (erase_accept) begin
            erase_busy <= 1'b1;
            erase_delay <= 20;
            pending_group <= erase_group;
            pending_high <= erase_high;
            erase_count <= erase_count + 1;
        end else if (erase_busy) begin
            if (erase_delay == 0) begin
                for (integer b = 0; b < 8; b++)
                    for (integer o = 0; o < 8192; o++)
                        image[(pending_group * 8 + b) * 16384 + (pending_high ? 8192 : 0) + o] = 8'hff;
                erase_busy <= 1'b0;
                erase_done <= 1'b1;
            end else erase_delay <= erase_delay - 1;
        end
        if (journal_valid && journal_ready) begin
            journal_log[journal_count] = journal_data;
            journal_count = journal_count + 1;
        end
    end

    task automatic check(input logic condition, input string message);
        checks++;
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask
    function automatic integer offset(input bit high, input [5:0] b, input [12:0] a);
        offset = b * 16384 + (high ? 8192 : 0) + a;
    endfunction
    task automatic cpu_write(input bit high, input [5:0] b, input [12:0] a, input [7:0] d);
        @(negedge clk);
        bank = b; address = a; write_data = d; rom_l = !high; rom_h = high;
        access_we = 1'b1; access_ce = 1'b1;
        @(negedge clk);
        access_ce = 1'b0; access_we = 1'b0; rom_l = 1'b0; rom_h = 1'b0;
    endtask
    task automatic cpu_read(input bit high, input [5:0] b, input [12:0] a,
                            output bit overridden, output [7:0] value);
        @(negedge clk);
        bank = b; address = a; rom_l = !high; rom_h = high; access_we = 1'b0; access_ce = 1'b1;
        #1;
        overridden = read_override; value = read_data;
        @(negedge clk);
        access_ce = 1'b0; rom_l = 1'b0; rom_h = 1'b0;
    endtask
    task automatic command(input bit high, input [7:0] value);
        cpu_write(high, 6'd0, 13'h0555, 8'haa);
        cpu_write(high, 6'd0, 13'h02aa, 8'h55);
        cpu_write(high, 6'd0, 13'h0555, value);
    endtask
    task automatic wait_engine;
        integer waited;
        waited = 0;
        while (dut.engine != 3'd0 && waited < 2000) begin @(negedge clk); waited++; end
        check(dut.engine == 3'd0, "operation reaches memory");
    endtask
    task automatic wait_idle;
        integer waited;
        waited = 0;
        while (busy && waited < 2000) begin @(negedge clk); waited++; end
        check(!busy, "operation completes");
    endtask
    function automatic [30:0] entry(input [1:0] kind, input bit high, input [5:0] b,
                                    input [12:0] a, input [7:0] d);
        entry = {kind, high, b, a, 1'b0, d};
    endfunction

    bit hit, hit2;
    logic [7:0] value, value2;
    initial begin
        for (integer i = 0; i < 1048576; i++) image[i] = 8'hff;
        repeat (2) @(negedge clk);
        reset = 1'b0;

        // Autoselect identifies an AMD AM29F040 on each chip independently.
        command(1'b0, 8'h90);
        cpu_read(1'b0, 6'd5, 13'h0000, hit, value);
        check(hit && value == 8'h01, "ROML autoselect manufacturer");
        cpu_read(1'b0, 6'd5, 13'h0001, hit, value);
        check(hit && value == 8'ha4, "ROML autoselect device");
        cpu_read(1'b0, 6'd5, 13'h0002, hit, value);
        check(hit && value == 8'h00, "ROML sector unprotected");
        cpu_read(1'b1, 6'd5, 13'h0000, hit, value);
        check(!hit, "ROMH stays in read mode");
        cpu_write(1'b0, 6'd0, 13'h0000, 8'hf0);
        cpu_read(1'b0, 6'd5, 13'h0000, hit, value);
        check(!hit, "reset returns ROML to read mode");

        // Program ANDs into the stored byte; data polling while busy.
        mem_latency = 40;
        command(1'b0, 8'ha0);
        cpu_write(1'b0, 6'd3, 13'h0123, 8'h5a);
        cpu_read(1'b0, 6'd3, 13'h0123, hit, value);
        cpu_read(1'b0, 6'd3, 13'h0123, hit2, value2);
        check(hit && hit2 && value[7] && value2[7] && value[6] != value2[6] && !value[3],
              "program status: DQ7 complement and DQ6 toggle");
        cpu_read(1'b1, 6'd3, 13'h0123, hit, value);
        check(!hit, "the other chip reads memory while one is busy");
        command(1'b0, 8'h90);
        wait_idle();
        cpu_read(1'b0, 6'd3, 13'h0123, hit, value);
        check(!hit && image[offset(0, 3, 13'h0123)] == 8'h5a, "programmed byte stored; busy writes ignored");
        mem_latency = 3;
        command(1'b0, 8'ha0);
        cpu_write(1'b0, 6'd3, 13'h0123, 8'hf0);
        wait_idle();
        check(image[offset(0, 3, 13'h0123)] == 8'h50, "program clears bits only");
        command(1'b1, 8'ha0);
        cpu_write(1'b1, 6'd63, 13'h1fff, 8'h00);
        wait_idle();
        check(image[offset(1, 63, 13'h1fff)] == 8'h00, "ROMH top byte programmed");

        // Commands need Ultimax mode, where the C64 asserts ROML/ROMH on writes.
        ultimax = 1'b0;
        command(1'b0, 8'ha0);
        cpu_write(1'b0, 6'd1, 13'h0000, 8'h00);
        @(negedge clk);
        check(!busy && image[offset(0, 1, 13'h0000)] == 8'hff, "non-Ultimax writes ignored");
        ultimax = 1'b1;

        // Sector erase clears 8 banks of one chip only.
        image[offset(1, 9, 13'h0005)] = 8'h12;
        image[offset(0, 8, 13'h0005)] = 8'h34;
        command(1'b1, 8'h80);
        cpu_write(1'b1, 6'd0, 13'h0555, 8'haa);
        cpu_write(1'b1, 6'd0, 13'h02aa, 8'h55);
        cpu_write(1'b1, 6'd9, 13'h0000, 8'h30);
        cpu_read(1'b1, 6'd9, 13'h0005, hit, value);
        check(hit && !value[7] && value[3], "erase status: DQ7 low, DQ3 set");
        wait_idle();
        check(image[offset(1, 9, 13'h0005)] == 8'hff && image[offset(0, 8, 13'h0005)] == 8'h34 &&
              image[offset(1, 63, 13'h1fff)] == 8'h00, "sector erase limited to its sector and chip");

        // Chip erase walks all eight sectors of one chip.
        image[offset(0, 0, 13'h0000)] = 8'h00;
        image[offset(0, 63, 13'h0064)] = 8'h11;
        command(1'b0, 8'h80);
        cpu_write(1'b0, 6'd0, 13'h0555, 8'haa);
        cpu_write(1'b0, 6'd0, 13'h02aa, 8'h55);
        cpu_write(1'b0, 6'd0, 13'h0555, 8'h10);
        wait_idle();
        check(erase_count == 9 && image[offset(0, 0, 13'h0000)] == 8'hff &&
              image[offset(0, 63, 13'h0064)] == 8'hff && image[offset(1, 63, 13'h1fff)] == 8'h00,
              "chip erase covers one whole chip");

        // The journal holds operations back when firmware has not drained it.
        journal_ready = 1'b0;
        for (integer i = 0; i < 5; i++) begin
            command(1'b0, 8'ha0);
            cpu_write(1'b0, 6'd2, i[12:0], 8'h0f);
            if (i < 4) wait_engine();
        end
        repeat (20) @(negedge clk);
        check(busy && image[offset(0, 2, 13'h0003)] == 8'h0f && image[offset(0, 2, 13'h0004)] == 8'hff,
              "full journal holds the next operation before memory changes");
        journal_ready = 1'b1;
        wait_idle();
        check(image[offset(0, 2, 13'h0004)] == 8'h0f, "drained journal releases the operation");

        check(journal_count == 10, "every accepted operation is journaled once");
        check(journal_log[0] == entry(2'd1, 1'b0, 6'd3, 13'h0123, 8'h5a) &&
              journal_log[1] == entry(2'd1, 1'b0, 6'd3, 13'h0123, 8'hf0) &&
              journal_log[2] == entry(2'd1, 1'b1, 6'd63, 13'h1fff, 8'h00) &&
              journal_log[3] == entry(2'd2, 1'b1, 6'd9, 13'h0000, 8'hff) &&
              journal_log[4] == entry(2'd3, 1'b0, 6'd0, 13'h0555, 8'hff) &&
              journal_log[9] == entry(2'd1, 1'b0, 6'd2, 13'h0004, 8'h0f),
              "journal entries describe each operation");

        // Detach clears command state and any queued entries.
        command(1'b0, 8'h90);
        enable = 1'b0;
        @(negedge clk);
        enable = 1'b1;
        cpu_read(1'b0, 6'd0, 13'h0000, hit, value);
        check(!hit && !journal_valid, "detach returns both chips to read mode");
        $display("PASS: C64 EasyFlash flash front end checks=%0d journal=%0d", checks, journal_count);
        $finish;
    end
    initial begin #20000000; $fatal(1, "EasyFlash flash timeout"); end
endmodule
