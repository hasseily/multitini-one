// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_local_ram;
    logic clk = 0;
    logic loader_we = 0;
    logic [15:0] loader_addr = 0;
    logic [7:0] loader_data = 0;
    logic machine_ce = 0, machine_we = 0, machine_store = 0;
    logic [15:0] machine_addr = 0;
    logic [7:0] machine_data = 0;
    wire [7:0] machine_q;
    integer checks = 0;
    logic [7:0] held;

    always #5 clk = !clk;
    multitini_c64_local_ram dut (.*);

    function automatic [7:0] pattern(input integer address);
        pattern = ((address * 37) ^ (address >> 8) ^ 8'hA7) & 255;
    endfunction

    task automatic check(input logic okay, input string message);
        checks++;
        if (!okay) $fatal(1, "FAIL: %s address=%04h q=%02h", message,
                          machine_addr, machine_q);
    endtask

    task automatic edge_clock;
        @(posedge clk); #1;
    endtask

    initial begin
        // Exercise the full address space so no lost upper address bit can
        // pass merely because a short test happened to touch one RAM page.
        for (integer address = 0; address < 65536; address++) begin
            @(negedge clk);
            loader_we = 1;
            loader_addr = address;
            loader_data = pattern(address);
            edge_clock();
        end
        @(negedge clk); loader_we = 0; machine_ce = 1;
        for (integer address = 0; address < 65536; address++) begin
            machine_addr = address;
            edge_clock();
            check(machine_q === pattern(address), "64 KiB loader/readback");
            @(negedge clk);
        end

        // An issued read has one-edge latency; an idle address change does
        // not alter the last completed response.
        machine_addr = 16'h1357;
        held = machine_q;
        #1; check(machine_q === held, "read cannot complete before its edge");
        edge_clock(); check(machine_q === pattern(16'h1357), "read response");
        @(negedge clk); machine_ce = 0; machine_addr = 16'h2468;
        repeat (3) begin
            edge_clock(); check(machine_q === pattern(16'h1357), "idle holds response");
        end

        // Mapper-denied writes still echo the bus byte, while the storage
        // contents remain intact. This covers ROM/cartridge overlays.
        @(negedge clk);
        machine_ce = 1; machine_we = 1; machine_store = 0;
        machine_addr = 16'h2468; machine_data = 8'h5C;
        edge_clock(); check(machine_q === 8'h5C, "nonstore write echoes data");
        @(negedge clk); machine_we = 0;
        edge_clock(); check(machine_q === pattern(16'h2468), "nonstore preserves RAM");

        // Back-to-back old-value and modified-value writes are distinct
        // transactions, as required by NMOS read-modify-write instructions.
        @(negedge clk); machine_we = 1; machine_store = 1; machine_data = 8'h7F;
        edge_clock(); check(machine_q === 8'h7F, "first RMW write echoes old byte");
        @(negedge clk); machine_data = 8'h80;
        edge_clock(); check(machine_q === 8'h80, "second RMW write echoes new byte");
        @(negedge clk); machine_we = 0;
        edge_clock(); check(machine_q === 8'h80, "RMW result committed");

        // Loader ownership wins a same-edge collision. The rejected machine
        // request neither updates the response nor writes its destination.
        @(negedge clk);
        loader_we = 1; loader_addr = 16'hABCD; loader_data = 8'h93;
        machine_we = 1; machine_store = 1;
        machine_addr = 16'h4321; machine_data = 8'hE7;
        edge_clock(); check(machine_q === 8'h80, "loader holds machine response");
        @(negedge clk); loader_we = 0; machine_we = 0;
        edge_clock(); check(machine_q === pattern(16'h4321), "loader rejects machine write");
        @(negedge clk); machine_addr = 16'hABCD;
        edge_clock(); check(machine_q === 8'h93, "loader collision write committed");

        // Same-address ownership and disabled stores are covered separately.
        @(negedge clk);
        loader_we = 1; loader_addr = 16'hABCD; loader_data = 8'h42;
        machine_we = 1; machine_data = 8'h11;
        edge_clock(); check(machine_q === 8'h93, "same-address loader holds response");
        @(negedge clk); loader_we = 0; machine_we = 0;
        edge_clock(); check(machine_q === 8'h42, "same-address loader wins");
        @(negedge clk); machine_ce = 0; machine_we = 1; machine_data = 8'h22;
        repeat (5) edge_clock();
        check(machine_q === 8'h42, "disabled request holds response");
        @(negedge clk); machine_ce = 1; machine_we = 0;
        edge_clock(); check(machine_q === 8'h42, "disabled request preserves storage");

        $display("PASS: C64 local RAM checks=%0d", checks);
        $finish;
    end
    initial begin #2000000; $fatal(1, "FAIL: local RAM timeout"); end
endmodule
