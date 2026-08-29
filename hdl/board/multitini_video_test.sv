// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// 1920x1080p60 RGB565 diagnostic pattern for a 148.5 MHz pixel clock.
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
    output logic [4:0] dvi_red,
    output logic [5:0] dvi_grn,
    output logic [4:0] dvi_blu,
    output logic       dvi_clk,
    output logic       dvi_de,
    output logic       dvi_hsync,
    output logic       dvi_vsync
);
    logic [11:0] x;
    logic [10:0] y;
    logic [15:0] rgb565;
    logic active;

    always_ff @(posedge pixel_clk) begin
        if (!resetn) begin
            x <= 12'd0;
            y <= 11'd0;
        end else if (x == H_TOTAL - 1) begin
            x <= 12'd0;
            y <= (y == V_TOTAL - 1) ? 11'd0 : y + 11'd1;
        end else begin
            x <= x + 12'd1;
        end
    end

    always_comb begin
        active = enable && x < H_ACTIVE && y < V_ACTIVE;
        unique case (x[10:8])
            3'd0: rgb565 = 16'hFFFF;
            3'd1: rgb565 = 16'hFFE0;
            3'd2: rgb565 = 16'h07FF;
            3'd3: rgb565 = 16'h07E0;
            3'd4: rgb565 = 16'hF81F;
            3'd5: rgb565 = 16'hF800;
            3'd6: rgb565 = 16'h001F;
            default: rgb565 = 16'h8410;
        endcase
        if (x == 0 || y == 0 || x == H_ACTIVE - 1 || y == V_ACTIVE - 1)
            rgb565 = 16'hFFFF;
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
            dvi_red   <= active ? rgb565[15:11] : 5'd0;
            dvi_grn   <= active ? rgb565[10:5] : 6'd0;
            dvi_blu   <= active ? rgb565[4:0] : 5'd0;
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
