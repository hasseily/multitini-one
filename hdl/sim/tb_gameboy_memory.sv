// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_gameboy_memory;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic loader_active = 1'b0;
    logic [23:0] loader_addr = 24'd0;
    logic loader_write = 1'b0;
    logic [63:0] loader_wdata = 64'd0;
    logic [7:0] loader_wstrb = 8'd0;
    logic loader_valid = 1'b0;
    logic loader_ready;
    logic [63:0] loader_rdata;
    logic loader_rvalid;
    logic save_read = 1'b0;
    logic [23:0] save_addr = 24'd0;
    logic save_pause = 1'b0;
    logic save_ready;
    logic [63:0] save_rdata;
    logic save_rvalid;
    logic core_read = 1'b0;
    logic core_write = 1'b0;
    logic [23:0] core_addr = 24'd0;
    logic [7:0] core_wdata = 8'd0;
    logic [7:0] core_rdata;
    logic core_stall;
    logic core_write_committed;
    logic [23:0] core_write_address;
    logic [23:0] memory_addr;
    logic memory_write;
    logic [63:0] memory_wdata;
    logic [7:0] memory_wstrb;
    logic memory_valid;
    logic memory_ready = 1'b1;
    logic [63:0] memory_rdata = 64'd0;
    logic memory_rvalid = 1'b0;
    logic [63:0] lines [0:3];
    logic response_pending = 1'b0;
    logic [1:0] response_delay = 2'd0;
    integer transaction_count = 0;
    integer pause_transaction_count = 0;

    always #5 clk = !clk;
    multitini_gameboy_memory dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    function automatic logic [63:0] merge_bytes(
        input logic [63:0] old_data,
        input logic [63:0] new_data,
        input logic [7:0] strobes
    );
        integer index;
        begin
            merge_bytes = old_data;
            for (index = 0; index < 8; index = index + 1)
                if (strobes[index])
                    merge_bytes[index * 8 +: 8] = new_data[index * 8 +: 8];
        end
    endfunction

    always @(posedge clk) begin
        memory_rvalid <= 1'b0;
        if (response_pending) begin
            if (response_delay == 0) begin
                memory_rvalid <= 1'b1;
                response_pending <= 1'b0;
            end else begin
                response_delay <= response_delay - 1'b1;
            end
        end
        if (memory_valid && memory_ready) begin
            check_value(!response_pending, "only one memory request outstanding");
            if (memory_write)
                lines[memory_addr[4:3]] <=
                    merge_bytes(lines[memory_addr[4:3]], memory_wdata,
                                memory_wstrb);
            memory_rdata <= memory_write ?
                merge_bytes(lines[memory_addr[4:3]], memory_wdata,
                            memory_wstrb) : lines[memory_addr[4:3]];
            response_pending <= 1'b1;
            response_delay <= 2'd2;
            transaction_count <= transaction_count + 1;
        end
    end

    initial begin
        lines[0] = 64'h0706050403020100;
        lines[1] = 64'h1716151413121110;
        lines[2] = 64'h2726252423222120;
        lines[3] = 64'h3736353433323130;
        repeat (3) @(negedge clk);
        reset = 1'b0;

        core_addr = 24'h00000b;
        core_read = 1'b1;
        #1;
        check_value(core_stall, "read miss stalls core");
        wait (!core_stall);
        check_value(core_rdata == 8'h13, "miss fills and selects byte");
        repeat (3) @(negedge clk);
        check_value(transaction_count == 1, "held read does not repeat");
        core_addr = 24'h00000f;
        #1;
        check_value(!core_stall && core_rdata == 8'h17,
                    "same-line read hits cache");
        core_read = 1'b0;

        @(negedge clk);
        core_addr = 24'h00000d;
        core_wdata = 8'haa;
        core_write = 1'b1;
        #1;
        check_value(core_stall, "cart RAM write stalls core");
        wait (!core_stall);
        check_value(lines[1] == 64'h1716aa1413121110,
                    "byte-strobed write reaches memory");
        core_write = 1'b0;
        @(negedge clk);
        @(negedge clk);

        // A transaction can already be queued when a snapshot requests its
        // pause. It must remain queued until the snapshot completion barrier
        // releases, or its write could land after the copied bytes and then
        // be lost when firmware acknowledges the snapshot.
        core_addr = 24'h000012;
        core_wdata = 8'h5a;
        core_write = 1'b1;
        @(negedge clk);
        save_pause = 1'b1;
        pause_transaction_count = transaction_count;
        repeat (3) @(negedge clk);
        check_value(transaction_count == pause_transaction_count,
                    "queued runtime write stays blocked by save barrier");
        save_pause = 1'b0;
        wait (!core_stall);
        check_value(lines[2] == 64'h27262524235a2120,
                    "queued runtime write resumes after save barrier");
        core_write = 1'b0;

        @(negedge clk);
        save_addr = 24'h000018;
        save_pause = 1'b1;
        save_read = 1'b1;
        while (!save_ready) @(negedge clk);
        @(negedge clk);
        save_read = 1'b0;
        wait (save_rvalid);
        check_value(save_rdata == 64'h3736353433323130,
                    "save snapshot response routed to save client");
        save_pause = 1'b0;

        @(negedge clk);
        loader_active = 1'b1;
        loader_addr = 24'h000008;
        loader_write = 1'b1;
        loader_wdata = 64'hffeeddccbbaa9988;
        loader_wstrb = 8'hff;
        loader_valid = 1'b1;
        while (!loader_ready) @(negedge clk);
        @(negedge clk);
        loader_valid = 1'b0;
        wait (loader_rvalid);
        check_value(loader_rdata == 64'hffeeddccbbaa9988,
                    "loader response routed to loader");
        loader_active = 1'b0;
        @(negedge clk);

        core_addr = 24'h000008;
        core_read = 1'b1;
        #1;
        wait (!core_stall);
        check_value(core_rdata == 8'h88,
                    "loader invalidates stale runtime cache");
        core_read = 1'b0;

        $display("PASS: Game Boy memory cache and arbiter");
        $finish;
    end
endmodule
