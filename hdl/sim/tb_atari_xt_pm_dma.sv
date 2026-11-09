// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_atari_xt_pm_dma;
    logic clk = 0;
    always #5 clk = !clk;

    logic rst = 1, we = 0;
    logic [3:0] reg_addr = 0;
    logic [7:0] reg_data = 0, dmactl = 0;
    logic mode4_case = 0;
    logic [2:0] subcycle = 0;
    wire tick = &subcycle;
    wire [6:0] hcount;
    wire [8:0] line;
    wire dma_steal, mem_req, pm_we, pm_fetch;
    wire [15:0] mem_addr;
    logic mem_valid = 0;
    logic [7:0] mem_data = 8'h5a;

    always @(posedge clk) begin
        subcycle <= rst ? 3'd0 : subcycle + 3'd1;
        mem_valid <= !rst && mem_req;
        if (mem_req) begin
            // Thirty eight-line mode-4 rows occupy all 240 visible lines.
            // The JVB immediately after them is beyond the visible range.
            if (mode4_case && mem_addr == 16'h2000) mem_data <= 8'h44;
            else if (mode4_case && mem_addr == 16'h2001) mem_data <= 8'h00;
            else if (mode4_case && mem_addr == 16'h2002) mem_data <= 8'h30;
            else if (mode4_case && mem_addr >= 16'h2003 && mem_addr < 16'h2020)
                mem_data <= 8'h04;
            else if (mode4_case && mem_addr == 16'h2020) mem_data <= 8'h41;
            else if (mode4_case && mem_addr == 16'h2021) mem_data <= 8'h00;
            else if (mode4_case && mem_addr == 16'h2022) mem_data <= 8'h20;
            else mem_data <= 8'h5a;
        end
    end

    antic2 dut (
        .clk(clk), .rst(rst), .tick(tick), .tune(16'd0), .px_tick(1'b0),
        .cs(1'b1), .we(we), .addr(reg_addr), .wdata(reg_data), .cpu_writing(1'b0),
        .mem_data(mem_data), .mem_valid(mem_valid), .mem_req(mem_req),
        .mem_addr(mem_addr), .bus_byte(8'ha5),
        .dma_steal(dma_steal), .hcount(hcount), .line(line),
        .pm_we(pm_we), .pm_fetch(pm_fetch)
    );

    integer requests, fetches, phantom, playfield_reads;
    integer expected_pm;
    bit visible, expected_steal, last_visible_seen;
    always @(posedge clk) begin
        if (rst) begin
            requests = 0;
            fetches = 0;
            phantom = 0;
            playfield_reads = 0;
            last_visible_seen = 0;
        end else if (mode4_case) begin
            if (dut.pf_mem_req) playfield_reads++;
            if (tick && hcount == 100) begin
                if (line == 247) begin
                    if (playfield_reads != 32)
                        $fatal(1, "Final visible mode-4 line had %0d glyph reads, expected 32", playfield_reads);
                    last_visible_seen = 1;
                end
                playfield_reads = 0;
            end
            if (last_visible_seen && (line >= 248 || line < 8)) begin
                if (dut.pf_mem_req)
                    $fatal(1, "Playfield DMA read during VBLANK at line=%0d cycle=%0d", line, hcount);
                expected_steal = hcount >= 25 && hcount <= 57 && hcount[1:0] == 2'd1;
                if (tick && dma_steal !== expected_steal)
                    $fatal(1, "VBLANK line=%0d cycle=%0d steal=%b expected=%b",
                           line, hcount, dma_steal, expected_steal);
            end
        end else begin
            // Playfield and display-list DMA are off. Refresh still owns its
            // nine slots per line after the first line following reset.
            visible = line >= 8 && line < 248;
            expected_steal = (visible && ((hcount == 0 && (|dmactl[3:2])) ||
                              (hcount >= 2 && hcount <= 5 && dmactl[3]))) ||
                             (line != 0 && hcount >= 25 && hcount <= 57 && hcount[1:0] == 2'd1);
            if (tick && dma_steal !== expected_steal)
                $fatal(1, "DMACTL=%h line=%0d cycle=%0d steal=%b expected=%b",
                       dmactl, line, hcount, dma_steal, expected_steal);

            if (dut.pm_mem_req) begin
                if (!mem_req) $fatal(1, "P/M DMA request did not reach the memory bus");
                requests++;
            end
            if (pm_fetch) fetches++;
            if (pm_we && !pm_fetch) phantom++;
            if (tick && hcount == 100) begin
                expected_pm = visible ? (dmactl[3] ? 5 : (dmactl[2] ? 1 : 0)) : 0;
                if (requests != expected_pm || fetches != expected_pm)
                    $fatal(1, "DMACTL=%h line=%0d requests=%0d fetches=%0d expected=%0d",
                           dmactl, line, requests, fetches, expected_pm);
                if (phantom != (dmactl[3] ? 0 : 4))
                    $fatal(1, "DMACTL=%h line=%0d phantom captures=%0d", dmactl, line, phantom);
                requests = 0;
                fetches = 0;
                phantom = 0;
            end
        end
    end

    task automatic write_antic(input logic [3:0] address, input logic [7:0] value);
        reg_addr = address;
        reg_data = value;
        we = 1;
        @(negedge clk);
        we = 0;
    endtask

    task automatic check_frame(input logic [7:0] value);
        @(negedge clk);
        rst = 1;
        dmactl = value;
        repeat (2) @(negedge clk);
        rst = 0;
        write_antic(4'd0, value);
        wait (line == 261 && hcount == 110);
    endtask

    initial begin
        check_frame(8'h18); // Player enable also fetches missiles.
        check_frame(8'h14); // Missile-only DMA coexists with phantom players.
        check_frame(8'h00);

        @(negedge clk);
        mode4_case = 1;
        rst = 1;
        repeat (2) @(negedge clk);
        rst = 0;
        write_antic(4'd2, 8'h00);
        write_antic(4'd3, 8'h20);
        write_antic(4'd0, 8'h21); // Display list and narrow playfield, no P/M.
        wait (last_visible_seen);
        wait (line == 8 && hcount == 100); // Check both ends of the frame.

        $display("PASS Atari-XT P/M and playfield DMA on visible lines, with VBLANK refresh and phantom captures preserved");
        $finish;
    end

    initial begin
        #15_000_000;
        $fatal(1, "ANTIC DMA test timed out");
    end
endmodule
