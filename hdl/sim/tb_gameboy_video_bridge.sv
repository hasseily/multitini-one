// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_gameboy_video_bridge;
    localparam int SOURCE_WIDTH = 4;
    localparam int SOURCE_HEIGHT = 3;
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
    logic path_de;
    logic path_dvi_clk;
    logic path_hsync;
    logic path_vsync;
    wire [11:0] scan_x = use_path_timing ? path_scan_x : manual_scan_x;
    wire [11:0] scan_y = use_path_timing ? path_scan_y : manual_scan_y;
    wire vblank_start = use_path_timing ? path_vblank_start :
                                             manual_vblank_start;
    logic machine_active;
    logic [15:0] machine_rgb565;

    logic [1:0] prior_display_bank = 2'd0;
    logic prior_machine_active = 1'b0;
    logic bank_collision_seen = 1'b0;
    logic mode_changed_outside_vblank = 1'b0;
    integer failure_count = 0;

    always #5 source_clk = !source_clk;
    always #3 pixel_clk = !pixel_clk;

    multitini_gameboy_video_bridge #(
        .SOURCE_WIDTH(SOURCE_WIDTH), .SOURCE_HEIGHT(SOURCE_HEIGHT),
        .OUTPUT_SCALE(OUTPUT_SCALE), .OUTPUT_X_OFFSET(OUTPUT_X_OFFSET),
        .OUTPUT_Y_OFFSET(OUTPUT_Y_OFFSET)
    ) dut (
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

    function automatic [15:0] pattern_rgb565(
        input integer x,
        input integer y,
        input integer variant
    );
        logic [4:0] red;
        logic [5:0] green;
        logic [4:0] blue;
        begin
            red = x + variant;
            green = y + (variant * 3);
            blue = x + y + (variant * 2);
            pattern_rgb565 = {red, green, blue};
        end
    endfunction

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            failure_count = failure_count + 1;
        end
    endtask

    task automatic send_pixel(input logic [15:0] rgb565);
        begin
            @(negedge source_clk);
            source_r = {rgb565[15:11], 3'b000};
            source_g = {rgb565[10:5], 2'b00};
            source_b = {rgb565[4:0], 3'b000};
            source_de = 1'b1;
            source_valid = 1'b1;
            @(posedge source_clk);
            #1;
            @(negedge source_clk);
            source_de = 1'b0;
            source_valid = 1'b0;
        end
    endtask

    task automatic pulse_source_vsync;
        begin
            @(negedge source_clk);
            source_vsync = 1'b1;
            @(posedge source_clk);
            #1;
            @(negedge source_clk);
            source_vsync = 1'b0;
            @(posedge source_clk);
            #1;
        end
    endtask

    task automatic send_frame(input integer variant, input integer pixels);
        integer index;
        integer x;
        integer y;
        begin
            for (index = 0; index < pixels; index = index + 1) begin
                x = index % SOURCE_WIDTH;
                y = index / SOURCE_WIDTH;
                send_pixel(pattern_rgb565(x, y, variant));
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
                $display("DVI x=%0d y=%0d expected=%h actual=%h de=%b bridge=%h addr=%0d repeat=%0d",
                         expected_x, expected_y, expected_rgb,
                         {path_red, path_green, path_blue}, path_de,
                         machine_rgb565, dut.read_address,
                         dut.horizontal_repeat_count);
            check_value(path_de && {path_red, path_green, path_blue} ==
                        expected_rgb, message);
        end
    endtask

    always @(posedge source_clk) begin
        #1;
        if (dut.source_memory_write_enable && dut.display_valid &&
            dut.write_bank == dut.display_bank)
            bank_collision_seen = 1'b1;
    end

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
        fork
            begin
                #200000;
                $fatal(1, "Game Boy video bridge timeout");
            end
        join_none

        repeat (4) @(negedge source_clk);
        check_value(dut.viewport_x == OUTPUT_X_OFFSET &&
                    dut.viewport_y == OUTPUT_Y_OFFSET &&
                    dut.viewport_width == SOURCE_WIDTH * OUTPUT_SCALE &&
                    dut.viewport_height == SOURCE_HEIGHT * OUTPUT_SCALE,
                    "border viewport matches the fixed Game Boy scan");
        source_resetn = 1'b1;
        source_ready = 1'b1;
        pixel_resetn = 1'b1;
        wait_session_run();

        // The first LCD boundary arms capture. Only a complete 160x144-style
        // event count is publishable.
        pulse_source_vsync();
        send_frame(1, SOURCE_WIDTH * SOURCE_HEIGHT - 1);
        check_value(dut.published_binary_source == 3'd0,
                    "short frame is rejected");
        send_frame(2, SOURCE_WIDTH * SOURCE_HEIGHT);
        check_value(dut.published_binary_source == 3'd1,
                    "exact frame is published");
        wait_published_pixel(3'd1);

        diagnostic_request = 1'b0;
        repeat (5) @(posedge pixel_clk);
        check_value(!machine_active, "machine waits for output vblank");
        pulse_output_vblank();
        check_value(machine_active && dut.display_bank == 2'd0,
                    "complete frame activates at output vblank");

        // Exercise the actual registered DVI stage, including the two hidden
        // prefetch columns, horizontal/vertical repeats, and right border.
        @(negedge pixel_clk);
        use_path_timing = 1'b1;
        path_resetn = 1'b1;
        check_path_pixel(OUTPUT_X_OFFSET - 2, OUTPUT_Y_OFFSET, 16'h0000,
                         "prefetch column remains black");
        check_path_pixel(OUTPUT_X_OFFSET - 1, OUTPUT_Y_OFFSET, 16'h0000,
                         "pipeline column remains black");
        check_path_pixel(OUTPUT_X_OFFSET, OUTPUT_Y_OFFSET,
                         pattern_rgb565(0, 0, 2),
                         "first source pixel reaches DVI");
        check_path_pixel(OUTPUT_X_OFFSET + 1, OUTPUT_Y_OFFSET,
                         pattern_rgb565(0, 0, 2),
                         "source pixel is repeated horizontally");
        check_path_pixel(OUTPUT_X_OFFSET + 2, OUTPUT_Y_OFFSET,
                         pattern_rgb565(1, 0, 2),
                         "next source pixel advances exactly");
        check_path_pixel(OUTPUT_X_OFFSET, OUTPUT_Y_OFFSET + 1,
                         pattern_rgb565(0, 0, 2),
                         "source row is repeated vertically");
        check_path_pixel(OUTPUT_X_OFFSET, OUTPUT_Y_OFFSET + OUTPUT_SCALE,
                         pattern_rgb565(0, 1, 2),
                         "next source row advances exactly");
        check_path_pixel(OUTPUT_X_OFFSET + OUTPUT_WIDTH, OUTPUT_Y_OFFSET,
                         16'h0000, "right border remains black");

        @(negedge pixel_clk);
        use_path_timing = 1'b0;
        path_resetn = 1'b0;
        diagnostic_request = 1'b1;
        repeat (5) @(posedge pixel_clk);
        check_value(machine_active, "diagnostic request waits for vblank");
        pulse_output_vblank();
        check_value(!machine_active, "diagnostics take ownership at vblank");

        check_value(!bank_collision_seen,
                    "producer never writes displayed frame bank");
        check_value(!mode_changed_outside_vblank,
                    "display mode and bank change only at vblank");
        if (failure_count != 0)
            $fatal(1, "%0d Game Boy bridge checks failed", failure_count);
        $display("PASS: Game Boy exact-frame ring, centered scaling, clean repeat, and registered DVI edges");
        $finish;
    end
endmodule
