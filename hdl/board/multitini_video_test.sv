// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// 1920x1080p60 timing, diagnostic fallback, and physical DVI output stage for
// a 148.5 MHz pixel clock. The machine framebuffer is selected only when its
// bridge has latched machine_enable at vblank; otherwise diagnostics remain on.
module multitini_video_test #(
    parameter bit DYNAMIC_DISPLAY=1'b0,
    parameter int H_ACTIVE = 1920,
    parameter int H_FRONT  = 88,
    parameter int H_SYNC   = 44,
    parameter int H_TOTAL  = 2200,
    parameter int V_ACTIVE = 1080,
    parameter int V_FRONT  = 4,
    parameter int V_SYNC   = 5,
    parameter int V_TOTAL  = 1125,
    parameter bit ENABLE_EFFECTS = 1'b0,
    parameter integer SOURCE_SCALE_NUM = 4,
    parameter integer SOURCE_SCALE_DEN = 1,
    parameter bit SHARED_PIXEL_RESET = 1'b0,
    parameter bit SHARED_PIXEL_LIVENESS = 1'b0
) (
    input wire [11:0] display_h_active,display_h_front,display_h_sync,display_h_total,
    input wire [11:0] display_v_active,display_v_front,display_v_sync,display_v_total,
    input wire display_h_positive,display_v_positive,
    input wire [3:0] display_scale_num,display_scale_den,
    input  logic       pixel_clk,
    input  logic       resetn,
    input  logic       enable,
    input  logic       machine_enable,
    input  wire control_clk, control_resetn, shared_pixel_resetn, shared_pixel_running,
    input  wire effect_write,
    input  wire [9:0] effect_address,
    input  wire [31:0] effect_data,
    input  wire effect_commit,
    input  wire [31:0] effect_commit_sequence,
    output wire effect_busy, effect_active_valid, effect_rejected,
    output wire [31:0] effect_active_sequence,
    input wire [11:0] viewport_x, viewport_y, viewport_width, viewport_height,
    input  logic [15:0] machine_rgb565,
    input wire [15:0] machine_neighbor_rgb565,
    input wire [7:0] machine_neighbor_weight,
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
    wire compact=DYNAMIC_DISPLAY && display_h_active<1920;
    wire [11:0] brand_x=compact ? {x[10:0],1'b0} : x;
    wire [11:0] brand_y=compact ? {y[10:0],1'b0} : y;
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

    wire [11:0] h_active=DYNAMIC_DISPLAY ? display_h_active : H_ACTIVE;
    wire [11:0] h_front=DYNAMIC_DISPLAY ? display_h_front : H_FRONT;
    wire [11:0] h_sync=DYNAMIC_DISPLAY ? display_h_sync : H_SYNC;
    wire [11:0] h_total=DYNAMIC_DISPLAY ? display_h_total : H_TOTAL;
    wire [11:0] v_active=DYNAMIC_DISPLAY ? display_v_active : V_ACTIVE;
    wire [11:0] v_front=DYNAMIC_DISPLAY ? display_v_front : V_FRONT;
    wire [11:0] v_sync=DYNAMIC_DISPLAY ? display_v_sync : V_SYNC;
    wire [11:0] v_total=DYNAMIC_DISPLAY ? display_v_total : V_TOTAL;
    wire h_positive=!DYNAMIC_DISPLAY || display_h_positive;
    wire v_positive=!DYNAMIC_DISPLAY || display_v_positive;
    assign scan_x = x;
    assign scan_y = y;
    assign vblank_start = resetn && x == 0 && y == v_active;
    assign unused_enable = enable;

    always_ff @(posedge pixel_clk) begin
        if (!resetn) begin
            x <= 12'd0;
            y <= 12'd0;
        end else if (x == h_total - 1) begin
            x <= 12'd0;
            y <= (y == v_total - 1) ? 12'd0 : y + 12'd1;
        end else begin
            x <= x + 12'd1;
        end
    end

    always_comb begin
        active = x < h_active && y < v_active;
        // machine_enable is the bridge's vblank-latched mode decision. The raw
        // diagnostic request must not override it here or DVI can change source
        // in the middle of an active scan.
        use_machine = machine_enable;
        main_dx = brand_x - MAIN_X0;
        main_dy = brand_y - MAIN_Y0;
        one_dx = brand_x - ONE_X0;
        one_dy = brand_y - ONE_Y0;
        hint_dx = brand_x - HINT_X0;
        hint_dy = brand_y - HINT_Y0;
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
        main_pixel = brand_x >= MAIN_X0 && brand_x < MAIN_X0 + MAIN_WIDTH &&
                     brand_y >= MAIN_Y0 && brand_y < MAIN_Y0 + MAIN_HEIGHT &&
                     main_column < 5 &&
                     main_glyph[34 - (main_row * 5 + main_column)];
        one_pixel = brand_x >= ONE_X0 && brand_x < ONE_X0 + ONE_WIDTH &&
                    brand_y >= ONE_Y0 && brand_y < ONE_Y0 + ONE_HEIGHT &&
                    one_column < 5 &&
                    one_glyph[34 - (one_row * 5 + one_column)];
        hint_pixel = brand_x >= HINT_X0 && brand_x < HINT_X0 + HINT_WIDTH &&
                     brand_y >= HINT_Y0 && brand_y < HINT_Y0 + HINT_HEIGHT &&
                     hint_column < 5 &&
                     hint_glyph[34 - (hint_row * 5 + hint_column)];

        // Branded standby instead of failure-like color bars. Power-of-two
        // cell sizes keep this path shallow enough for the 148.5 MHz output.
        diagnostic_rgb565 = 16'h0842;
        if (x < h_active && y < v_active &&
            (brand_x[6:0] == 7'd0 || brand_y[6:0] == 7'd0))
            diagnostic_rgb565 = 16'h1063;
        if (brand_x >= 12'd256 && brand_x < 12'd1664 &&
            brand_y >= 12'd300 && brand_y < 12'd780)
            diagnostic_rgb565 = 16'h1084;
        if (brand_x >= 12'd384 && brand_x < 12'd960 &&
            brand_y >= 12'd352 && brand_y < 12'd360)
            diagnostic_rgb565 = 16'h2DFF;
        if (brand_x >= 12'd960 && brand_x < 12'd1536 &&
            brand_y >= 12'd352 && brand_y < 12'd360)
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

    generate if (ENABLE_EFFECTS) begin : g_effects
        wire [15:0] processed_rgb;
        wire processed_de, processed_hsync, processed_vsync;
        wire in_viewport = scan_x >= viewport_x && scan_y >= viewport_y &&
            {1'b0,scan_x} < {1'b0,viewport_x}+{1'b0,viewport_width} &&
            {1'b0,scan_y} < {1'b0,viewport_y}+{1'b0,viewport_height};
        multitini_video_effects #(.DYNAMIC_DISPLAY(DYNAMIC_DISPLAY),.SOURCE_SCALE_NUM(SOURCE_SCALE_NUM),
            .SOURCE_SCALE_DEN(SOURCE_SCALE_DEN),.SHARED_PIXEL_RESET(SHARED_PIXEL_RESET),
            .SHARED_PIXEL_LIVENESS(SHARED_PIXEL_LIVENESS)) effects_i (
            .display_scale_num(display_scale_num),.display_scale_den(display_scale_den),
            .control_clk(control_clk),.control_resetn(control_resetn),
            .effect_write(effect_write),.effect_address(effect_address),.effect_data(effect_data),
            .effect_commit(effect_commit),.effect_commit_sequence(effect_commit_sequence),
            .effect_busy(effect_busy),.effect_active_valid(effect_active_valid),
            .effect_rejected(effect_rejected),.effect_active_sequence(effect_active_sequence),
            .pixel_clk(pixel_clk),.pixel_resetn(resetn),.shared_pixel_resetn(shared_pixel_resetn),
            .shared_pixel_running(shared_pixel_running),.vblank_start(vblank_start),
            .pixel_rgb565(selected_rgb565),
            .pixel_neighbor_rgb565(machine_neighbor_rgb565),.pixel_neighbor_weight(machine_neighbor_weight),
            .pixel_effect_enable(active && use_machine && !ui_opaque && !border_opaque && in_viewport),
            .pixel_x(scan_x-viewport_x),.pixel_y(scan_y-viewport_y),
            .pixel_de(active),
            .pixel_hsync((x >= h_active+h_front && x < h_active+h_front+h_sync) ^ !h_positive),
            .pixel_vsync((y >= v_active+v_front && y < v_active+v_front+v_sync) ^ !v_positive),
            .output_rgb565(processed_rgb),.output_de(processed_de),
            .output_hsync(processed_hsync),.output_vsync(processed_vsync)
        );
        // Preserve the physical IOB stage and forwarded DVI-clock relation.
        // Every selected pixel, including unmodified text/borders/standby,
        // travels through the same nine stages as DE and both sync signals.
        always_ff @(posedge pixel_clk) begin
            if (!resetn) begin
                dvi_red<=0;dvi_grn<=0;dvi_blu<=0;dvi_de<=0;dvi_hsync<=0;dvi_vsync<=0;
            end else begin
                dvi_red<=processed_de ? processed_rgb[15:11] : 0;
                dvi_grn<=processed_de ? processed_rgb[10:5] : 0;
                dvi_blu<=processed_de ? processed_rgb[4:0] : 0;
                dvi_de<=processed_de;dvi_hsync<=processed_hsync;dvi_vsync<=processed_vsync;
            end
        end
    end else begin : g_direct
        assign effect_busy=1'b0, effect_active_valid=1'b0, effect_rejected=1'b0;
        assign effect_active_sequence=32'd0;
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
            dvi_hsync <= (x >= h_active + h_front &&
                         x < h_active + h_front + h_sync) ^ !h_positive;
            dvi_vsync <= (y >= v_active + v_front &&
                         y < v_active + v_front + v_sync) ^ !v_positive;
        end
    end

    end endgenerate

    ODDR #(
        .DDR_CLK_EDGE("OPPOSITE_EDGE"),
        .INIT(1'b0),
        .SRTYPE("SYNC")
    ) dvi_clk_oddr (
        .Q(dvi_clk), .C(pixel_clk), .CE(1'b1),
        .D1(1'b0), .D2(1'b1), .R(1'b0), .S(1'b0)
    );

endmodule
