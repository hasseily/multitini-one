// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_text_overlay #(
    parameter bit DYNAMIC_DISPLAY = 1'b0
);
    import multitini_regs_pkg::*;
    integer H_ACTIVE = 1920, H_TOTAL = 2200;
    integer V_ACTIVE = 1080, V_TOTAL = 1125;
    integer display_mode = 0;
    localparam int CELL_COUNT = MT_UI_COLUMNS * MT_UI_ROWS;
    logic control_clk = 0, control_resetn = 0;
    logic vram_wr_en = 0;
    logic [15:0] vram_wr_addr = 0;
    logic [31:0] vram_wr_data = 0;
    logic [3:0] vram_wr_strb = 0;
    logic commit_valid = 0, commit_visible = 0, commit_bank = 0, commit_dense = 0;
    logic commit_ready, active_visible_control, active_bank_control, active_dense_control;
    logic pixel_clk = 0, pixel_resetn = 0;
    logic [11:0] scan_x = 0, scan_y = 0;
    logic vblank_start, pixel_opaque;
    logic [15:0] pixel_rgb565;
    logic [15:0] expected_cells [0:1][0:CELL_COUNT-1];
    logic [63:0] expected_font [0:95];
    bit verify_pixels = 0, expected_bank = 0, expected_dense = 0, expected_visible = 0;
    integer verified_active = 0, verified_blank = 0;
    integer verified_foreground = 0, verified_transparent = 0, completed_frames = 0;
    integer verified_tail = 0, verified_tail_opaque = 0, verified_tail_transparent = 0;
    integer verified_tail_column = 0, verified_tail_row = 0, verified_tail_corner = 0;

    always #5 control_clk = !control_clk;
    always #3 pixel_clk = !pixel_clk;
    assign vblank_start = scan_x == 0 && scan_y == V_ACTIVE;
    always_ff @(posedge pixel_clk) begin
        if (!pixel_resetn) begin
            scan_x <= 0;
            scan_y <= 0;
        end else if (scan_x == H_TOTAL - 1) begin
            scan_x <= 0;
            scan_y <= scan_y == V_TOTAL - 1 ? 12'd0 : scan_y + 12'd1;
        end else scan_x <= scan_x + 12'd1;
    end

    multitini_text_overlay #(.DYNAMIC_DISPLAY(DYNAMIC_DISPLAY)) dut (
        .display_h_active(12'(H_ACTIVE)), .display_h_total(12'(H_TOTAL)),
        .display_v_active(12'(V_ACTIVE)), .display_v_total(12'(V_TOTAL)), .*);

    task automatic check_value(input logic condition, input string message);
        if (condition !== 1'b1) $fatal(1, "%s", message);
    endtask

    function automatic logic [15:0] reference_palette(input logic [3:0] index);
        case (index)
            0: reference_palette = 16'h0841;
            1: reference_palette = 16'h10A3;
            2: reference_palette = 16'h18E4;
            3: reference_palette = 16'h10C3;
            4: reference_palette = 16'h2166;
            5: reference_palette = 16'hE75E;
            6: reference_palette = 16'hA596;
            7: reference_palette = 16'h39D7;
            8: reference_palette = 16'h6CFB;
            9: reference_palette = 16'hF5AB;
            10: reference_palette = 16'h8ED1;
            11: reference_palette = 16'h3186;
            12: reference_palette = 16'h4208;
            13: reference_palette = 16'hF800;
            14: reference_palette = 16'h07E0;
            15: reference_palette = 16'hFFFF;
        endcase
    endfunction

    // Sample before this edge's pipeline updates, just as the video output
    // stage does. The reference derives every pixel from raster coordinates.
    always @(posedge pixel_clk) begin : pixel_scoreboard
        integer columns, rows, cell_width, cell_height, scale, origin_x, origin_y;
        integer index, column, row, local_x, local_y, glyph_x, glyph_y;
        logic [15:0] cell_value, color;
        logic [63:0] glyph;
        logic foreground, opaque, tail_column, tail_row;
        if (pixel_resetn)
            check_value(dut.read_address < CELL_COUNT, "BRAM read address within all 7680 cells");
        if (verify_pixels) begin
            columns = expected_dense ? MT_UI_COLUMNS : MT_UI_LEGACY_COLUMNS;
            rows = expected_dense ? MT_UI_ROWS : MT_UI_LEGACY_ROWS;
            cell_width = expected_dense ? MT_UI_CELL_WIDTH : MT_UI_LEGACY_CELL_WIDTH;
            cell_height = expected_dense ? MT_UI_CELL_HEIGHT : MT_UI_LEGACY_CELL_HEIGHT;
            scale = expected_dense ? MT_UI_FONT_SCALE : MT_UI_LEGACY_FONT_SCALE;
            origin_x = expected_dense ? MT_UI_GLYPH_X : MT_UI_LEGACY_GLYPH_X;
            origin_y = expected_dense ? MT_UI_GLYPH_Y : MT_UI_LEGACY_GLYPH_Y;
            // Independent arithmetic reference for the finite hardware decode.
            if (DYNAMIC_DISPLAY && H_ACTIVE < 1920) begin
                cell_width = H_ACTIVE / columns;
                cell_height = V_ACTIVE / rows;
                scale = 1;
                origin_x = (cell_width - 7) / 2;
                origin_y = (cell_height - 8) / 2;
            end
            if (scan_x < H_ACTIVE && scan_y < V_ACTIVE) begin
                    // The floor-sized grid retains its geometry. Physical
                    // remainder strips inherit the nearest edge cell's
                    // background/transparency, without repeating its glyph.
                    column = scan_x / cell_width;
                    row = scan_y / cell_height;
                    tail_column = column >= columns;
                    tail_row = row >= rows;
                    if (tail_column) column = columns - 1;
                    if (tail_row) row = rows - 1;
                    index = row * columns + column;
                    local_x = scan_x % cell_width;
                    local_y = scan_y % cell_height;
                    cell_value = expected_cells[expected_bank][index];
                    glyph = cell_value[6:0] >= 32 ? expected_font[cell_value[6:0] - 32] : 64'd0;
                    foreground = 0;
                    if (!tail_column && !tail_row &&
                        local_x >= origin_x && local_x < origin_x + 7 * scale &&
                        local_y >= origin_y && local_y < origin_y + 8 * scale) begin
                        glyph_x = (local_x - origin_x) / scale;
                        glyph_y = (local_y - origin_y) / scale;
                        foreground = glyph[glyph_y * 8 + glyph_x];
                    end
                    opaque = expected_visible && (foreground || !cell_value[15]);
                    color = reference_palette(foreground ? cell_value[10:7] : cell_value[14:11]);
                    if (pixel_opaque !== opaque || pixel_rgb565 !== color)
                        $fatal(1, "pixel (%0d,%0d), bank=%0d dense=%0d cell=%0d: expected %0d/%04h got %0d/%04h",
                               scan_x, scan_y, expected_bank, expected_dense, index,
                               opaque, color, pixel_opaque, pixel_rgb565);
                    if (foreground) verified_foreground = verified_foreground + 1;
                    if (!opaque) verified_transparent = verified_transparent + 1;
                    if (tail_column || tail_row) begin
                        verified_tail = verified_tail + 1;
                        if (opaque) verified_tail_opaque = verified_tail_opaque + 1;
                        else verified_tail_transparent = verified_tail_transparent + 1;
                        if (tail_column && tail_row) verified_tail_corner = verified_tail_corner + 1;
                        else if (tail_column) verified_tail_column = verified_tail_column + 1;
                        else verified_tail_row = verified_tail_row + 1;
                    end
                verified_active = verified_active + 1;
            end else begin
                check_value(!pixel_opaque, "all horizontal and vertical blanking transparent");
                verified_blank = verified_blank + 1;
            end
        end
    end

    task automatic write_word(input logic [15:0] address,
                              input logic [31:0] value, input logic [3:0] strobes);
        @(negedge control_clk);
        vram_wr_addr = address;
        vram_wr_data = value;
        vram_wr_strb = strobes;
        vram_wr_en = 1;
        @(negedge control_clk);
        vram_wr_en = 0;
    endtask

    task automatic set_cells(input integer bank, input integer index,
                             input logic [31:0] value, input logic [3:0] strobes,
                             input bit legacy);
        integer address;
        address = legacy ? (bank ? MT_UI_LEGACY_VRAM_BANK1_OFFSET : MT_UI_LEGACY_VRAM_BANK0_OFFSET) :
                           (bank ? MT_UI_VRAM_BANK1_OFFSET : MT_UI_VRAM_BANK0_OFFSET);
        write_word(address + index * 2, value, strobes);
        if (strobes[0]) expected_cells[bank][index][7:0] = value[7:0];
        if (strobes[1]) expected_cells[bank][index][15:8] = value[15:8];
        if (strobes[2]) expected_cells[bank][index + 1][7:0] = value[23:16];
        if (strobes[3]) expected_cells[bank][index + 1][15:8] = value[31:24];
    endtask

    task automatic request_commit(input bit visible, input bit bank, input bit dense);
        bit previous_visible, previous_bank, previous_dense;
        previous_visible = dut.active_visible;
        previous_bank = dut.active_bank;
        previous_dense = dut.active_dense;
        @(negedge control_clk);
        while (!commit_ready) @(negedge control_clk);
        commit_visible = visible;
        commit_bank = bank;
        commit_dense = dense;
        commit_valid = 1;
        @(negedge control_clk);
        commit_valid = 0;
        repeat (16) @(negedge pixel_clk);
        check_value(!commit_ready, "commit remains pending before vertical blank");
        check_value(dut.active_visible == previous_visible &&
                    dut.active_bank == previous_bank && dut.active_dense == previous_dense,
                    "bank, visibility, geometry stay atomic before vertical blank");
        // The mailbox must retain its snapshot of all three input fields.
        commit_visible = !visible;
        commit_bank = !bank;
        commit_dense = !dense;
    endtask

    task automatic await_commit(input bit visible, input bit bank, input bit dense);
        while (!commit_ready) @(negedge control_clk);
        check_value(active_visible_control == visible && active_bank_control == bank &&
                    active_dense_control == dense, "committed state synchronizes to control clock");
        expected_visible = visible;
        expected_bank = bank;
        expected_dense = dense;
    endtask

    task automatic check_frame;
        integer active_before, blank_before;
        while (scan_x != 0 || scan_y != 0) @(negedge pixel_clk);
        active_before = verified_active;
        blank_before = verified_blank;
        verify_pixels = 1;
        repeat (H_TOTAL * V_TOTAL) @(negedge pixel_clk);
        verify_pixels = 0;
        check_value(verified_active - active_before == H_ACTIVE * V_ACTIVE,
                    "every active pixel verified, including final row and column");
        check_value(verified_blank - blank_before == H_TOTAL * V_TOTAL - H_ACTIVE * V_ACTIVE,
                    "every blanking pixel verified");
        completed_frames = completed_frames + 1;
        $display("PASS: full raster bank=%0d dense=%0d visible=%0d", expected_bank,
                 expected_dense, expected_visible);
        $fflush();
    endtask

    task automatic frame_and_next_commit(input bit visible, input bit bank, input bit dense);
        fork
            check_frame();
            begin
                @(negedge pixel_clk);
                while (!verify_pixels) @(negedge pixel_clk);
                while (scan_x != 100 || scan_y != 100) @(negedge pixel_clk);
                request_commit(visible, bank, dense);
            end
        join
        await_commit(visible, bank, dense);
    endtask

    initial begin : stimulus
        integer bank, index, lane;
        logic [15:0] low_cell, high_cell;
        if (DYNAMIC_DISPLAY) begin
            if (!$value$plusargs("MODE=%d", display_mode)) display_mode = 0;
            // Expected public output dimensions, independent of the RTL package.
            case (display_mode)
                0: begin H_ACTIVE=1920; H_TOTAL=2200; V_ACTIVE=1080; V_TOTAL=1125; end
                1: begin H_ACTIVE=1024; H_TOTAL=1344; V_ACTIVE=768; V_TOTAL=806; end
                2: begin H_ACTIVE=1200; H_TOTAL=1360; V_ACTIVE=800; V_TOTAL=828; end
                3: begin H_ACTIVE=1280; H_TOTAL=1688; V_ACTIVE=1024; V_TOTAL=1066; end
                4: begin H_ACTIVE=1360; H_TOTAL=1792; V_ACTIVE=768; V_TOTAL=795; end
                5: begin H_ACTIVE=1680; H_TOTAL=1840; V_ACTIVE=1050; V_TOTAL=1080; end
                default: $fatal(1, "invalid display mode");
            endcase
        end
        $readmemh("multitini_font7x8.mem", expected_font);
        check_value(MT_UI_COLUMNS == 128 && MT_UI_ROWS == 60 &&
                    MT_UI_CELL_WIDTH == 15 && MT_UI_CELL_HEIGHT == 18 &&
                    MT_UI_FONT_SCALE == 2 && MT_UI_GLYPH_X == 0 && MT_UI_GLYPH_Y == 1,
                    "128x60 geometry with tightly spaced 2x glyphs");
        repeat (3) @(negedge control_clk);
        control_resetn = 1;
        pixel_resetn = 1;
        for (bank = 0; bank < 2; bank = bank + 1) begin
            for (index = 0; index < CELL_COUNT; index = index + 2) begin
                low_cell = ((index % 3 == 0) << 15) | (((index / 7 + bank) % 16) << 11) |
                           (((index / 5 + bank + 5) % 16) << 7) | (32 + (index + bank * 17) % 96);
                high_cell = (((index + 1) % 3 == 0) << 15) | ((((index + 1) / 7 + bank) % 16) << 11) |
                            ((((index + 1) / 5 + bank + 5) % 16) << 7) | (32 + (index + 1 + bank * 17) % 96);
                set_cells(bank, index, {high_cell, low_cell}, 4'hF, 0);
            end
            // Every byte lane, both packed cells, and the highest RAM address.
            for (lane = 0; lane < 4; lane = lane + 1) begin
                set_cells(bank, 0, 32'hFF412A57, 1 << lane, 0);
                set_cells(bank, CELL_COUNT - 2, 32'hAAAABBBB, 1 << lane, 0);
            end
            set_cells(bank, 2, 32'h00617FFF, 4'h0, 0);
            // Legacy aliases share RAM, including the final legacy word.
            set_cells(bank, MT_UI_LEGACY_COLUMNS * MT_UI_LEGACY_ROWS - 2,
                      32'h0A4DAF1F, 4'hF, 1);
            set_cells(bank, 4, 32'hFA4107C2, 4'h5, 1);
        end
        // Register space, inter-bank holes, bank ends, and unaligned writes
        // must neither change nor alias any valid cell.
        write_word(16'h0000, 0, 4'hF);
        write_word(MT_UI_LEGACY_VRAM_BANK0_OFFSET - 4, 0, 4'hF);
        write_word(MT_UI_LEGACY_VRAM_BANK0_OFFSET + MT_UI_LEGACY_VRAM_BANK_BYTES, 0, 4'hF);
        write_word(MT_UI_LEGACY_VRAM_BANK1_OFFSET + MT_UI_LEGACY_VRAM_BANK_BYTES, 0, 4'hF);
        write_word(MT_UI_VRAM_BANK0_OFFSET - 4, 0, 4'hF);
        write_word(MT_UI_VRAM_BANK0_OFFSET + MT_UI_VRAM_BANK_BYTES, 0, 4'hF);
        write_word(MT_UI_VRAM_BANK1_OFFSET + MT_UI_VRAM_BANK_BYTES, 0, 4'hF);
        write_word(16'hFFFC, 0, 4'hF);
        for (lane = 1; lane < 4; lane = lane + 1) begin
            write_word(MT_UI_VRAM_BANK0_OFFSET + lane, 0, 4'hF);
            write_word(MT_UI_VRAM_BANK1_OFFSET + lane, 0, 4'hF);
            write_word(MT_UI_LEGACY_VRAM_BANK0_OFFSET + lane, 0, 4'hF);
            write_word(MT_UI_LEGACY_VRAM_BANK1_OFFSET + lane, 0, 4'hF);
        end
        request_commit(1, 0, 0);
        await_commit(1, 0, 0);
        frame_and_next_commit(1, 1, 1);
        frame_and_next_commit(1, 0, 1);
        frame_and_next_commit(1, 1, 0);
        frame_and_next_commit(0, 1, 1);
        check_frame();
        check_value(verified_foreground > 0 && verified_transparent > 0,
                    "glyph strokes and transparent backgrounds both exercised");
        if (DYNAMIC_DISPLAY && display_mode != 0) begin
            check_value(verified_tail > 0 && verified_tail_opaque > 0 && verified_tail_transparent > 0,
                        "remainder background includes opaque and transparent edge cells");
            check_value(verified_tail_row > 0, "bottom remainder strip checked");
            if (display_mode == 1 || display_mode == 2 || display_mode == 4 || display_mode == 5)
                check_value(verified_tail_column > 0 && verified_tail_corner > 0,
                            "right remainder strip and bottom-right corner checked");
        end
        $display("PASS: remainder background mode=%0d tail=%0d opaque=%0d transparent=%0d right=%0d bottom=%0d corner=%0d",
                 display_mode, verified_tail, verified_tail_opaque, verified_tail_transparent,
                 verified_tail_column, verified_tail_row, verified_tail_corner);
        $display("PASS: mode=%0d dynamic=%0d; %0d full overlay rasters; %0d active and %0d blank pixels",
                 display_mode, DYNAMIC_DISPLAY, completed_frames, verified_active, verified_blank);
        $finish;
    end

    initial begin
        #250000000;
        $fatal(1, "text overlay test timed out");
    end
endmodule
