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
    logic [11:0] x;
    logic [11:0] y;
    logic [15:0] diagnostic_rgb565;
    logic [15:0] selected_rgb565;
    logic active;
    logic use_machine;
    logic unused_enable;

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
        unique case (x[10:8])
            3'd0: diagnostic_rgb565 = 16'hFFFF;
            3'd1: diagnostic_rgb565 = 16'hFFE0;
            3'd2: diagnostic_rgb565 = 16'h07FF;
            3'd3: diagnostic_rgb565 = 16'h07E0;
            3'd4: diagnostic_rgb565 = 16'hF81F;
            3'd5: diagnostic_rgb565 = 16'hF800;
            3'd6: diagnostic_rgb565 = 16'h001F;
            default: diagnostic_rgb565 = 16'h8410;
        endcase
        if (x == 0 || y == 0 || x == H_ACTIVE - 1 || y == V_ACTIVE - 1)
            diagnostic_rgb565 = 16'hFFFF;
        selected_rgb565 = use_machine ? machine_rgb565 : diagnostic_rgb565;
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
