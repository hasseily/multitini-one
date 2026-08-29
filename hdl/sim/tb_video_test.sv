// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_video_test;
    localparam int H_ACTIVE = 8;
    localparam int H_FRONT = 2;
    localparam int H_SYNC = 2;
    localparam int H_TOTAL = 14;
    localparam int V_ACTIVE = 4;
    localparam int V_FRONT = 1;
    localparam int V_SYNC = 1;
    localparam int V_TOTAL = 7;

    logic clk = 1'b0;
    logic resetn = 1'b0;
    logic enable = 1'b1;
    logic [4:0] red;
    logic [5:0] green;
    logic [4:0] blue;
    logic dvi_clk;
    logic de;
    logic hsync;
    logic vsync;
    integer de_count = 0;
    integer hsync_count = 0;
    integer vsync_count = 0;

    always #3 clk = !clk;

    multitini_video_test #(
        .H_ACTIVE(H_ACTIVE), .H_FRONT(H_FRONT), .H_SYNC(H_SYNC),
        .H_TOTAL(H_TOTAL), .V_ACTIVE(V_ACTIVE), .V_FRONT(V_FRONT),
        .V_SYNC(V_SYNC), .V_TOTAL(V_TOTAL)
    ) dut (
        .pixel_clk(clk), .resetn(resetn), .enable(enable),
        .dvi_red(red), .dvi_grn(green), .dvi_blu(blue),
        .dvi_clk(dvi_clk), .dvi_de(de), .dvi_hsync(hsync),
        .dvi_vsync(vsync)
    );

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    initial begin
        repeat (3) @(negedge clk);
        resetn = 1'b1;
        repeat (H_TOTAL * V_TOTAL) begin
            @(posedge clk);
            #1;
            if (de) de_count = de_count + 1;
            if (hsync) hsync_count = hsync_count + 1;
            if (vsync) vsync_count = vsync_count + 1;
            check_value(!$isunknown({red, green, blue, dvi_clk, de, hsync, vsync}),
                   "video output is known");
        end
        check_value(de_count == H_ACTIVE * V_ACTIVE, "active pixel count");
        check_value(hsync_count == H_SYNC * V_TOTAL, "horizontal sync width");
        check_value(vsync_count == V_SYNC * H_TOTAL, "vertical sync width");

        enable = 1'b0;
        repeat (2) @(posedge clk);
        #1;
        check_value(!de && red == 0 && green == 0 && blue == 0,
               "disabled video blanks pixels");
        $display("PASS: video timing generator");
        $finish;
    end
endmodule
