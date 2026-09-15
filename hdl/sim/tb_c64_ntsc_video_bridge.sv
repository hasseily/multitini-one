// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Exercises the production 6567R8 geometry rather than a reduced model. The
// source is slightly slower than 60 Hz, so the output-side checks also prove
// that an empty 60 Hz interval repeats the owned frame without releasing it.
module tb_c64_ntsc_video_bridge;
    localparam int NTSC_WIDTH = 404;
    localparam int NTSC_HEIGHT = 250;
    localparam int NTSC_X_OFFSET = 152;
    localparam int NTSC_Y_OFFSET = 40;
    localparam int OUTPUT_SCALE = 4;
    localparam int FRAME_PIXELS = NTSC_WIDTH * NTSC_HEIGHT;

    logic source_clk = 1'b0;
    logic source_resetn = 1'b0;
    logic source_ready = 1'b0;
    logic [7:0] source_r = 8'd0;
    logic [7:0] source_g = 8'd0;
    logic [7:0] source_b = 8'd0;
    logic source_de = 1'b0;
    logic source_vsync = 1'b0;
    logic source_valid = 1'b0;

    logic pixel_clk = 1'b0;
    logic pixel_resetn = 1'b0;
    logic diagnostic_request = 1'b1;
    logic [11:0] scan_x = 12'd0;
    logic [11:0] scan_y = 12'd0;
    logic vblank_start = 1'b0;
    logic machine_active;
    logic [15:0] machine_rgb565;
    integer failure_count = 0;

    always #5 source_clk = !source_clk;
    always #3 pixel_clk = !pixel_clk;

    multitini_c64_video_bridge #(.NTSC_MODE(1'b1)) dut (
        .source_clk(source_clk), .source_resetn(source_resetn),
        .source_ready(source_ready),
        .source_r(source_r), .source_g(source_g), .source_b(source_b),
        .source_de(source_de), .source_vsync(source_vsync),
        .source_valid(source_valid),
        .pixel_clk(pixel_clk), .pixel_resetn(pixel_resetn),
        .diagnostic_request(diagnostic_request),
        .scan_x(scan_x), .scan_y(scan_y), .vblank_start(vblank_start),
        .machine_active(machine_active), .machine_rgb565(machine_rgb565)
    );

    function automatic [23:0] palette_rgb888(input integer index);
        begin
            case (index & 15)
                0: palette_rgb888 = 24'h000000;
                1: palette_rgb888 = 24'hFFFFFF;
                2: palette_rgb888 = 24'h813338;
                3: palette_rgb888 = 24'h75CEC8;
                4: palette_rgb888 = 24'h8E3C97;
                5: palette_rgb888 = 24'h56AC4D;
                6: palette_rgb888 = 24'h2E2C9B;
                7: palette_rgb888 = 24'hEDF171;
                8: palette_rgb888 = 24'h8E5029;
                9: palette_rgb888 = 24'h553800;
                10: palette_rgb888 = 24'hC46C71;
                11: palette_rgb888 = 24'h4A4A4A;
                12: palette_rgb888 = 24'h7B7B7B;
                13: palette_rgb888 = 24'hA9FF9F;
                14: palette_rgb888 = 24'h706DEB;
                default: palette_rgb888 = 24'hB2B2B2;
            endcase
        end
    endfunction

    function automatic [15:0] palette_rgb565(input integer index);
        begin
            case (index & 15)
                0: palette_rgb565 = 16'h0000;
                1: palette_rgb565 = 16'hFFFF;
                2: palette_rgb565 = 16'h8187;
                3: palette_rgb565 = 16'h7679;
                4: palette_rgb565 = 16'h89F2;
                5: palette_rgb565 = 16'h5569;
                6: palette_rgb565 = 16'h2973;
                7: palette_rgb565 = 16'hEF8E;
                8: palette_rgb565 = 16'h8A85;
                9: palette_rgb565 = 16'h51C0;
                10: palette_rgb565 = 16'hC36E;
                11: palette_rgb565 = 16'h4A49;
                12: palette_rgb565 = 16'h7BCF;
                13: palette_rgb565 = 16'hAFF3;
                14: palette_rgb565 = 16'h737D;
                default: palette_rgb565 = 16'hB596;
            endcase
        end
    endfunction

    function automatic integer pattern_index(
        input integer x,
        input integer y,
        input integer variant
    );
        pattern_index = (x + y + variant) & 15;
    endfunction

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            failure_count = failure_count + 1;
        end
    endtask

    task automatic send_source_tuple(
        input logic [23:0] rgb888,
        input logic de,
        input logic vsync
    );
        @(negedge source_clk);
        source_r = rgb888[23:16];
        source_g = rgb888[15:8];
        source_b = rgb888[7:0];
        source_de = de;
        source_vsync = vsync;
        source_valid = 1'b1;
        @(posedge source_clk);
        #1;
    endtask

    task automatic pulse_source_vsync;
        begin
            send_source_tuple(24'h000000, 1'b0, 1'b1);
            send_source_tuple(24'h000000, 1'b0, 1'b0);
            source_valid = 1'b0;
        end
    endtask

    task automatic send_frame(input integer width, input integer variant);
        integer x;
        integer y;
        logic [23:0] pixel_rgb;
        begin
            for (y = 0; y < NTSC_HEIGHT; y = y + 1) begin
                for (x = 0; x < width; x = x + 1) begin
                    pixel_rgb = palette_rgb888(pattern_index(x, y, variant));
                    send_source_tuple(pixel_rgb, 1'b1, 1'b0);
                end
                send_source_tuple(24'h000000, 1'b0, 1'b0);
            end
            pulse_source_vsync();
        end
    endtask

    task automatic pulse_output_vblank;
        begin
            @(negedge pixel_clk);
            vblank_start = 1'b1;
            @(posedge pixel_clk);
            #1;
            @(negedge pixel_clk);
            vblank_start = 1'b0;
            repeat (4) @(posedge source_clk);
        end
    endtask

    task automatic wait_session_run;
        integer timeout;
        begin
            timeout = 0;
            while ((dut.source_session_state != 2'd2 ||
                    dut.pixel_session_state != 2'd2) && timeout < 300) begin
                @(posedge pixel_clk);
                timeout = timeout + 1;
            end
            #1;
            check_value(dut.source_session_state == 2'd2 &&
                        dut.pixel_session_state == 2'd2,
                        "NTSC capture session rearm");
        end
    endtask

    task automatic wait_published_pixel(input logic [2:0] expected);
        integer timeout;
        begin
            timeout = 0;
            while (dut.published_binary_pixel != expected && timeout < 100) begin
                @(posedge pixel_clk);
                timeout = timeout + 1;
            end
            #1;
            check_value(dut.published_binary_pixel == expected,
                        "NTSC published pointer crossing");
        end
    endtask

    task automatic prepare_pixel(
        input logic [11:0] x,
        input logic [11:0] y,
        input logic [15:0] expected,
        input string message
    );
        begin
            @(negedge pixel_clk);
            scan_x = x;
            scan_y = y;
            @(posedge pixel_clk);
            #1;
            check_value(machine_rgb565 == expected, message);
        end
    endtask

    initial begin
        logic [2:0] held_acquire;
        logic [2:0] held_release;

        fork
            begin
                #5000000;
                $display("FAIL: production NTSC bridge timeout");
                $fatal(1);
            end
        join_none

        check_value(dut.SOURCE_WIDTH == NTSC_WIDTH,
                    "NTSC default capture width is 404");
        check_value(dut.SOURCE_HEIGHT == NTSC_HEIGHT,
                    "NTSC default capture height is 250");
        check_value(dut.OUTPUT_X_OFFSET == NTSC_X_OFFSET &&
                    dut.OUTPUT_Y_OFFSET == NTSC_Y_OFFSET,
                    "NTSC 4x scan is centered in 1080p");
        check_value(dut.FRAME_PIXELS == FRAME_PIXELS,
                    "NTSC bank contains exactly one active raster");

        repeat (4) @(negedge source_clk);
        check_value(dut.viewport_x == NTSC_X_OFFSET &&
                    dut.viewport_y == NTSC_Y_OFFSET &&
                    dut.viewport_width == NTSC_WIDTH * OUTPUT_SCALE &&
                    dut.viewport_height == NTSC_HEIGHT * OUTPUT_SCALE,
                    "border viewport matches the fixed NTSC scan");
        source_resetn = 1'b1;
        source_ready = 1'b1;
        pixel_resetn = 1'b1;
        wait_session_run();

        pulse_source_vsync();
        check_value(dut.source_frame_armed,
                    "NTSC capture starts only on VSYNC");
        send_frame(NTSC_WIDTH - 1, 1);
        check_value(dut.published_binary_source == 3'd0,
                    "403-pixel NTSC lines reject the whole frame");

        send_frame(NTSC_WIDTH, 2);
        check_value(dut.published_binary_source == 3'd1,
                    "complete 404x250 NTSC frame publishes");
        wait_published_pixel(3'd1);
        check_value(dut.frame_bank_0.memory[0] ==
                    pattern_index(0, 0, 2),
                    "NTSC first active pixel stored");
        check_value(dut.frame_bank_0.memory[FRAME_PIXELS - 1] ==
                    pattern_index(NTSC_WIDTH - 1, NTSC_HEIGHT - 1, 2),
                    "NTSC last active pixel stored");

        diagnostic_request = 1'b0;
        repeat (5) @(posedge pixel_clk);
        check_value(!machine_active, "NTSC waits for output vblank");
        pulse_output_vblank();
        check_value(machine_active && dut.display_bank == 2'd0,
                    "NTSC frame takes ownership at vblank");

        prepare_pixel(12'd0, 12'd0, 16'h0000,
                      "NTSC scan address resets at output frame start");
        prepare_pixel(NTSC_X_OFFSET, NTSC_Y_OFFSET - 1, 16'h0000,
                      "NTSC top matte is black");
        prepare_pixel(12'd0, NTSC_Y_OFFSET, 16'h0000,
                      "NTSC first line starts from source row zero");
        prepare_pixel(NTSC_X_OFFSET - 2, NTSC_Y_OFFSET, 16'h0000,
                      "NTSC hidden RAM prefetch remains black");
        prepare_pixel(NTSC_X_OFFSET - 1, NTSC_Y_OFFSET, 16'h0000,
                      "NTSC hidden palette pipeline remains black");
        prepare_pixel(NTSC_X_OFFSET, NTSC_Y_OFFSET,
                      palette_rgb565(pattern_index(0, 0, 2)),
                      "NTSC centered scan begins with source pixel zero");
        prepare_pixel(NTSC_X_OFFSET + NTSC_WIDTH * OUTPUT_SCALE,
                      NTSC_Y_OFFSET, 16'h0000,
                      "NTSC right matte is black");
        prepare_pixel(NTSC_X_OFFSET,
                      NTSC_Y_OFFSET + NTSC_HEIGHT * OUTPUT_SCALE,
                      16'h0000, "NTSC bottom matte is black");

        held_acquire = dut.acquire_binary_pixel;
        held_release = dut.released_binary_pixel;
        pulse_output_vblank();
        check_value(dut.acquire_binary_pixel == held_acquire &&
                    dut.released_binary_pixel == held_release &&
                    dut.display_bank == 2'd0,
                    "60 Hz output repeats when no 59.8 Hz frame is ready");

        pulse_source_vsync();
        send_frame(NTSC_WIDTH, 3);
        wait_published_pixel(3'd2);
        pulse_output_vblank();
        check_value(dut.display_bank == 2'd1 &&
                    dut.acquire_binary_pixel == 3'd2 &&
                    dut.released_binary_pixel == 3'd1,
                    "next NTSC frame swaps in FIFO order");
        check_value(dut.frame_bank_1.memory[0] ==
                    pattern_index(0, 0, 3),
                    "next NTSC bank contains the new complete frame");

        diagnostic_request = 1'b1;
        repeat (5) @(posedge pixel_clk);
        check_value(machine_active,
                    "diagnostic request cannot cut an NTSC frame mid-scan");
        pulse_output_vblank();
        check_value(!machine_active,
                    "diagnostics take NTSC output at vblank");

        if (failure_count != 0)
            $fatal(1, "%0d production NTSC bridge checks failed",
                   failure_count);
        else begin
            $display("PASS: 404x250 NTSC capture, centered 4x scan, 59.8-to-60 Hz repeat, and vblank ownership");
            $finish;
        end
    end
endmodule
