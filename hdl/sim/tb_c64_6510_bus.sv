// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Runs the imported NMOS T65 and production 6510 port wrapper. The runner
// supplies a synthetic program and compares this accepted bus trace with
// the pinned MAME microstep oracle; no private machine ROM is required.
module tb_c64_6510_bus;
    logic clk = 0, reset = 1, enable = 0, rdy = 1;
    wire [15:0] addr;
    wire [7:0] cpu_data, port_data, effective_data;
    wire we, nmi_ack;
    logic [7:0] memory [0:65535];
    wire [7:0] data_in = memory[addr];
    integer scenario = 0, trace_count = 0, hold_checks = 0, rdy_checks = 0;
    integer write_rdy_checks = 0, rmw_count = 0, port_writes = 0;
    logic tracing = 0, finished = 0;
    logic [7:0] expected_rmw [0:7];
    string memory_file = "memory.hex";

    always #5 clk = !clk;
    // This flat-memory CPU test uses no floating inputs; the independent
    // production-profile port differential covers the C64 pull/decay model.
    cpu_6510 #(.PULLUP(8'h00), .FLOATING(8'h00), .FALLOFF_MASK(8'h00)) dut
       (.clk(clk), .enable(enable), .reset(reset), .port_tick(enable),
        .nmi_n(1'b1), .nmi_ack(nmi_ack), .irq_n(1'b1), .rdy(rdy),
        .di(data_in), .\do (cpu_data), .addr(addr), .we(we),
        .diIO(8'hA0), .doIO(port_data), .test_data(effective_data));

    always @(posedge clk) begin
        if (!reset && enable && (rdy || we)) begin
            if (!we && addr == 16'h0800 && !tracing) tracing = 1;
            if (tracing && !finished) begin
                $display("BUS,%0d,%0d,%04h,%s,%02h", scenario, trace_count,
                         addr, we ? "W" : "R",
                         we ? cpu_data : effective_data);
                trace_count++;
                if (we) begin
                    memory[addr] = cpu_data;
                    if (!rdy) write_rdy_checks++;
                    if (addr == 16'h2000) begin
                        if (rmw_count > 7 || cpu_data !== expected_rmw[rmw_count])
                            $fatal(1, "FAIL: NMOS RMW write %0d value=%02h", rmw_count, cpu_data);
                        rmw_count++;
                    end
                    if (addr == 1) port_writes++;
                    if (addr == 16'h0204 && cpu_data == 8'hA5) finished = 1;
                end
            end
        end
    end

    task automatic check(input logic okay, input string message);
        if (!okay) $fatal(1, "FAIL: case %0d %s", scenario, message);
    endtask

    task automatic run_case(input integer selected);
        integer clocks;
        logic [15:0] frozen_addr;
        logic [7:0] frozen_data;
        logic frozen_we;
        logic stalled;
        scenario = selected;
        @(negedge clk); reset = 1; enable = 0; rdy = 1;
        tracing = 0; finished = 0; trace_count = 0; rmw_count = 0; port_writes = 0;
        $readmemh(memory_file, memory);
        repeat (8) @(negedge clk);
        check(port_data === 8'hFF, "reset restores 6510 port state");
        reset = 0;
        clocks = 0;
        while (!finished && clocks < 4000) begin
            // Classic four-clock service, then faster service with explicit
            // CE and RDY interruptions. Neither may lose or duplicate writes.
            enable = selected == 0 ? ((clocks % 4) == 0) : ((clocks % 2) == 0);
            if (selected == 1 && (clocks % 13) < 3) enable = 0;
            rdy = !(selected == 1 && (we || ((clocks % 17) < 4)));
            frozen_addr = addr; frozen_data = cpu_data; frozen_we = we;
            stalled = !enable || (!rdy && !we);
            @(posedge clk); #1;
            if (tracing && stalled) begin
                check(addr === frozen_addr && cpu_data === frozen_data && we === frozen_we,
                      "held CPU bus must remain stable");
                if (!enable) hold_checks++; else rdy_checks++;
            end
            clocks++;
            @(negedge clk);
        end
        enable = 0; rdy = 1;
        check(finished, "synthetic program finished through NMOS JMP wrap");
        check(memory[16'h0200] === 8'h2F, "6510 direction register read");
        check(memory[16'h020C] === 8'hFF && memory[16'h020D] === 8'hFF,
              "6510 reset direction and data read before writes");
        check(memory[16'h0201] === 8'h85, "6510 input/output mask read");
        check(memory[16'h0202] === 8'h03, "undocumented SLO result");
        check(memory[16'h0203][0] === 1'b1, "undocumented SLO carry");
        check(memory[16'h2000] === 8'h02 && rmw_count == 8, "NMOS RMW sequence");
        check(port_writes == 3 && port_data === 8'h06, "port RMW old/new writes");
        $display("CPU_SUMMARY case=%0d bus_cycles=%0d clocks=%0d rmw_writes=%0d", selected,
                 trace_count, clocks, rmw_count);
    endtask

    initial begin
        expected_rmw[0] = 8'h7F; expected_rmw[1] = 8'h7F;
        expected_rmw[2] = 8'h80; expected_rmw[3] = 8'h80;
        expected_rmw[4] = 8'h00; expected_rmw[5] = 8'h81;
        expected_rmw[6] = 8'h81; expected_rmw[7] = 8'h02;
        run_case(0);
        run_case(1);
        check(hold_checks > 10 && rdy_checks > 0 && write_rdy_checks > 0,
              "CE holds, RDY read holds and RDY write acceptance covered");
        $display("PASS: C64 real 6510 bus hold_checks=%0d rdy_checks=%0d write_rdy_checks=%0d",
                 hold_checks, rdy_checks, write_rdy_checks);
        $finish;
    end
    initial begin #200000; $fatal(1, "FAIL: 6510 bus timeout"); end
endmodule
