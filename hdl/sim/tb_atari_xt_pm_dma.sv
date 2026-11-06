// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_atari_xt_pm_dma;
    logic clk = 0;
    always #5 clk = !clk;

    logic rst = 1, we = 0;
    logic [7:0] dmactl = 0;
    logic [2:0] subcycle = 0;
    wire tick = &subcycle;
    wire [6:0] hcount;
    wire [8:0] line;
    wire dma_steal, mem_req, pm_we, pm_fetch;
    logic mem_valid = 0;

    always @(posedge clk) begin
        subcycle <= rst ? 3'd0 : subcycle + 3'd1;
        mem_valid <= !rst && mem_req;
    end

    antic2 dut (
        .clk(clk), .rst(rst), .tick(tick), .tune(16'd0), .px_tick(1'b0),
        .cs(1'b1), .we(we), .addr(4'd0), .wdata(dmactl), .cpu_writing(1'b0),
        .mem_data(8'h5a), .mem_valid(mem_valid), .mem_req(mem_req), .bus_byte(8'ha5),
        .dma_steal(dma_steal), .hcount(hcount), .line(line),
        .pm_we(pm_we), .pm_fetch(pm_fetch)
    );

    integer requests, fetches, phantom;
    integer expected_pm;
    bit visible, expected_steal;
    always @(posedge clk) begin
        if (rst) begin
            requests = 0;
            fetches = 0;
            phantom = 0;
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

    task automatic check_frame(input logic [7:0] value);
        @(negedge clk);
        rst = 1;
        dmactl = value;
        repeat (2) @(negedge clk);
        rst = 0;
        we = 1;
        @(negedge clk);
        we = 0;
        wait (line == 261 && hcount == 110);
    endtask

    initial begin
        check_frame(8'h18); // Player enable also fetches missiles.
        check_frame(8'h14); // Missile-only DMA coexists with phantom players.
        check_frame(8'h00);
        $display("PASS Atari-XT P/M DMA reads and steals on lines 8-247, with refresh and phantom captures preserved");
        $finish;
    end

    initial begin
        #10_000_000;
        $fatal(1, "P/M DMA test timed out");
    end
endmodule
