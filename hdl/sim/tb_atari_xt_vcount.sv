// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_atari_xt_vcount;
    logic clk = 0;
    always #5 clk = !clk;

    logic rst = 1;
    logic [1:0] subcycle = 0;
    wire tick = &subcycle;
    wire [7:0] readback [0:1];
    wire [6:0] hcount [0:1];
    wire [8:0] line [0:1];

    always @(posedge clk) subcycle <= rst ? 2'd0 : subcycle + 2'd1;

    for (genvar mode = 0; mode < 2; mode++) begin : g_mode
        antic2 #(.VCOUNT_COMPAT(mode == 1)) dut (
            .clk(clk), .rst(rst), .tick(tick), .tune(16'd0), .px_tick(1'b0),
            .cs(1'b1), .we(1'b0), .addr(4'hb), .wdata(8'd0), .cpu_writing(1'b0),
            .mem_data(8'd0), .mem_valid(1'b0), .bus_byte(8'd0),
            .rdata(readback[mode]), .hcount(hcount[mode]), .line(line[mode])
        );
    end

    initial begin
        repeat (2) @(negedge clk);
        rst = 0;
        #1_500_000;
        $fatal(1, "ANTIC VCOUNT test timed out");
    end

    always @(posedge clk) if (!rst && tick) begin
        if (hcount[0] !== hcount[1] || line[0] !== line[1])
            $fatal(1, "VCOUNT compatibility changed the raster timing");

        // The beam reports the next line from cycle 111. On the last line,
        // native VCOUNT reaches $83 at 111 and both modes read zero at 112.
        if (line[0] == 0 && hcount[0] == 111) begin
            if (readback[0] !== 8'h83 || readback[1] !== 8'h82)
                $fatal(1, "VCOUNT rollover reads were %h and %h, expected 83 and 82",
                       readback[0], readback[1]);
        end else if (readback[0] !== line[0][8:1] || readback[1] !== line[0][8:1]) begin
            $fatal(1, "VCOUNT reads were %h and %h at line=%0d cycle=%0d, expected %h",
                   readback[0], readback[1], line[0], hcount[0], line[0][8:1]);
        end

        if (line[0] == 0 && hcount[0] == 113) begin
            $display("PASS Atari-XT VCOUNT exact and compatible rollover reads across a full frame");
            $finish;
        end
    end
endmodule
