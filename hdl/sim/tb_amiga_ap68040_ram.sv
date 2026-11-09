// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Run a synthetic guest on the AP68040 and production memory controller.
// The DDR responder has a fixed delay. No chipset DMA or board timing is modeled.
module tb_amiga_ap68040_ram;
    localparam [31:0] PROBE = 32'h00210000;
    localparam [31:0] MOVE16_SOURCE = 32'h00211000;
    localparam [31:0] MOVE16_DESTINATION = 32'h00230000;
    localparam [31:0] SOURCE = 32'h00220000;
    localparam [31:0] DESTINATION = 32'h00240000;
    localparam [31:0] MARKER = 32'h00260000;
    localparam integer STREAM_BYTES = 65536;

    reg clk = 0, clk_fast = 1, aligned = 0;
    always #17.62 clk = !clk;
    always #8.81 clk_fast = !clk_fast;
    always @(negedge clk_fast) aligned <= !clk;
    reg reset = 1;
    wire [31:0] address;
    wire [15:0] wdata, wdata_low, rdata, rdata_low;
    wire [1:0] bus_state;
    wire uds_n, lds_n, wide, ready, busy, fault;
    wire [24:0] memory_addr;
    wire [63:0] memory_wdata;
    wire [7:0] memory_wstrb, monitor;
    wire memory_write, memory_burst, memory_valid;
    wire cpu_line, cpu_line_ready;
    wire [127:0] cpu_line_data;
    reg [63:0] response_data = 0;
    reg response_valid = 0, response_busy = 0;
    integer latency = 8;
    integer expect_burst = 0;
    integer cpu_2x = 0;
    integer movem_read = 0;
    integer fast16 = 0;
    wire high_ram = fast16 != 0 && address[31:24] == 8'h40;
    wire [28:1] routed_address = high_ram ? {5'b01000,address[23:1]} : address[28:1];
    wire [31:0] guest_base = fast16 != 0 ? 32'h40000000 : 0;
    wire [24:0] physical_base = fast16 != 0 ? 25'h1000000 : 0;

    multitini_amiga_ap68040 dut (
        .clk(clk), .cpu_clk(cpu_2x ? clk_fast : clk), .cpu_bclk(!cpu_2x || aligned), .reset_n(!reset),
        .ipl_n(3'b111), .fast_top(8'ha0), .fast16_enabled(fast16 != 0), .fast16_base(8'h40),
        .wide_selected(1'b1), .fast_ram_selected(expect_burst != 0 && (high_ram ||
                          (address >= 32'h00200000 && address < 32'h00a00000))), .ready(ready),
        .line_request(cpu_line), .line_ready(cpu_line_ready), .line_in(cpu_line_data),
        .data_in(rdata), .data_next(rdata_low), .address(address),
        .data_out(wdata), .data_low(wdata_low), .bus_state(bus_state),
        .read_cycle(), .uds_n(uds_n), .lds_n(lds_n), .wide_candidate(wide), .reset_out_n()
    );

    multitini_amiga_memory #(.USE_AP68040(1'b1)) memory_i (
        .clk(clk), .reset(reset), .chip_c1(1'b0), .chip_addr(23'd0),
        .chip_wdata(16'd0), .chip_wdata2(16'd0), .chip_oe_n(1'b1), .chip_we_n(1'b1),
        .chip_ub_n(1'b1), .chip_lb_n(1'b1), .chip_long(1'b0),
        .chip_rdata(), .chip_rdata2(), .chip_rdata48(),
        .cpu_req(bus_state[1]), .cpu_addr(routed_address), .cpu_wdata(wdata),
        .cpu_state(bus_state), .cpu_ub_n(uds_n), .cpu_lb_n(lds_n),
        .cpu_long(wide), .cpu_wdata2(wdata_low), .cpu_rdata(rdata), .cpu_rdata2(rdata_low),
        .cpu_ready(ready), .load_valid(1'b0), .load_addr(24'd0), .load_data(8'd0),
        .cpu_line(cpu_line), .cpu_line_stop(reset), .cpu_line_ready(cpu_line_ready), .cpu_line_data(cpu_line_data),
        .load_ready(), .load_done(), .run_enable(), .busy(busy), .fault(fault), .monitor(monitor),
        .memory_addr(memory_addr), .memory_write(memory_write), .memory_wdata(memory_wdata),
        .memory_wstrb(memory_wstrb), .memory_burst(memory_burst), .memory_valid(memory_valid),
        .memory_ready(!response_busy && !response_valid), .memory_rdata(response_data),
        .memory_rvalid(response_valid), .memory_fault(1'b0)
    );

    reg [7:0] rom [0:511];
    initial begin
        $readmemh("guest.hex", rom);
        if ($value$plusargs("latency=%d", latency) && latency < 0) $fatal(1, "Negative DDR latency");
        if ($value$plusargs("expect_burst=%d", expect_burst) && expect_burst > 1) $fatal(1, "Invalid burst expectation");
        if ($value$plusargs("cpu_2x=%d", cpu_2x) && cpu_2x > 1) $fatal(1, "Invalid CPU clock ratio");
        if ($value$plusargs("movem_read=%d", movem_read) && movem_read > 1) $fatal(1, "Invalid read mode");
        if ($value$plusargs("fast16=%d", fast16) && fast16 > 1) $fatal(1, "Invalid Fast RAM bank");
        repeat (10) @(negedge clk);
        reset = 0;
    end

    function automatic [31:0] pattern(input [31:0] byte_addr);
        return 32'hb5ca0000 ^ (byte_addr & 32'hfffffffc);
    endfunction

    function automatic [7:0] pattern_byte(input [31:0] byte_addr);
        return 8'(pattern(byte_addr) >> ((3 - int'(byte_addr[1:0])) * 8));
    endfunction

    function automatic [7:0] read_byte(input [24:0] byte_addr);
        // SP=$00200000, PC=$00200000. The first 512 Fast RAM bytes hold the test.
        case (byte_addr)
            25'h000001, 25'h000005: return 8'h20;
            default: begin
                if (byte_addr < 8) return 0;
                if (byte_addr[24:9] == 16'h1000) return rom[byte_addr[8:0]];
                return pattern_byte({7'd0, byte_addr});
            end
        endcase
    endfunction

    integer delay_left = 0, beat = 0, copied_bytes = 0, move16_bytes = 0;
    reg held_write, held_burst;
    reg [24:0] held_addr;
    always @(posedge clk) begin
        response_valid <= 0;
        if (memory_valid && !response_busy && !response_valid) begin
            response_busy <= 1;
            delay_left <= latency;
            held_addr <= memory_addr;
            held_write <= memory_write;
            held_burst <= memory_burst;
            beat <= 0;
            if (memory_write)
                for (int lane = 0; lane < 8; lane++) begin
                    if (memory_wstrb[lane] && memory_addr >= physical_base + MOVE16_DESTINATION &&
                        memory_addr < physical_base + MOVE16_DESTINATION + 16) begin
                        if (memory_addr + lane != physical_base + MOVE16_DESTINATION + move16_bytes ||
                            memory_wdata[lane*8+:8] !== pattern_byte(physical_base + MOVE16_SOURCE + move16_bytes))
                            $fatal(1, "Wrong MOVE16 write byte at %h lane %0d", memory_addr, lane);
                        move16_bytes++;
                    end else if (memory_wstrb[lane] && memory_addr >= physical_base + DESTINATION &&
                                 memory_addr < physical_base + DESTINATION + STREAM_BYTES) begin
                        if (memory_addr + lane != physical_base + DESTINATION + copied_bytes ||
                            memory_wdata[lane*8+:8] !== pattern_byte(physical_base + SOURCE + copied_bytes))
                            $fatal(1, "Wrong streaming copy byte at %h lane %0d", memory_addr, lane);
                        copied_bytes++;
                    end
                end
        end else if (response_busy) begin
            if (delay_left != 0) delay_left <= delay_left - 1;
            else begin
                response_valid <= 1;
                if (!held_write)
                    for (int lane = 0; lane < 8; lane++)
                        response_data[lane*8+:8] <= read_byte(25'(held_addr + beat*8 + lane));
                if (!held_burst || beat == 3) response_busy <= 0;
                else beat <= beat + 1;
            end
        end
    end

    integer cycles = 0, phase_cycle = 0, phase = 0;
    integer read_cycles = 0, copy_cycles = 0, phase_fills = 0, phase_writes = 0;
    integer move16_write_starts = 0;
    integer probe_beats [0:3], probe_starts [0:3], probe_first_cycle [0:3];
    wire [31:0] line_address = {dut.cpu.biu.cur.addr[31:4], 4'b0000};
    wire [31:0] logical_offset = line_address - guest_base;
    initial for (int line = 0; line < 4; line++) begin
        probe_beats[line] = 0;
        probe_starts[line] = 0;
    end

    // Read the BIU result before the next PCLK edge in either speed mode.
    always @(negedge clk) if (!reset) begin
        if (dut.cpu.b_rvalid && logical_offset >= SOURCE &&
            logical_offset < SOURCE + (movem_read ? 261120 : STREAM_BYTES) &&
            dut.cpu.b_rdata !== pattern(physical_base + logical_offset + 4*int'(dut.cpu.b_rbeat)))
            $fatal(1,"Wrong stream data for %h",line_address);
        if (dut.cpu.b_rvalid && logical_offset >= PROBE && logical_offset < PROBE + 64) begin
            int line, word_index;
            line = int'(line_address[5:4]);
            word_index = (line + probe_beats[line]) % 4;
            if (probe_beats[line] >= 4 || dut.cpu.b_rbeat !== 2'(word_index) ||
                dut.cpu.b_rdata !== pattern(physical_base + logical_offset + word_index*4))
                $fatal(1, "Wrong wrapped fill for line %0d beat %0d: index %0d data %h",
                       line, probe_beats[line], dut.cpu.b_rbeat, dut.cpu.b_rdata);
            if (probe_beats[line] == 0) probe_first_cycle[line] = cycles;
            if (probe_beats[line] == 3) begin
                if (expect_burst && cycles - probe_first_cycle[line] >= 15)
                    $fatal(1, "Burst line %0d did not reduce native cycles", line);
                if (fast16 != 0 && cycles - probe_first_cycle[line] != 3)
                    $fatal(1, "16 MiB line %0d did not replay on consecutive bus edges", line);
                $display("Probe start=%0d: TS=%0d, first-to-last cycles=%0d",
                         line, probe_starts[line], cycles - probe_first_cycle[line]);
            end
            probe_beats[line]++;
        end
    end

    always @(posedge clk) if (!reset) begin
        cycles++;
        if (fault || cycles > 2000000) $fatal(1, "Memory fault or guest timeout, phase %0d", phase);
        if (!dut.ts_n && dut.a >= guest_base + PROBE && dut.a < guest_base + PROBE + 64)
            probe_starts[int'(dut.a[5:4])]++;
        if (!dut.ts_n && !dut.rw_n && dut.a >= guest_base + MOVE16_DESTINATION &&
            dut.a < guest_base + MOVE16_DESTINATION + 16)
            move16_write_starts++;
        if (monitor[6]) phase_fills++;
        if (monitor[7]) phase_writes++;
        if (bus_state == 2'b11 && ready && address == MARKER) begin
            if ({wdata, wdata_low} !== 32'(phase + 1)) $fatal(1, "Unexpected guest phase marker");
            if (phase == 1) begin
                read_cycles = cycles - phase_cycle;
                $display("Stream read: %0d bytes, %0d cycles, %0d DDR fills",
                         movem_read ? 261120 : STREAM_BYTES, read_cycles, phase_fills);
                if (fast16 != 0 && movem_read != 0 && cpu_2x != 0 && latency == 8 && read_cycles > 296373)
                    $fatal(1,"Modeled Zorro III read is below 25 MB/s");
            end
            if (phase == 2) begin
                copy_cycles = cycles - phase_cycle;
                $display("Stream copy: %0d bytes, %0d cycles, %0d DDR fills, %0d DDR writes",
                         STREAM_BYTES, copy_cycles, phase_fills, phase_writes);
            end
            phase++;
            phase_cycle = cycles;
            phase_fills = 0;
            phase_writes = 0;
        end
        if (phase == 3 && !busy && !response_busy) begin
            for (int line = 0; line < 4; line++)
                if (probe_beats[line] != 4 || probe_starts[line] != (expect_burst ? 1 : 4))
                    $fatal(1, "Unexpected transfer count for line %0d: TS=%0d data=%0d",
                           line, probe_starts[line], probe_beats[line]);
            if (copied_bytes != STREAM_BYTES) $fatal(1, "Copied %0d bytes", copied_bytes);
            if (move16_write_starts != 4 || move16_bytes != 16)
                $fatal(1, "MOVE16 write was not split correctly: TS=%0d bytes=%0d", move16_write_starts, move16_bytes);
            $display("PASS AP68040 RAM, model DDR latency=%0d, AP burst=%0d, PCLK2x=%0d, Fast16=%0d",
                     latency, expect_burst, cpu_2x, fast16);
            $finish;
        end
    end
endmodule
