// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// 1920x1080p60 timing, diagnostic fallback, and physical DVI output stage for
// a 148.5 MHz pixel clock. The machine framebuffer is selected only when its
// bridge has latched machine_enable at vblank; otherwise diagnostics remain on.
module multitini_video_test #(
    parameter int H_ACTIVE = 1920,
    parameter int H_FRONT  = 88,
    parameter int H_SYNC   = 44,
    parameter int H_TOTAL  = 2200,
    parameter int V_ACTIVE = 1080,
    parameter int V_FRONT  = 4,
    parameter int V_SYNC   = 5,
    parameter int V_TOTAL  = 1125
) (
    input  logic       pixel_clk,
    input  logic       resetn,
    input  logic       enable,
    input  logic       machine_enable,
    input  logic [15:0] machine_rgb565,
    input  logic        border_opaque,
    input  logic [15:0] border_rgb565,
    input  logic       ui_opaque,
    input  logic [15:0] ui_rgb565,
    output logic [11:0] scan_x,
    output logic [11:0] scan_y,
    output logic        vblank_start,
    (* IOB = "TRUE" *) output logic [4:0] dvi_red,
    (* IOB = "TRUE" *) output logic [5:0] dvi_grn,
    (* IOB = "TRUE" *) output logic [4:0] dvi_blu,
    output logic       dvi_clk,
    (* IOB = "TRUE" *) output logic       dvi_de,
    (* IOB = "TRUE" *) output logic       dvi_hsync,
    (* IOB = "TRUE" *) output logic       dvi_vsync
);
    localparam int MAIN_X0 = 384;
    localparam int MAIN_Y0 = 408;
    localparam int MAIN_WIDTH = 1152;
    localparam int MAIN_HEIGHT = 112;
    localparam int ONE_X0 = 864;
    localparam int ONE_Y0 = 568;
    localparam int ONE_WIDTH = 192;
    localparam int ONE_HEIGHT = 56;
    localparam int HINT_X0 = 672;
    localparam int HINT_Y0 = 680;
    localparam int HINT_WIDTH = 576;
    localparam int HINT_HEIGHT = 28;

    logic [11:0] x;
    logic [11:0] y;
    logic [15:0] diagnostic_rgb565;
    logic [15:0] diagnostic_rgb565_q;
    logic [15:0] selected_rgb565;
    logic active;
    logic use_machine;
    logic unused_enable;
    logic [11:0] main_dx;
    logic [11:0] main_dy;
    logic [11:0] one_dx;
    logic [11:0] one_dy;
    logic [11:0] hint_dx;
    logic [11:0] hint_dy;
    logic [3:0] main_index;
    logic [1:0] one_index;
    logic [4:0] hint_index;
    logic [2:0] main_column;
    logic [2:0] main_row;
    logic [2:0] one_column;
    logic [2:0] one_row;
    logic [2:0] hint_column;
    logic [2:0] hint_row;
    logic [34:0] main_glyph;
    logic [34:0] one_glyph;
    logic [34:0] hint_glyph;
    logic main_pixel;
    logic one_pixel;
    logic hint_pixel;

    function automatic logic [34:0] standby_glyph(input logic [6:0] character);
        case (character)
            7'h31: standby_glyph = {5'b00100, 5'b01100, 5'b00100,
                                    5'b00100, 5'b00100, 5'b00100,
                                    5'b01110};
            7'h32: standby_glyph = {5'b01110, 5'b10001, 5'b00001,
                                    5'b00010, 5'b00100, 5'b01000,
                                    5'b11111};
            7'h45: standby_glyph = {5'b11111, 5'b10000, 5'b10000,
                                    5'b11110, 5'b10000, 5'b10000,
                                    5'b11111};
            7'h46: standby_glyph = {5'b11111, 5'b10000, 5'b10000,
                                    5'b11110, 5'b10000, 5'b10000,
                                    5'b10000};
            7'h49: standby_glyph = {5'b11111, 5'b00100, 5'b00100,
                                    5'b00100, 5'b00100, 5'b00100,
                                    5'b11111};
            7'h4C: standby_glyph = {5'b10000, 5'b10000, 5'b10000,
                                    5'b10000, 5'b10000, 5'b10000,
                                    5'b11111};
            7'h4D: standby_glyph = {5'b10001, 5'b11011, 5'b10101,
                                    5'b10101, 5'b10001, 5'b10001,
                                    5'b10001};
            7'h4E: standby_glyph = {5'b10001, 5'b11001, 5'b10101,
                                    5'b10011, 5'b10001, 5'b10001,
                                    5'b10001};
            7'h4F: standby_glyph = {5'b01110, 5'b10001, 5'b10001,
                                    5'b10001, 5'b10001, 5'b10001,
                                    5'b01110};
            7'h50: standby_glyph = {5'b11110, 5'b10001, 5'b10001,
                                    5'b11110, 5'b10000, 5'b10000,
                                    5'b10000};
            7'h52: standby_glyph = {5'b11110, 5'b10001, 5'b10001,
                                    5'b11110, 5'b10100, 5'b10010,
                                    5'b10001};
            7'h53: standby_glyph = {5'b01111, 5'b10000, 5'b10000,
                                    5'b01110, 5'b00001, 5'b00001,
                                    5'b11110};
            7'h54: standby_glyph = {5'b11111, 5'b00100, 5'b00100,
                                    5'b00100, 5'b00100, 5'b00100,
                                    5'b00100};
            7'h55: standby_glyph = {5'b10001, 5'b10001, 5'b10001,
                                    5'b10001, 5'b10001, 5'b10001,
                                    5'b01110};
            default: standby_glyph = 35'd0;
        endcase
    endfunction

    function automatic logic [6:0] main_character(input logic [3:0] index);
        case (index)
            4'd0: main_character = 7'h4D;
            4'd1: main_character = 7'h55;
            4'd2: main_character = 7'h4C;
            4'd3: main_character = 7'h54;
            4'd4: main_character = 7'h49;
            4'd5: main_character = 7'h54;
            4'd6: main_character = 7'h49;
            4'd7: main_character = 7'h4E;
            4'd8: main_character = 7'h49;
            default: main_character = 7'h20;
        endcase
    endfunction

    function automatic logic [6:0] one_character(input logic [1:0] index);
        case (index)
            2'd0: one_character = 7'h4F;
            2'd1: one_character = 7'h4E;
            2'd2: one_character = 7'h45;
            default: one_character = 7'h20;
        endcase
    endfunction

    function automatic logic [6:0] hint_character(input logic [4:0] index);
        case (index)
            5'd0:  hint_character = 7'h50;
            5'd1:  hint_character = 7'h52;
            5'd2:  hint_character = 7'h45;
            5'd3:  hint_character = 7'h53;
            5'd4:  hint_character = 7'h53;
            5'd6:  hint_character = 7'h46;
            5'd7:  hint_character = 7'h31;
            5'd8:  hint_character = 7'h32;
            5'd10: hint_character = 7'h46;
            5'd11: hint_character = 7'h4F;
            5'd12: hint_character = 7'h52;
            5'd14: hint_character = 7'h4D;
            5'd15: hint_character = 7'h45;
            5'd16: hint_character = 7'h4E;
            5'd17: hint_character = 7'h55;
            default: hint_character = 7'h20;
        endcase
    endfunction

    assign scan_x = x;
    assign scan_y = y;
    assign vblank_start = resetn && x == 0 && y == V_ACTIVE;
    assign unused_enable = enable;

    always_ff @(posedge pixel_clk) begin
        if (!resetn) begin
            x <= 12'd0;
            y <= 12'd0;
        end else if (x == H_TOTAL - 1) begin
            x <= 12'd0;
            y <= (y == V_TOTAL - 1) ? 12'd0 : y + 12'd1;
        end else begin
            x <= x + 12'd1;
        end
    end

    always_comb begin
        active = x < H_ACTIVE && y < V_ACTIVE;
        // machine_enable is the bridge's vblank-latched mode decision. The raw
        // diagnostic request must not override it here or DVI can change source
        // in the middle of an active scan.
        use_machine = machine_enable;
        main_dx = x - MAIN_X0;
        main_dy = y - MAIN_Y0;
        one_dx = x - ONE_X0;
        one_dy = y - ONE_Y0;
        hint_dx = x - HINT_X0;
        hint_dy = y - HINT_Y0;
        main_index = main_dx[10:7];
        main_column = main_dx[6:4];
        main_row = main_dy[6:4];
        one_index = one_dx[7:6];
        one_column = one_dx[5:3];
        one_row = one_dy[5:3];
        hint_index = hint_dx[9:5];
        hint_column = hint_dx[4:2];
        hint_row = hint_dy[4:2];
        main_glyph = standby_glyph(main_character(main_index));
        one_glyph = standby_glyph(one_character(one_index));
        hint_glyph = standby_glyph(hint_character(hint_index));
        main_pixel = x >= MAIN_X0 && x < MAIN_X0 + MAIN_WIDTH &&
                     y >= MAIN_Y0 && y < MAIN_Y0 + MAIN_HEIGHT &&
                     main_column < 5 &&
                     main_glyph[34 - (main_row * 5 + main_column)];
        one_pixel = x >= ONE_X0 && x < ONE_X0 + ONE_WIDTH &&
                    y >= ONE_Y0 && y < ONE_Y0 + ONE_HEIGHT &&
                    one_column < 5 &&
                    one_glyph[34 - (one_row * 5 + one_column)];
        hint_pixel = x >= HINT_X0 && x < HINT_X0 + HINT_WIDTH &&
                     y >= HINT_Y0 && y < HINT_Y0 + HINT_HEIGHT &&
                     hint_column < 5 &&
                     hint_glyph[34 - (hint_row * 5 + hint_column)];

        // Branded standby instead of failure-like color bars. Power-of-two
        // cell sizes keep this path shallow enough for the 148.5 MHz output.
        diagnostic_rgb565 = 16'h0842;
        if (x < H_ACTIVE && y < V_ACTIVE &&
            (x[6:0] == 7'd0 || y[6:0] == 7'd0))
            diagnostic_rgb565 = 16'h1063;
        if (x >= 12'd256 && x < 12'd1664 &&
            y >= 12'd300 && y < 12'd780)
            diagnostic_rgb565 = 16'h1084;
        if (x >= 12'd384 && x < 12'd960 &&
            y >= 12'd352 && y < 12'd360)
            diagnostic_rgb565 = 16'h2DFF;
        if (x >= 12'd960 && x < 12'd1536 &&
            y >= 12'd352 && y < 12'd360)
            diagnostic_rgb565 = 16'hF15F;
        if (main_pixel)
            diagnostic_rgb565 = main_index < 4'd5 ? 16'h2DFF : 16'hF15F;
        if (one_pixel) diagnostic_rgb565 = 16'hFFFF;
        if (hint_pixel) diagnostic_rgb565 = 16'hBDF7;
        selected_rgb565 = ui_opaque ? ui_rgb565 :
                          (use_machine ? (border_opaque ? border_rgb565 : machine_rgb565) :
                                         diagnostic_rgb565_q);
    end

    // The standby glyph renderer contains coordinate compares, character
    // lookup, and a variable glyph-bit selection. Register its result before
    // the final DVI stage so that decorative diagnostics cannot become the
    // critical 148.5 MHz output path. Live machine and overlay pixels retain
    // their existing single-stage latency.
    always_ff @(posedge pixel_clk) begin
        if (!resetn)
            diagnostic_rgb565_q <= 16'h0842;
        else
            diagnostic_rgb565_q <= diagnostic_rgb565;
    end

    always_ff @(posedge pixel_clk) begin
        if (!resetn) begin
            dvi_red   <= 5'd0;
            dvi_grn   <= 6'd0;
            dvi_blu   <= 5'd0;
            dvi_de    <= 1'b0;
            dvi_hsync <= 1'b0;
            dvi_vsync <= 1'b0;
        end else begin
            dvi_red   <= active ? selected_rgb565[15:11] : 5'd0;
            dvi_grn   <= active ? selected_rgb565[10:5] : 6'd0;
            dvi_blu   <= active ? selected_rgb565[4:0] : 5'd0;
            dvi_de    <= active;
            dvi_hsync <= x >= H_ACTIVE + H_FRONT &&
                         x < H_ACTIVE + H_FRONT + H_SYNC;
            dvi_vsync <= y >= V_ACTIVE + V_FRONT &&
                         y < V_ACTIVE + V_FRONT + V_SYNC;
        end
    end

    ODDR #(
        .DDR_CLK_EDGE("OPPOSITE_EDGE"),
        .INIT(1'b0),
        .SRTYPE("SYNC")
    ) dvi_clk_oddr (
        .Q(dvi_clk), .C(pixel_clk), .CE(1'b1),
        .D1(1'b0), .D2(1'b1), .R(1'b0), .S(1'b0)
    );

endmodule
