// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Production T65/6510 wrapper. The generated wrapper adds observation ports
// only. Controls advance on CPU clock attempts, including RDY-held reads;
// enable gaps are deliberately independent of this native stimulus timeline.
module tb_c64_6510_boundaries;
    logic clk = 0, reset = 1, enable = 0;
    logic rdy = 1, irq_n = 1, nmi_n = 1;
    wire [15:0] addr;
    wire [7:0] cpu_data, port_data, effective_data;
    wire we, nmi_ack, sync;
    logic [7:0] memory [0:65535];
    logic [2:0] controls [0:2047];
    wire [7:0] data_in = memory[addr];
    integer scenario, access_count, tick, clocks, holds, writes_while_held;
    logic tracing = 0, finished = 0;
    integer cases_file, count, parsed, cadence;
    string memory_file, controls_file;

    always #5 clk = !clk;
    cpu_6510 #(.PULLUP(8'h00), .FLOATING(8'h00), .FALLOFF_MASK(8'h00)) dut
       (.clk(clk), .enable(enable), .reset(reset), .port_tick(enable),
        .nmi_n(nmi_n), .nmi_ack(nmi_ack), .irq_n(irq_n), .rdy(rdy),
        .di(data_in), .\do (cpu_data), .addr(addr), .we(we),
        .diIO(8'hA0), .doIO(port_data), .test_data(effective_data), .test_sync(sync));

    always @(posedge clk) begin
        if (!reset && enable) begin
            if (!tracing && sync && !we && addr == 16'h0800) begin
                tracing = 1;
                tick = 0;
            end
            if (tracing && !finished) begin
                if (rdy || we) begin
                    $display("BOUNDARY_BUS,%0d,%0d,%0d,%04h,%s,%02h,%0d", scenario,
                             access_count, tick, addr, we ? "W" : "R",
                             we ? cpu_data : effective_data, sync);
                    access_count++;
                    if (we) begin
                        memory[addr] = cpu_data;
                        if (!rdy) writes_while_held++;
                        if (addr == 16'h0204 && cpu_data == 8'hA5) finished = 1;
                    end
                end else holds++;
                tick++;
            end
        end
    end

    task automatic run_case;
        logic [15:0] saved_addr;
        logic [7:0] saved_data;
        logic saved_we, stalled;
        @(negedge clk); reset = 1; enable = 0; rdy = 1; irq_n = 1; nmi_n = 1;
        tracing = 0; finished = 0; access_count = 0; tick = 0; clocks = 0;
        holds = 0; writes_while_held = 0;
        $readmemh(memory_file, memory);
        $readmemh(controls_file, controls);
        repeat (8) @(negedge clk);
        reset = 0;
        while (!finished && clocks < 16000 && tick < 2047) begin
            enable = ((clocks % cadence) == 0);
            if (cadence == 2 && (clocks % 19) < 3) enable = 0;
            if (tracing) begin
                rdy = controls[tick][2];
                irq_n = !controls[tick][1];
                nmi_n = !controls[tick][0];
            end
            // Inputs can prepare an unissued bus cycle combinationally.
            // Check stability after they settle and across the clock edge.
            #1;
            saved_addr = addr; saved_data = cpu_data; saved_we = we;
            stalled = !enable || (!rdy && !we);
            @(posedge clk); #1;
            if (tracing && stalled &&
                (addr !== saved_addr || cpu_data !== saved_data || we !== saved_we))
                $fatal(1, "FAIL: boundary case %0d held bus changed", scenario);
            clocks++;
            @(negedge clk);
        end
        enable = 0;
        $display("BOUNDARY_SUMMARY,%0d,%0d,%0d,%0d,%0d,%0d", scenario,
                 finished, access_count, tick, holds, writes_while_held);
    endtask

    initial begin
        cases_file = $fopen("cases.txt", "r");
        if (!cases_file) $fatal(1, "FAIL: boundary cases file missing");
        parsed = $fscanf(cases_file, "%d\n", count);
        if (parsed != 1) $fatal(1, "FAIL: boundary case count missing");
        for (integer index = 0; index < count; index++) begin
            parsed = $fscanf(cases_file, "%d %d %s %s\n", scenario, cadence,
                             memory_file, controls_file);
            if (parsed != 4) $fatal(1, "FAIL: boundary case row %0d", index);
            run_case();
        end
        $fclose(cases_file);
        $display("PASS: C64 boundary stimulus execution cases=%0d", count);
        $finish;
    end
    initial begin #200000000; $fatal(1, "FAIL: C64 boundary harness timeout"); end
endmodule
