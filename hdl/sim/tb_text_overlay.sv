// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_text_overlay;
    logic control_clk = 1'b0;
    logic control_resetn = 1'b0;
    logic vram_wr_en = 1'b0;
    logic [15:0] vram_wr_addr = 16'd0;
    logic [31:0] vram_wr_data = 32'd0;
    logic [3:0] vram_wr_strb = 4'd0;
    logic commit_valid = 1'b0;
    logic commit_visible = 1'b0;
    logic commit_bank = 1'b0;
    logic commit_ready;
    logic active_visible_control;
    logic active_bank_control;
    logic pixel_clk = 1'b0;
    logic pixel_resetn = 1'b0;
    logic [11:0] scan_x = 12'd0;
    logic [11:0] scan_y = 12'd0;
    logic vblank_start = 1'b0;
    logic pixel_opaque;
    logic [15:0] pixel_rgb565;

    always #5 control_clk = !control_clk;
    always #3 pixel_clk = !pixel_clk;

    multitini_text_overlay dut (
        .control_clk(control_clk), .control_resetn(control_resetn),
        .vram_wr_en(vram_wr_en), .vram_wr_addr(vram_wr_addr),
        .vram_wr_data(vram_wr_data), .vram_wr_strb(vram_wr_strb),
        .commit_valid(commit_valid), .commit_visible(commit_visible),
        .commit_bank(commit_bank), .commit_ready(commit_ready),
        .active_visible_control(active_visible_control),
        .active_bank_control(active_bank_control),
        .pixel_clk(pixel_clk), .pixel_resetn(pixel_resetn),
        .scan_x(scan_x), .scan_y(scan_y), .vblank_start(vblank_start),
        .pixel_opaque(pixel_opaque), .pixel_rgb565(pixel_rgb565)
    );

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic write_word(input logic [15:0] address,
                              input logic [31:0] value);
        @(negedge control_clk);
        vram_wr_addr = address;
        vram_wr_data = value;
        vram_wr_strb = 4'hF;
        vram_wr_en = 1'b1;
        @(negedge control_clk);
        vram_wr_en = 1'b0;
    endtask

    task automatic commit(input logic visible, input logic bank);
        while (!commit_ready) @(negedge control_clk);
        commit_visible = visible;
        commit_bank = bank;
        commit_valid = 1'b1;
        @(negedge control_clk);
        commit_valid = 1'b0;
        repeat (3) @(posedge pixel_clk);
        vblank_start = 1'b1;
        @(posedge pixel_clk);
        #1;
        vblank_start = 1'b0;
        repeat (3) @(posedge control_clk);
    endtask

    task automatic prefetch_first_cell;
        scan_x = 12'd2199;
        scan_y = 12'd1124;
        @(posedge pixel_clk);
        #1;
        scan_x = 12'd0;
        scan_y = 12'd0;
        #1;
    endtask

    initial begin
        repeat (3) @(negedge control_clk);
        control_resetn = 1'b1;
        pixel_resetn = 1'b1;

        // Cell 0 in bank 0 is an opaque teal space. Cell 0 in bank 1 is an
        // opaque warning-colored space. Packing also exercises both cells in
        // one 32-bit bus write.
        write_word(16'h1000, {16'h0820, 16'h3820});
        write_word(16'h2800, {16'h0820, 16'h4820});

        commit(1'b1, 1'b0);
        prefetch_first_cell();
        check_value(pixel_opaque && pixel_rgb565 == 16'h39D7,
                    "visible bank 0 background");
        check_value(active_visible_control && !active_bank_control,
                    "bank 0 state synchronized to control clock");

        commit(1'b1, 1'b1);
        // Allow the newly selected BRAM bank to feed the registered cell.
        prefetch_first_cell();
        check_value(pixel_opaque && pixel_rgb565 == 16'hF5AB,
                    "atomic switch to bank 1");
        check_value(active_visible_control && active_bank_control,
                    "bank 1 state synchronized to control clock");

        commit(1'b0, 1'b1);
        prefetch_first_cell();
        check_value(!pixel_opaque, "hidden overlay is transparent");
        $display("PASS: double-buffered text overlay");
        $finish;
    end
endmodule
