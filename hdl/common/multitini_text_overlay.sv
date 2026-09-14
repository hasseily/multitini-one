// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// One text bank. The ARM writes two 16-bit cells per 32-bit AXI
// word while the video side reads one cell. Keep this width conversion inside
// an explicit XPM so Vivado cannot dissolve the plane into flip-flops when the
// two clocks are unrelated.
module multitini_text_bank #(
    parameter int CELL_COUNT = multitini_regs_pkg::MT_UI_COLUMNS *
                               multitini_regs_pkg::MT_UI_ROWS
) (
    input  logic        write_clk,
    input  logic        write_enable,
    input  logic [$clog2((CELL_COUNT + 1) / 2)-1:0] write_address,
    input  logic [31:0] write_data,
    input  logic [3:0]  write_strobe,
    input  logic        read_clk,
    input  logic [$clog2(CELL_COUNT)-1:0] read_address,
    output logic [15:0] read_data
);
`ifdef SYNTHESIS
    logic [31:0] unused_write_data;

    xpm_memory_tdpram #(
        .ADDR_WIDTH_A($clog2((CELL_COUNT + 1) / 2)),
        .ADDR_WIDTH_B($clog2(CELL_COUNT)),
        .AUTO_SLEEP_TIME(0),
        .BYTE_WRITE_WIDTH_A(8),
        .BYTE_WRITE_WIDTH_B(16),
        .CLOCKING_MODE("independent_clock"),
        .ECC_MODE("no_ecc"),
        .MEMORY_INIT_FILE("none"),
        .MEMORY_INIT_PARAM("0"),
        .MEMORY_OPTIMIZATION("true"),
        .MEMORY_PRIMITIVE("block"),
        .MEMORY_SIZE(CELL_COUNT * 16),
        .MESSAGE_CONTROL(0),
        .READ_DATA_WIDTH_A(32),
        .READ_DATA_WIDTH_B(16),
        .READ_LATENCY_A(1),
        .READ_LATENCY_B(1),
        .READ_RESET_VALUE_A("0"),
        .READ_RESET_VALUE_B("0"),
        .RST_MODE_A("SYNC"),
        .RST_MODE_B("SYNC"),
        .SIM_ASSERT_CHK(1),
        .USE_EMBEDDED_CONSTRAINT(0),
        .USE_MEM_INIT(0),
        .WAKEUP_TIME("disable_sleep"),
        .WRITE_DATA_WIDTH_A(32),
        .WRITE_DATA_WIDTH_B(16),
        .WRITE_MODE_A("write_first"),
        .WRITE_MODE_B("read_first")
    ) memory_i (
        .dbiterra(), .dbiterrb(), .douta(unused_write_data),
        .doutb(read_data), .sbiterrb(), .sbiterra(),
        .addra(write_address), .addrb(read_address),
        .clka(write_clk), .clkb(read_clk),
        .dina(write_data), .dinb(16'd0),
        .ena(write_enable), .enb(1'b1),
        .injectdbiterra(1'b0), .injectdbiterrb(1'b0),
        .injectsbiterra(1'b0), .injectsbiterrb(1'b0),
        .regcea(1'b1), .regceb(1'b1),
        .rsta(1'b0), .rstb(1'b0), .sleep(1'b0),
        .wea(write_enable ? write_strobe : 4'd0), .web(1'b0)
    );
`else
    logic [15:0] memory [0:CELL_COUNT-1];
    logic [$clog2(CELL_COUNT)-1:0] write_cell_address;

    always_comb write_cell_address = {write_address, 1'b0};

    always_ff @(posedge write_clk) begin
        if (write_enable) begin
            if (write_strobe[0]) memory[write_cell_address][7:0] <=
                write_data[7:0];
            if (write_strobe[1]) memory[write_cell_address][15:8] <=
                write_data[15:8];
            if (write_strobe[2]) memory[write_cell_address + 1'b1][7:0] <=
                write_data[23:16];
            if (write_strobe[3]) memory[write_cell_address + 1'b1][15:8] <=
                write_data[31:24];
        end
    end

    always_ff @(posedge read_clk)
        read_data <= memory[read_address];
`endif
endmodule

// Double-buffered 128x60 text plane for the supervisor UI. Each 15x18 cell
// contains a 7x8 glyph at 2x scale, with one pixel between glyphs and one
// pixel above/below. Geometry and the VRAM map share the firmware ABI constants.
// Cell layout is:
//   [15] transparent background, [14:11] background, [10:7] foreground,
//   [6:0] ASCII. Firmware writes the inactive bank and commits it atomically;
// the video side changes visibility/bank only at vertical blank.
module multitini_text_overlay #(
    parameter int H_ACTIVE = 1920,
    parameter int H_TOTAL = 2200,
    parameter int V_ACTIVE = 1080,
    parameter int V_TOTAL = 1125
) (
    input  logic        control_clk,
    input  logic        control_resetn,
    input  logic        vram_wr_en,
    input  logic [15:0] vram_wr_addr,
    input  logic [31:0] vram_wr_data,
    input  logic [3:0]  vram_wr_strb,
    input  logic        commit_valid,
    input  logic        commit_visible,
    input  logic        commit_bank,
    input  logic        commit_dense,
    output logic        commit_ready,
    (* ASYNC_REG = "TRUE" *) output logic active_visible_control,
    (* ASYNC_REG = "TRUE" *) output logic active_bank_control,
    (* ASYNC_REG = "TRUE" *) output logic active_dense_control,

    input  logic        pixel_clk,
    input  logic        pixel_resetn,
    input  logic [11:0] scan_x,
    input  logic [11:0] scan_y,
    input  logic        vblank_start,
    output logic        pixel_opaque,
    output logic [15:0] pixel_rgb565
);
    import multitini_regs_pkg::*;

    localparam int COLUMNS = MT_UI_COLUMNS;
    localparam int ROWS = MT_UI_ROWS;
    localparam int CELL_COUNT = COLUMNS * ROWS;
    localparam int CELL_ADDRESS_WIDTH = $clog2(CELL_COUNT);
    localparam int WORD_ADDRESS_WIDTH = $clog2((CELL_COUNT + 1) / 2);
    localparam logic [15:0] BANK0_BASE = MT_UI_VRAM_BANK0_OFFSET;
    localparam logic [15:0] BANK1_BASE = MT_UI_VRAM_BANK1_OFFSET;
    localparam logic [15:0] BANK_BYTES = MT_UI_VRAM_BANK_BYTES;
    localparam logic [15:0] LEGACY_BANK0_BASE = MT_UI_LEGACY_VRAM_BANK0_OFFSET;
    localparam logic [15:0] LEGACY_BANK1_BASE = MT_UI_LEGACY_VRAM_BANK1_OFFSET;
    localparam logic [15:0] LEGACY_BANK_BYTES = MT_UI_LEGACY_VRAM_BANK_BYTES;

    // A synchronous font ROM breaks the character lookup away from the glyph
    // pixel mux.  Keeping the 96-way, 64-bit lookup combinational cannot meet
    // the 148.5 MHz video clock once the overlay is placed with the core.
    (* rom_style = "block" *) logic [63:0] font [0:95];
    logic [15:0] cell_q;
    logic [15:0] bank0_q;
    logic [15:0] bank1_q;
    logic bank0_write;
    logic bank1_write;
    logic legacy_bank0_write;
    logic legacy_bank1_write;
    logic [WORD_ADDRESS_WIDTH-1:0] write_address;
    logic [2:0] commit_source;
    logic [2:0] commit_destination;
    logic commit_destination_valid;
    logic active_visible;
    logic active_bank;
    logic active_dense;
    logic [7:0] columns;
    logic [4:0] cell_width;
    logic [4:0] cell_height;
    (* ASYNC_REG = "TRUE" *) logic active_visible_meta;
    (* ASYNC_REG = "TRUE" *) logic active_bank_meta;
    (* ASYNC_REG = "TRUE" *) logic active_dense_meta;
    logic [11:0] fetch_x;
    logic [11:0] fetch_y;
    logic [4:0] fetch_local_x;
    logic [4:0] fetch_local_y;
    logic [CELL_ADDRESS_WIDTH-1:0] fetch_row_base;
    logic [CELL_ADDRESS_WIDTH-1:0] fetch_address;
    logic fetch_active;
    logic [4:0] local_x_q;
    logic [4:0] local_y_q;
    logic [4:0] local_x_qq;
    logic [4:0] local_y_qq;
    logic [CELL_ADDRESS_WIDTH-1:0] read_address;
    logic coordinate_active_q;
    logic coordinate_active_qq;
    logic active_visible_q;
    logic active_visible_qq;
    logic [63:0] glyph_q;
    logic [7:0] glyph_row;
    logic glyph_pixel;
    logic [3:0] foreground_q;
    logic [3:0] background_q;
    logic transparent_q;

    initial $readmemh("multitini_font7x8.mem", font);

    assign commit_source = {commit_dense, commit_bank, commit_visible};
    multitini_async_mailbox #(.WIDTH(3)) commit_mailbox (
        .src_clk(control_clk), .src_resetn(control_resetn),
        .src_data(commit_source), .src_valid(commit_valid),
        .src_ready(commit_ready),
        .dst_clk(pixel_clk), .dst_resetn(pixel_resetn),
        .dst_data(commit_destination),
        .dst_valid(commit_destination_valid),
        .dst_ready(vblank_start)
    );

    always_ff @(posedge control_clk) begin
        if (!control_resetn) begin
            active_visible_meta <= 1'b0;
            active_visible_control <= 1'b0;
            active_bank_meta <= 1'b0;
            active_bank_control <= 1'b0;
            active_dense_meta <= 1'b0;
            active_dense_control <= 1'b0;
        end else begin
            active_visible_meta <= active_visible;
            active_visible_control <= active_visible_meta;
            active_bank_meta <= active_bank;
            active_bank_control <= active_bank_meta;
            active_dense_meta <= active_dense;
            active_dense_control <= active_dense_meta;
        end
    end

    always_comb begin
        legacy_bank0_write = vram_wr_en && vram_wr_addr[1:0] == 2'b00 &&
                             vram_wr_addr >= LEGACY_BANK0_BASE &&
                             vram_wr_addr < LEGACY_BANK0_BASE + LEGACY_BANK_BYTES;
        legacy_bank1_write = vram_wr_en && vram_wr_addr[1:0] == 2'b00 &&
                             vram_wr_addr >= LEGACY_BANK1_BASE &&
                             vram_wr_addr < LEGACY_BANK1_BASE + LEGACY_BANK_BYTES;
        bank0_write = vram_wr_en && vram_wr_addr[1:0] == 2'b00 &&
                      vram_wr_addr >= BANK0_BASE &&
                      vram_wr_addr < BANK0_BASE + BANK_BYTES;
        bank1_write = vram_wr_en && vram_wr_addr[1:0] == 2'b00 &&
                      vram_wr_addr >= BANK1_BASE &&
                      vram_wr_addr < BANK1_BASE + BANK_BYTES;
        if (legacy_bank1_write)
            write_address = (vram_wr_addr - LEGACY_BANK1_BASE) >> 2;
        else if (legacy_bank0_write)
            write_address = (vram_wr_addr - LEGACY_BANK0_BASE) >> 2;
        else if (bank1_write)
            write_address = (vram_wr_addr - BANK1_BASE) >> 2;
        else
            write_address = (vram_wr_addr - BANK0_BASE) >> 2;
        bank0_write = bank0_write || legacy_bank0_write;
        bank1_write = bank1_write || legacy_bank1_write;
    end

    multitini_text_bank #(.CELL_COUNT(CELL_COUNT)) bank0_i (
        .write_clk(control_clk), .write_enable(bank0_write),
        .write_address(write_address), .write_data(vram_wr_data),
        .write_strobe(vram_wr_strb), .read_clk(pixel_clk),
        .read_address(read_address), .read_data(bank0_q)
    );

    multitini_text_bank #(.CELL_COUNT(CELL_COUNT)) bank1_i (
        .write_clk(control_clk), .write_enable(bank1_write),
        .write_address(write_address), .write_data(vram_wr_data),
        .write_strobe(vram_wr_strb), .read_clk(pixel_clk),
        .read_address(read_address), .read_data(bank1_q)
    );

    always_comb cell_q = active_bank ? bank1_q : bank0_q;

    assign columns = active_dense ? MT_UI_COLUMNS : MT_UI_LEGACY_COLUMNS;
    assign cell_width = active_dense ? MT_UI_CELL_WIDTH : MT_UI_LEGACY_CELL_WIDTH;
    assign cell_height = active_dense ? MT_UI_CELL_HEIGHT : MT_UI_LEGACY_CELL_HEIGHT;

    // The text BRAM, synchronous font ROM, and registered glyph/color output
    // add three cycles before multitini_video_test samples the overlay.  A
    // sequential cursor stays three pixels ahead of the scan and advances the
    // cell address only at cell and row boundaries. This deliberately
    // avoids putting constant dividers and modulo operators on the 148.5 MHz
    // BRAM-address path.
    assign read_address = fetch_address;

    always_ff @(posedge pixel_clk) begin
        if (!pixel_resetn) begin
            active_visible <= 1'b0;
            active_bank <= 1'b0;
            active_dense <= 1'b0;
            fetch_x <= 12'd3;
            fetch_y <= 12'd0;
            fetch_local_x <= 5'd3;
            fetch_local_y <= 5'd0;
            fetch_row_base <= '0;
            fetch_address <= '0;
            fetch_active <= 1'b1;
            local_x_q <= 5'd0;
            local_y_q <= 5'd0;
            local_x_qq <= 5'd0;
            local_y_qq <= 5'd0;
            coordinate_active_q <= 1'b0;
            coordinate_active_qq <= 1'b0;
            active_visible_q <= 1'b0;
            active_visible_qq <= 1'b0;
            glyph_q <= 64'd0;
            foreground_q <= 4'd0;
            background_q <= 4'd0;
            transparent_q <= 1'b0;
            pixel_opaque <= 1'b0;
            pixel_rgb565 <= 16'd0;
        end else begin
            if (commit_destination_valid && vblank_start) begin
                active_visible <= commit_destination[0];
                active_bank <= commit_destination[1];
                active_dense <= commit_destination[2];
            end
            // Re-anchor once per frame. At scan (0,0), the current request is
            // pixel 3 and the next request must be pixel 4.
            if (scan_x == 12'd0 && scan_y == 12'd0) begin
                fetch_x <= 12'd4;
                fetch_y <= 12'd0;
                fetch_local_x <= 5'd4;
                fetch_local_y <= 5'd0;
                fetch_row_base <= '0;
                fetch_address <= '0;
                fetch_active <= 1'b1;
            end else if (fetch_x == H_TOTAL - 1) begin
                fetch_x <= 12'd0;
                fetch_local_x <= 5'd0;
                if (fetch_y == V_TOTAL - 1) begin
                    fetch_y <= 12'd0;
                    fetch_local_y <= 5'd0;
                    fetch_row_base <= '0;
                    fetch_address <= '0;
                    fetch_active <= 1'b1;
                end else begin
                    fetch_y <= fetch_y + 12'd1;
                    if (fetch_y < V_ACTIVE - 1) begin
                        fetch_active <= 1'b1;
                        if (fetch_local_y == cell_height - 1) begin
                            fetch_local_y <= 5'd0;
                            fetch_row_base <= fetch_row_base + columns;
                            fetch_address <= fetch_row_base + columns;
                        end else begin
                            fetch_local_y <= fetch_local_y + 5'd1;
                            fetch_address <= fetch_row_base;
                        end
                    end else begin
                        fetch_local_y <= 5'd0;
                        fetch_row_base <= '0;
                        fetch_address <= '0;
                        fetch_active <= 1'b0;
                    end
                end
            end else begin
                fetch_x <= fetch_x + 12'd1;
                if (fetch_y < V_ACTIVE && fetch_x < H_ACTIVE - 1) begin
                    fetch_active <= 1'b1;
                    if (fetch_local_x == cell_width - 1) begin
                        fetch_local_x <= 5'd0;
                        fetch_address <= fetch_address + 1'b1;
                    end else begin
                        fetch_local_x <= fetch_local_x + 5'd1;
                    end
                end else begin
                    fetch_local_x <= 5'd0;
                    fetch_active <= 1'b0;
                end
            end
            local_x_q <= fetch_local_x;
            local_y_q <= fetch_local_y;
            coordinate_active_q <= fetch_active;
            active_visible_q <= active_visible;
            local_x_qq <= local_x_q;
            local_y_qq <= local_y_q;
            coordinate_active_qq <= coordinate_active_q;
            active_visible_qq <= active_visible_q;
            if (cell_q[6:0] >= 7'd32 && cell_q[6:0] <= 7'd127)
                glyph_q <= font[cell_q[6:0] - 7'd32];
            else
                glyph_q <= 64'd0;
            foreground_q <= cell_q[10:7];
            background_q <= cell_q[14:11];
            transparent_q <= cell_q[15];
            pixel_opaque <= active_visible_qq && coordinate_active_qq &&
                            (glyph_pixel || !transparent_q);
            pixel_rgb565 <= palette(glyph_pixel ? foreground_q : background_q);
        end
    end

    function automatic logic [15:0] palette(input logic [3:0] index);
        case (index)
        4'h0: palette = 16'h0841; // background
        4'h1: palette = 16'h10A3; // panel
        4'h2: palette = 16'h18E4; // raised panel
        4'h3: palette = 16'h10C3; // row
        4'h4: palette = 16'h2166; // active row
        4'h5: palette = 16'hE75E; // text
        4'h6: palette = 16'hA596; // muted
        4'h7: palette = 16'h39D7; // accent teal
        4'h8: palette = 16'h6CFB; // accent blue
        4'h9: palette = 16'hF5AB; // warning
        4'hA: palette = 16'h8ED1; // success
        4'hB: palette = 16'h3186; // border
        4'hC: palette = 16'h4208; // dim
        4'hD: palette = 16'hF800;
        4'hE: palette = 16'h07E0;
        default: palette = 16'hFFFF;
        endcase
    endfunction

    always_comb begin
        glyph_row = 8'd0;
        glyph_pixel = 1'b0;
        if (active_dense) begin
            if (local_y_qq >= MT_UI_GLYPH_Y &&
                local_y_qq < MT_UI_GLYPH_Y + 8 * MT_UI_FONT_SCALE)
                glyph_row = glyph_q[(((local_y_qq - MT_UI_GLYPH_Y) /
                                      MT_UI_FONT_SCALE) * 8) +: 8];
            if (local_x_qq >= MT_UI_GLYPH_X &&
                local_x_qq < MT_UI_GLYPH_X + 7 * MT_UI_FONT_SCALE &&
                local_y_qq >= MT_UI_GLYPH_Y &&
                local_y_qq < MT_UI_GLYPH_Y + 8 * MT_UI_FONT_SCALE)
                glyph_pixel = glyph_row[(local_x_qq - MT_UI_GLYPH_X) /
                                        MT_UI_FONT_SCALE];
        end else begin
            if (local_y_qq >= MT_UI_LEGACY_GLYPH_Y &&
                local_y_qq < MT_UI_LEGACY_GLYPH_Y + 8 * MT_UI_LEGACY_FONT_SCALE)
                glyph_row = glyph_q[(((local_y_qq - MT_UI_LEGACY_GLYPH_Y) /
                                      MT_UI_LEGACY_FONT_SCALE) * 8) +: 8];
            if (local_x_qq >= MT_UI_LEGACY_GLYPH_X &&
                local_x_qq < MT_UI_LEGACY_GLYPH_X + 7 * MT_UI_LEGACY_FONT_SCALE &&
                local_y_qq >= MT_UI_LEGACY_GLYPH_Y &&
                local_y_qq < MT_UI_LEGACY_GLYPH_Y + 8 * MT_UI_LEGACY_FONT_SCALE)
                glyph_pixel = glyph_row[(local_x_qq - MT_UI_LEGACY_GLYPH_X) /
                                        MT_UI_LEGACY_FONT_SCALE];
        end
    end
endmodule
