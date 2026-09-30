// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_video_bridge;
    parameter bit DYNAMIC_DISPLAY=0;
    localparam int SOURCE_WIDTH = 8;
    localparam int SOURCE_HEIGHT = 4;
    localparam int OUTPUT_SCALE = 2;
    localparam int OUTPUT_X_OFFSET = 4;
    localparam int OUTPUT_Y_OFFSET = 2;
    localparam int OUTPUT_WIDTH = SOURCE_WIDTH * OUTPUT_SCALE;
    localparam int PATH_H_ACTIVE = OUTPUT_X_OFFSET + OUTPUT_WIDTH + 2;
    localparam int PATH_H_TOTAL = PATH_H_ACTIVE + 4;
    localparam int PATH_V_ACTIVE = OUTPUT_Y_OFFSET +
                                   SOURCE_HEIGHT * OUTPUT_SCALE + 2;
    localparam int PATH_V_TOTAL = PATH_V_ACTIVE + 3;

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
    logic [11:0] manual_scan_x = 12'd0;
    logic [11:0] manual_scan_y = 12'd0;
    logic manual_vblank_start = 1'b0;
    logic use_path_timing = 1'b0;
    logic path_resetn = 1'b0;
    logic [11:0] path_scan_x;
    logic [11:0] path_scan_y;
    logic path_vblank_start;
    logic [4:0] path_red;
    logic [5:0] path_green;
    logic [4:0] path_blue;
    logic path_dvi_clk;
    logic path_de;
    logic path_hsync;
    logic path_vsync;
    wire [11:0] scan_x = use_path_timing ? path_scan_x : manual_scan_x;
    wire [11:0] scan_y = use_path_timing ? path_scan_y : manual_scan_y;
    wire vblank_start = use_path_timing ? path_vblank_start :
                                             manual_vblank_start;
    logic machine_active;
    logic [15:0] machine_rgb565;

    logic bank_collision_seen = 1'b0;
    logic mode_changed_outside_vblank = 1'b0;
    logic [1:0] prior_display_bank = 2'd0;
    logic prior_machine_active = 1'b0;
    integer failure_count = 0;

    always #5 source_clk = !source_clk;
    always #3 pixel_clk = !pixel_clk;

    multitini_c64_video_bridge #(
        .SOURCE_WIDTH(SOURCE_WIDTH), .SOURCE_HEIGHT(SOURCE_HEIGHT),
        .DYNAMIC_DISPLAY(DYNAMIC_DISPLAY),
        .OUTPUT_SCALE(DYNAMIC_DISPLAY ? 4 : OUTPUT_SCALE), .OUTPUT_X_OFFSET(OUTPUT_X_OFFSET),
        .OUTPUT_Y_OFFSET(OUTPUT_Y_OFFSET)
    ) dut (
        .display_view_x(12'(OUTPUT_X_OFFSET)),.display_view_y(12'(OUTPUT_Y_OFFSET)),
        .display_view_width(12'(SOURCE_WIDTH*OUTPUT_SCALE)),.display_view_height(12'(SOURCE_HEIGHT*OUTPUT_SCALE)),
        .display_scale_num(4'(OUTPUT_SCALE)),.display_scale_den(4'd1),
        .source_clk(source_clk), .source_resetn(source_resetn),
        .source_ready(source_ready),
        .source_r(source_r), .source_g(source_g), .source_b(source_b),
        .source_de(source_de), .source_vsync(source_vsync),
        .source_valid(source_valid),
        .pixel_clk(pixel_clk), .pixel_resetn(pixel_resetn),
        .diagnostic_request(diagnostic_request),
        .scan_x(scan_x), .scan_y(scan_y),
        .vblank_start(vblank_start),
        .machine_active(machine_active),
        .machine_rgb565(machine_rgb565)
    );

    // Compose the bridge with the actual registered DVI timing stage. This
    // catches prefetch-window mistakes that are invisible when the BRAM output
    // is inspected only after its nonblocking update.
    multitini_video_test #(
        .H_ACTIVE(PATH_H_ACTIVE), .H_FRONT(1), .H_SYNC(1),
        .H_TOTAL(PATH_H_TOTAL), .V_ACTIVE(PATH_V_ACTIVE), .V_FRONT(1),
        .V_SYNC(1), .V_TOTAL(PATH_V_TOTAL)
    ) video_path (
        .pixel_clk(pixel_clk), .resetn(path_resetn),
        .enable(diagnostic_request), .machine_enable(machine_active),
        .border_opaque(1'b0), .border_rgb565(16'd0), .machine_rgb565(machine_rgb565),
        .ui_opaque(1'b0), .ui_rgb565(16'd0),
        .scan_x(path_scan_x), .scan_y(path_scan_y),
        .vblank_start(path_vblank_start),
        .dvi_red(path_red), .dvi_grn(path_green), .dvi_blu(path_blue),
        .dvi_clk(path_dvi_clk), .dvi_de(path_de),
        .dvi_hsync(path_hsync), .dvi_vsync(path_vsync)
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

    task automatic send_exact_frame(input integer variant);
        integer x;
        integer y;
        logic [23:0] pixel_rgb;
        begin
            for (y = 0; y < SOURCE_HEIGHT; y = y + 1) begin
                for (x = 0; x < SOURCE_WIDTH; x = x + 1) begin
                    pixel_rgb = palette_rgb888(pattern_index(x, y, variant));
                    send_source_tuple(pixel_rgb, 1'b1, 1'b0);
                end
                send_source_tuple(24'h000000, 1'b0, 1'b0);
            end
            pulse_source_vsync();
        end
    endtask

    task automatic send_short_line_frame(input integer variant);
        integer x;
        integer y;
        integer width;
        logic [23:0] pixel_rgb;
        begin
            for (y = 0; y < SOURCE_HEIGHT; y = y + 1) begin
                width = (y == 0) ? SOURCE_WIDTH - 1 : SOURCE_WIDTH;
                for (x = 0; x < width; x = x + 1) begin
                    pixel_rgb = palette_rgb888(pattern_index(x, y, variant));
                    send_source_tuple(pixel_rgb, 1'b1, 1'b0);
                end
                send_source_tuple(24'h000000, 1'b0, 1'b0);
            end
            pulse_source_vsync();
        end
    endtask

    task automatic send_unknown_palette_frame(input integer variant);
        integer x;
        integer y;
        logic [23:0] rgb;
        begin
            for (y = 0; y < SOURCE_HEIGHT; y = y + 1) begin
                for (x = 0; x < SOURCE_WIDTH; x = x + 1) begin
                    rgb = (x == 2 && y == 1) ? 24'h010203 :
                          palette_rgb888(pattern_index(x, y, variant));
                    send_source_tuple(rgb, 1'b1, 1'b0);
                end
                send_source_tuple(24'h000000, 1'b0, 1'b0);
            end
            pulse_source_vsync();
        end
    endtask

    task automatic pulse_output_vblank;
        begin
            @(negedge pixel_clk);
            manual_vblank_start = 1'b1;
            @(posedge pixel_clk);
            #1;
            @(negedge pixel_clk);
            manual_vblank_start = 1'b0;
            // Keep synthetic pointer increments slower than the destination
            // clock. Real 60 Hz vblank intervals exceed this by many orders
            // of magnitude; the spacing also satisfies XPM Gray CDC checks.
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
            if (dut.source_session_state != 2'd2 ||
                dut.pixel_session_state != 2'd2)
                $display("SESSION timeout source_state=%0d pixel_state=%0d grant_launch=%b grant_source=%b ack_launch=%b ack_pixel=%b published_source=%0d published_pixel=%0d release_pixel=%0d release_source=%0d ready=%b",
                         dut.source_session_state, dut.pixel_session_state,
                         dut.capture_grant_launch, dut.capture_grant_source,
                         dut.source_session_ack_launch,
                         dut.source_session_ack_pixel,
                         dut.published_binary_source,
                         dut.published_binary_pixel,
                         dut.released_binary_pixel,
                         dut.released_binary_source, source_ready);
            check_value(dut.source_session_state == 2'd2 &&
                        dut.pixel_session_state == 2'd2,
                        "reset-epoch session rearm timeout");
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
                        "published pointer crossing timeout");
        end
    endtask

    task automatic wait_released_source(input logic [2:0] expected);
        integer timeout;
        begin
            timeout = 0;
            while (dut.released_binary_source != expected && timeout < 100) begin
                @(posedge source_clk);
                timeout = timeout + 1;
            end
            #1;
            check_value(dut.released_binary_source == expected,
                        "release pointer crossing timeout");
        end
    endtask

    task automatic prepare_next_pixel(
        input logic [11:0] current_x,
        input logic [11:0] current_y,
        input logic [15:0] expected,
        input string message
    );
        begin
            @(negedge pixel_clk);
            manual_scan_x = current_x;
            manual_scan_y = current_y;
            @(posedge pixel_clk);
            #1;
            if (machine_rgb565 != expected)
                $display("PIXEL x=%0d y=%0d expected=%h actual=%h data=%h addr=%0d repeat=%0d prefetch=%b output=%b bank=%0d mem0=%h r0=%h re=%b",
                         current_x, current_y, expected, machine_rgb565,
                         dut.selected_bank_read_data, dut.read_address,
                         dut.horizontal_repeat_count, dut.prefetch_active,
                         dut.output_active, dut.display_bank,
                         dut.frame_bank_0.memory[0], dut.bank_read_data_0,
                         dut.pixel_read_enable);
            check_value(machine_rgb565 == expected, message);
        end
    endtask

    task automatic check_path_pixel(
        input logic [11:0] expected_x,
        input logic [11:0] expected_y,
        input logic [15:0] expected_rgb,
        input string message
    );
        begin
            while (!(path_scan_x == expected_x && path_scan_y == expected_y))
                @(negedge pixel_clk);
            @(posedge pixel_clk);
            #1;
            if (!(path_de && {path_red, path_green, path_blue} == expected_rgb))
                $display("DVI x=%0d y=%0d expected=%h actual=%h de=%b bridge=%h data=%h addr=%0d repeat=%0d",
                         expected_x, expected_y, expected_rgb,
                         {path_red, path_green, path_blue}, path_de,
                         machine_rgb565, dut.selected_bank_read_data,
                         dut.read_address, dut.horizontal_repeat_count);
            check_value(path_de && {path_red, path_green, path_blue} ==
                        expected_rgb, message);
        end
    endtask

    // A displayed bank remains owned even when the test is not actively
    // stepping scan coordinates through it.
    always @(posedge source_clk) begin
        #1;
        if (dut.source_memory_write_enable && dut.display_valid &&
            dut.write_bank == dut.display_bank)
            bank_collision_seen = 1'b1;
    end

    // Normal mode and bank changes are vblank-only. Pixel reset is the one
    // intentional fail-closed exception.
    always @(posedge pixel_clk) begin
        #1;
        if (pixel_resetn && !vblank_start &&
            (dut.display_bank != prior_display_bank ||
             machine_active != prior_machine_active))
            mode_changed_outside_vblank = 1'b1;
        prior_display_bank = dut.display_bank;
        prior_machine_active = machine_active;
    end

    initial begin
        integer index;
        logic [23:0] rgb;
        logic [2:0] held_acquire;
        logic [2:0] held_release;
        logic [2:0] expected_pointer;
        logic [1:0] next_bank;
        logic [15:0] expected_rgb;

        fork
            begin
                #200000;
                $display("FAIL: live C64 bridge timeout");
                $fatal(1);
            end
        join_none

        // Prove all fixed upstream palette values round-trip exactly.
        for (index = 0; index < 16; index = index + 1) begin
            rgb = palette_rgb888(index);
            check_value(dut.encode_palette(rgb[23:16], rgb[15:8], rgb[7:0]) ==
                        {1'b1, index[3:0]}, "palette index encoder");
            check_value(dut.palette_rgb565(index[3:0]) ==
                        palette_rgb565(index), "palette RGB565 expansion");
        end
        check_value(dut.encode_palette(8'h01, 8'h02, 8'h03) == 5'b0,
                    "unknown palette tuple rejected");

        repeat (4) @(negedge source_clk);
        check_value(dut.viewport_x == OUTPUT_X_OFFSET &&
                    dut.viewport_y == OUTPUT_Y_OFFSET &&
                    dut.viewport_width == SOURCE_WIDTH * OUTPUT_SCALE &&
                    dut.viewport_height == SOURCE_HEIGHT * OUTPUT_SCALE,
                    "border viewport matches the fixed C64 scan");
        source_resetn = 1'b1;
        source_ready = 1'b1;
        pixel_resetn = 1'b1;
        wait_session_run();

        // First VSYNC arms capture. The following exact frame is published,
        // but diagnostics retain physical ownership until output vblank.
        pulse_source_vsync();
        check_value(dut.source_frame_armed, "capture arms at source boundary");
        send_exact_frame(1);
        check_value(dut.published_binary_source == 3'd1,
                    "first exact frame published");
        wait_published_pixel(3'd1);
        check_value(!machine_active, "complete frame waits behind diagnostics");

        diagnostic_request = 1'b0;
        repeat (5) @(posedge pixel_clk);
        check_value(!machine_active, "machine request waits for vblank");
        pulse_output_vblank();
        check_value(machine_active && dut.display_bank == 2'd0,
                    "first frame acquired at vblank");

        // Default-style integer scaling and two-stage prefetch alignment. Both
        // hidden pipeline coordinates must remain black border pixels.
        prepare_next_pixel(12'd0, 12'd0, 16'h0000,
                           "left border remains black");
        prepare_next_pixel(OUTPUT_X_OFFSET, OUTPUT_Y_OFFSET - 1, 16'h0000,
                           "top border remains black");
        prepare_next_pixel(12'd0, OUTPUT_Y_OFFSET, 16'h0000,
                           "first centered row starts at frame origin");
        prepare_next_pixel(OUTPUT_X_OFFSET - 2, OUTPUT_Y_OFFSET, 16'h0000,
                           "RAM prefetch coordinate remains black");
        prepare_next_pixel(OUTPUT_X_OFFSET - 1, OUTPUT_Y_OFFSET, 16'h0000,
                           "index-pipeline coordinate remains black");
        prepare_next_pixel(OUTPUT_X_OFFSET, OUTPUT_Y_OFFSET,
                           palette_rgb565(pattern_index(0, 0, 1)),
                           "horizontal source pixel repeated");
        prepare_next_pixel(OUTPUT_X_OFFSET + 1, OUTPUT_Y_OFFSET,
                           palette_rgb565(pattern_index(1, 0, 1)),
                           "next source pixel selected");
        prepare_next_pixel(12'd0, OUTPUT_Y_OFFSET + OUTPUT_SCALE, 16'h0000,
                           "next source-row base advances");
        prepare_next_pixel(OUTPUT_X_OFFSET - 2,
                           OUTPUT_Y_OFFSET + OUTPUT_SCALE, 16'h0000,
                           "next row RAM prefetch remains black");
        prepare_next_pixel(OUTPUT_X_OFFSET - 1,
                           OUTPUT_Y_OFFSET + OUTPUT_SCALE, 16'h0000,
                           "next row index pipeline remains black");

        // Raw diagnostic changes cannot alter physical mode mid-active.
        diagnostic_request = 1'b1;
        repeat (7) @(posedge pixel_clk);
        check_value(machine_active, "diagnostic assertion waits for vblank");
        pulse_output_vblank();
        check_value(!machine_active, "diagnostics take over at vblank");
        diagnostic_request = 1'b0;
        repeat (7) @(posedge pixel_clk);
        check_value(!machine_active, "machine re-request waits for vblank");
        pulse_output_vblank();
        check_value(machine_active, "machine returns at vblank");

        // No queued frame repeats the current display without releasing it.
        held_acquire = dut.acquire_binary_pixel;
        held_release = dut.released_binary_pixel;
        pulse_output_vblank();
        check_value(dut.display_bank == 2'd0 &&
                    dut.acquire_binary_pixel == held_acquire &&
                    dut.released_binary_pixel == held_release,
                    "empty output interval repeats complete frame");

        // Exact width, height, and palette membership are all publication gates.
        send_short_line_frame(2);
        check_value(dut.published_binary_source == 3'd1,
                    "short source line rejected");
        send_unknown_palette_frame(2);
        check_value(dut.published_binary_source == 3'd1,
                    "unknown source color rejects whole frame");

        // Fill all four owned banks while the output consumer is stalled.
        send_exact_frame(2);
        send_exact_frame(3);
        send_exact_frame(4);
        check_value(dut.published_binary_source == 3'd4 &&
                    dut.source_queue_full && !dut.source_frame_armed,
                    "four-bank ring reaches full without overwrite");
        send_exact_frame(5);
        check_value(dut.published_binary_source == 3'd4,
                    "full ring drops one whole source frame");

        // One output swap releases bank zero. Capture resumes only after the
        // next source boundary and safely wraps the producer back to bank zero.
        pulse_output_vblank();
        check_value(dut.display_bank == 2'd1,
                    "oldest queued frame selected in FIFO order");
        wait_released_source(3'd1);
        pulse_source_vsync();
        check_value(dut.source_frame_armed,
                    "released bank is reserved at a later source boundary");
        send_exact_frame(6);
        check_value(dut.published_binary_source == 3'd5,
                    "producer wraps into released bank zero");

        // Drain in FIFO order; the wrapped frame becomes visible only after
        // banks two and three have each owned a complete output interval.
        wait_published_pixel(3'd5);
        pulse_output_vblank();
        check_value(dut.display_bank == 2'd2, "second queued bank displayed");
        pulse_output_vblank();
        check_value(dut.display_bank == 2'd3, "third queued bank displayed");
        pulse_output_vblank();
        check_value(dut.display_bank == 2'd0, "wrapped bank displayed");
        prepare_next_pixel(12'd0, 12'd0, 16'h0000,
                           "wrapped frame address resets");
        prepare_next_pixel(OUTPUT_X_OFFSET - 2, OUTPUT_Y_OFFSET, 16'h0000,
                           "wrapped-frame RAM prefetch remains hidden");
        prepare_next_pixel(OUTPUT_X_OFFSET - 1, OUTPUT_Y_OFFSET, 16'h0000,
                           "wrapped-frame index pipeline remains hidden");
        prepare_next_pixel(OUTPUT_X_OFFSET, OUTPUT_Y_OFFSET,
                           palette_rgb565(pattern_index(0, 0, 6)),
                           "wrapped bank contains only its complete frame");

        // Run well beyond the three-bit pointer rollover. Each iteration keeps
        // only one new frame queued, acquires it in FIFO order, checks its
        // contents, and releases the preceding displayed bank. Starting at
        // five makes sixteen iterations cross 7->0 twice.
        expected_pointer = 3'd5;
        for (index = 0; index < 16; index = index + 1) begin
            pulse_source_vsync();
            check_value(dut.source_frame_armed,
                        "steady-state capture arms at source boundary");
            next_bank = dut.acquire_binary_pixel[1:0];
            send_exact_frame(9 + index);
            expected_pointer = expected_pointer + 1'b1;
            check_value(dut.published_binary_source == expected_pointer,
                        "producer pointer advances through rollover");
            wait_published_pixel(expected_pointer);
            pulse_output_vblank();
            check_value(dut.acquire_binary_pixel == expected_pointer &&
                        dut.display_bank == next_bank,
                        "consumer acquires rollover frame in FIFO order");
            wait_released_source(expected_pointer - 1'b1);
            prepare_next_pixel(12'd0, 12'd0, 16'h0000,
                               "rollover frame resets scan address");
            prepare_next_pixel(OUTPUT_X_OFFSET - 2, OUTPUT_Y_OFFSET, 16'h0000,
                               "rollover RAM prefetch remains hidden");
            prepare_next_pixel(OUTPUT_X_OFFSET - 1, OUTPUT_Y_OFFSET, 16'h0000,
                               "rollover index pipeline remains hidden");
            expected_rgb = palette_rgb565(pattern_index(0, 0, 9 + index));
            prepare_next_pixel(OUTPUT_X_OFFSET, OUTPUT_Y_OFFSET, expected_rgb,
                               "rollover frame contains expected pixels");
        end
        check_value(dut.published_binary_source == 3'd5 &&
                    dut.acquire_binary_pixel == 3'd5 &&
                    dut.released_binary_pixel == 3'd4,
                    "ring pointers complete two full rollovers");

        // An independent source reset starts from nonzero pointers. The old
        // complete frame remains visible until vblank, then both domains hold
        // the four-phase grant low until the source has sampled it.
        @(negedge source_clk);
        source_resetn = 1'b0;
        repeat (12) @(posedge pixel_clk);
        check_value(machine_active,
                    "source reset holds old frame until vblank");
        pulse_output_vblank();
        check_value(!machine_active, "source reset returns diagnostics");
        repeat (20) @(posedge source_clk);
        check_value(!dut.capture_grant_launch && !dut.grant_low_ack_pixel,
                    "grant stays low throughout an extended source reset");
        @(negedge source_clk);
        source_resetn = 1'b1;
        wait_session_run();
        pulse_output_vblank();
        check_value(!machine_active, "source reset cannot expose stale banks");
        pulse_source_vsync();
        send_exact_frame(7);
        wait_published_pixel(3'd1);
        pulse_output_vblank();
        check_value(machine_active && dut.display_bank == 2'd0,
                    "fresh frame activates after source reset");

        // C64 readiness loss closes source writes immediately and changes DVI
        // mode only at vblank. Rearm requires a fresh empty epoch and frame.
        @(negedge source_clk);
        source_ready = 1'b0;
        repeat (12) @(posedge pixel_clk);
        check_value(machine_active,
                    "source readiness loss holds old frame until vblank");
        pulse_output_vblank();
        check_value(!machine_active, "source readiness loss returns diagnostics");
        repeat (20) @(posedge source_clk);
        check_value(!dut.capture_grant_launch && !dut.grant_low_ack_pixel,
                    "grant stays low throughout an extended source outage");
        @(negedge source_clk);
        source_ready = 1'b1;
        wait_session_run();
        pulse_output_vblank();
        check_value(!machine_active, "new epoch cannot expose stale bank");
        pulse_source_vsync();
        send_exact_frame(8);
        wait_published_pixel(3'd1);
        pulse_output_vblank();
        check_value(machine_active && dut.display_bank == 2'd0,
                    "fresh frame activates after readiness rearm");

        // Independent pixel reset also invalidates display and forces the full
        // low/high session round trip before a new frame can be acquired.
        @(negedge pixel_clk);
        pixel_resetn = 1'b0;
        repeat (5) @(posedge source_clk);
        check_value(!machine_active, "pixel reset fails machine scanout closed");
        @(negedge pixel_clk);
        pixel_resetn = 1'b1;
        wait_session_run();
        pulse_output_vblank();
        check_value(!machine_active, "pixel reset cannot reuse stale frame");
        pulse_source_vsync();
        send_exact_frame(9);
        wait_published_pixel(3'd1);
        pulse_output_vblank();
        check_value(machine_active, "new frame activates after pixel reset");

        // Run the displayed frame through the real timing/output register.
        // The checks include both coordinates that the prior bridge-only test
        // could misclassify: the hidden prefetch column and the last visible
        // column, where no subsequent RAM request is required.
        @(negedge pixel_clk);
        use_path_timing = 1'b1;
        path_resetn = 1'b1;
        check_path_pixel(OUTPUT_X_OFFSET, OUTPUT_Y_OFFSET - 1, 16'h0000,
                         "composed DVI path keeps top border black");
        check_path_pixel(OUTPUT_X_OFFSET - 2, OUTPUT_Y_OFFSET, 16'h0000,
                         "composed DVI path keeps RAM prefetch column black");
        check_path_pixel(OUTPUT_X_OFFSET - 1, OUTPUT_Y_OFFSET, 16'h0000,
                         "composed DVI path keeps index pipeline column black");
        check_path_pixel(OUTPUT_X_OFFSET, OUTPUT_Y_OFFSET,
                         palette_rgb565(pattern_index(0, 0, 9)),
                         "composed DVI path shows first source pixel");
        check_path_pixel(OUTPUT_X_OFFSET + 1, OUTPUT_Y_OFFSET,
                         palette_rgb565(pattern_index(0, 0, 9)),
                         "composed DVI path repeats first source pixel");
        check_path_pixel(OUTPUT_X_OFFSET + 2, OUTPUT_Y_OFFSET,
                         palette_rgb565(pattern_index(1, 0, 9)),
                         "composed DVI path advances to second source pixel");
        check_path_pixel(OUTPUT_X_OFFSET + OUTPUT_WIDTH - 1, OUTPUT_Y_OFFSET,
                         palette_rgb565(pattern_index(SOURCE_WIDTH - 1, 0, 9)),
                         "composed DVI path retains final visible column");
        check_path_pixel(OUTPUT_X_OFFSET + OUTPUT_WIDTH, OUTPUT_Y_OFFSET,
                         16'h0000,
                         "composed DVI path restores right border");
        check_path_pixel(OUTPUT_X_OFFSET, OUTPUT_Y_OFFSET + OUTPUT_SCALE,
                         palette_rgb565(pattern_index(0, 1, 9)),
                         "composed DVI path advances source rows exactly");

        check_value(!bank_collision_seen,
                    "producer never writes the displayed bank");
        check_value(!mode_changed_outside_vblank,
                    "display bank and physical mode change only at vblank");
        if (failure_count != 0) begin
            $fatal(1, "%0d live C64 bridge checks failed", failure_count);
        end else begin
            $display("PASS: live C64 ring, centered scaling, reset epochs, vblank swaps, and registered DVI edges");
            $finish;
        end
    end
endmodule
