// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Tear-free PAL/NTSC C64 frame-rate conversion into a 1080p scan.
//
// The fixed C64 palette is stored as four-bit indices in four independent
// dual-clock frame banks. A source-domain write pointer publishes only exact,
// complete frames. The pixel domain acquires at most one frame at each output
// vblank and releases the previously displayed bank only after the swap. A
// missing source frame therefore repeats cleanly; a stopped consumer fills the
// ring and causes whole source frames to be dropped before capture begins.
//
// XPM Gray-pointer crossings carry only monotonically incrementing binary
// pointers. The grant/acknowledge rearm barrier empties both sides after an
// independent reset or loss of C64 readiness before either RAM port is enabled.
module multitini_c64_frame_bank #(
    parameter integer FRAME_PIXELS = 382 * 270,
    parameter integer ADDRESS_WIDTH = $clog2(FRAME_PIXELS)
) (
    input  logic                     write_clk,
    input  logic                     write_enable,
    input  logic [ADDRESS_WIDTH-1:0] write_address,
    input  logic [3:0]               write_data,
    input  logic                     read_clk,
    input  logic                     read_enable,
    input  logic [ADDRESS_WIDTH-1:0] read_address,
    output logic [3:0]               read_data
);
    // Keep the two RAM ports structurally simple. Vivado's 7-series block-RAM
    // inference requires the registered read to be inside the memory wrapper;
    // palette expansion and bank selection stay outside this module.
    (* ram_style = "block" *) logic [3:0] memory [0:FRAME_PIXELS-1];

    always_ff @(posedge write_clk) begin
        if (write_enable)
            memory[write_address] <= write_data;
    end

    always_ff @(posedge read_clk) begin
        if (read_enable)
            read_data <= memory[read_address];
    end
endmodule

module multitini_c64_video_bridge #(
    parameter bit DYNAMIC_DISPLAY=1'b0,
    parameter bit     NTSC_MODE        = 1'b0,
    parameter integer SOURCE_WIDTH    = NTSC_MODE ? 404 : 382,
    parameter integer SOURCE_HEIGHT   = NTSC_MODE ? 250 : 270,
    parameter integer OUTPUT_SCALE    = 4,
    parameter integer OUTPUT_X_OFFSET = NTSC_MODE ? 152 : 196,
    parameter integer OUTPUT_Y_OFFSET = NTSC_MODE ? 40 : 0
) (
    input wire [11:0] display_view_x, display_view_y, display_view_width, display_view_height,
    input wire [3:0] display_scale_num, display_scale_den,
    input  logic        source_clk,
    input  logic        source_resetn,
    input  logic        source_ready,
    input  logic [7:0]  source_r,
    input  logic [7:0]  source_g,
    input  logic [7:0]  source_b,
    input  logic        source_de,
    input  logic        source_vsync,
    input  logic        source_valid,

    input  logic        pixel_clk,
    input  logic        pixel_resetn,
    input  logic        diagnostic_request,
    output logic [11:0] viewport_x, viewport_y, viewport_width, viewport_height,
    input  logic [11:0] scan_x,
    input  logic [11:0] scan_y,
    input  logic        vblank_start,

    output logic        machine_active,
    output logic [15:0] machine_rgb565
);
    localparam integer FRAME_PIXELS = SOURCE_WIDTH * SOURCE_HEIGHT;
    localparam integer ADDRESS_WIDTH = $clog2(FRAME_PIXELS);
    localparam integer COUNT_WIDTH = $clog2(FRAME_PIXELS + 1);
    localparam integer LINE_PIXEL_COUNT_WIDTH = $clog2(SOURCE_WIDTH + 1);
    localparam integer LINE_COUNT_WIDTH = $clog2(SOURCE_HEIGHT + 1);
    localparam integer REPEAT_COUNT_WIDTH =
        OUTPUT_SCALE > 1 ? $clog2(OUTPUT_SCALE) : 1;
    localparam integer SCALED_WIDTH = SOURCE_WIDTH * OUTPUT_SCALE;
    localparam integer SCALED_HEIGHT = SOURCE_HEIGHT * OUTPUT_SCALE;
    localparam integer POINTER_WIDTH = 3; // one wrap bit plus two bank bits

    logic [COUNT_WIDTH-1:0] write_count;
    logic [LINE_PIXEL_COUNT_WIDTH-1:0] line_pixel_count;
    logic [LINE_COUNT_WIDTH-1:0] captured_lines;
    logic source_de_d;
    logic source_vsync_d;
    logic source_frame_armed;
    logic source_geometry_error;
    logic source_memory_write_enable;
    logic [4:0] source_palette_code;

    logic capture_grant_launch;
    logic capture_grant_source;
    logic source_session_ack_launch;
    logic source_session_ack_pixel;
    logic grant_low_ack_launch;
    logic grant_low_ack_pixel;

    logic [POINTER_WIDTH-1:0] published_binary_source;
    logic [POINTER_WIDTH-1:0] published_binary_next;
    logic [POINTER_WIDTH-1:0] published_binary_pixel;
    logic [POINTER_WIDTH-1:0] acquire_binary_pixel;
    logic [POINTER_WIDTH-1:0] released_binary_pixel;
    logic [POINTER_WIDTH-1:0] released_binary_source;
    logic source_queue_full;
    logic source_queue_full_after_publish;
    logic pixel_frame_available;
    logic [1:0] write_bank;
    logic [1:0] display_bank;
    logic display_valid;

    typedef enum logic [1:0] {
        SOURCE_WAIT_GRANT_LOW,
        SOURCE_WAIT_GRANT_HIGH,
        SOURCE_SESSION_RUN
    } source_session_state_t;
    source_session_state_t source_session_state;

    typedef enum logic [1:0] {
        PIXEL_WAIT_ACK_LOW,
        PIXEL_REQUEST_SESSION,
        PIXEL_SESSION_RUN,
        PIXEL_FAULT_TO_VBLANK
    } pixel_session_state_t;
    pixel_session_state_t pixel_session_state;

    logic [ADDRESS_WIDTH-1:0] read_line_base;
    logic [ADDRESS_WIDTH-1:0] read_address;
    logic [REPEAT_COUNT_WIDTH-1:0] horizontal_repeat_count;
    logic prefetch_active;
    logic output_active;
    logic pixel_read_enable;
    logic [3:0] bank_read_data_0;
    logic [3:0] bank_read_data_1;
    logic [3:0] bank_read_data_2;
    logic [3:0] bank_read_data_3;
    logic [3:0] selected_bank_read_data;
    logic [3:0] selected_palette_index;

    function automatic logic [4:0] encode_palette(
        input logic [7:0] red,
        input logic [7:0] green,
        input logic [7:0] blue
    );
        begin
            unique case ({red, green, blue})
                24'h000000: encode_palette = {1'b1, 4'h0};
                24'hFFFFFF: encode_palette = {1'b1, 4'h1};
                24'h813338: encode_palette = {1'b1, 4'h2};
                24'h75CEC8: encode_palette = {1'b1, 4'h3};
                24'h8E3C97: encode_palette = {1'b1, 4'h4};
                24'h56AC4D: encode_palette = {1'b1, 4'h5};
                24'h2E2C9B: encode_palette = {1'b1, 4'h6};
                24'hEDF171: encode_palette = {1'b1, 4'h7};
                24'h8E5029: encode_palette = {1'b1, 4'h8};
                24'h553800: encode_palette = {1'b1, 4'h9};
                24'hC46C71: encode_palette = {1'b1, 4'hA};
                24'h4A4A4A: encode_palette = {1'b1, 4'hB};
                24'h7B7B7B: encode_palette = {1'b1, 4'hC};
                24'hA9FF9F: encode_palette = {1'b1, 4'hD};
                24'h706DEB: encode_palette = {1'b1, 4'hE};
                24'hB2B2B2: encode_palette = {1'b1, 4'hF};
                default:   encode_palette = 5'b0;
            endcase
        end
    endfunction

    function automatic logic [15:0] palette_rgb565(
        input logic [3:0] index
    );
        begin
            unique case (index)
                4'h0: palette_rgb565 = 16'h0000;
                4'h1: palette_rgb565 = 16'hFFFF;
                4'h2: palette_rgb565 = 16'h8187;
                4'h3: palette_rgb565 = 16'h7679;
                4'h4: palette_rgb565 = 16'h89F2;
                4'h5: palette_rgb565 = 16'h5569;
                4'h6: palette_rgb565 = 16'h2973;
                4'h7: palette_rgb565 = 16'hEF8E;
                4'h8: palette_rgb565 = 16'h8A85;
                4'h9: palette_rgb565 = 16'h51C0;
                4'hA: palette_rgb565 = 16'hC36E;
                4'hB: palette_rgb565 = 16'h4A49;
                4'hC: palette_rgb565 = 16'h7BCF;
                4'hD: palette_rgb565 = 16'hAFF3;
                4'hE: palette_rgb565 = 16'h737D;
                default: palette_rgb565 = 16'hB596;
            endcase
        end
    endfunction

    assign source_palette_code =
        encode_palette(source_r, source_g, source_b);
    assign write_bank = published_binary_source[1:0];
    assign published_binary_next = published_binary_source + 1'b1;
    assign source_queue_full =
        published_binary_source[1:0] == released_binary_source[1:0] &&
        published_binary_source[2] != released_binary_source[2];
    assign source_queue_full_after_publish =
        published_binary_next[1:0] == released_binary_source[1:0] &&
        published_binary_next[2] != released_binary_source[2];
    assign pixel_frame_available =
        acquire_binary_pixel != published_binary_pixel;

    cdc_bit_sync capture_grant_to_source (
        .clk(source_clk), .resetn(source_resetn),
        .din(capture_grant_launch), .dout(capture_grant_source)
    );

    cdc_bit_sync source_session_ack_to_pixel (
        .clk(pixel_clk), .resetn(pixel_resetn),
        .din(source_session_ack_launch), .dout(source_session_ack_pixel)
    );

    // Explicitly acknowledge that the source FSM has sampled a low grant.
    // The pixel clock is much faster than the C64 clock, so merely observing a
    // low session acknowledgement cannot prove that this four-phase handshake
    // has completed its return-to-zero phase.
    cdc_bit_sync grant_low_ack_to_pixel (
        .clk(pixel_clk), .resetn(pixel_resetn),
        .din(grant_low_ack_launch), .dout(grant_low_ack_pixel)
    );

    // XPM converts binary input to Gray before synchronization and converts it
    // back only after four destination-domain settling stages.
    xpm_cdc_gray #(
        .DEST_SYNC_FF(4), .INIT_SYNC_FF(1), .REG_OUTPUT(1),
        .SIM_ASSERT_CHK(1), .SIM_LOSSLESS_GRAY_CHK(0),
        .WIDTH(POINTER_WIDTH)
    ) published_pointer_to_pixel (
        .src_clk(source_clk), .src_in_bin(published_binary_source),
        .dest_clk(pixel_clk), .dest_out_bin(published_binary_pixel)
    );

    xpm_cdc_gray #(
        .DEST_SYNC_FF(4), .INIT_SYNC_FF(1), .REG_OUTPUT(1),
        .SIM_ASSERT_CHK(1), .SIM_LOSSLESS_GRAY_CHK(0),
        .WIDTH(POINTER_WIDTH)
    ) released_pointer_to_source (
        .src_clk(pixel_clk), .src_in_bin(released_binary_pixel),
        .dest_clk(source_clk), .dest_out_bin(released_binary_source)
    );

    assign source_memory_write_enable =
        source_resetn && source_ready &&
        source_session_state == SOURCE_SESSION_RUN &&
        source_frame_armed && source_valid && source_de &&
        !(source_vsync && !source_vsync_d) &&
        source_palette_code[4] && write_count < FRAME_PIXELS;

    multitini_c64_frame_bank #(
        .FRAME_PIXELS(FRAME_PIXELS), .ADDRESS_WIDTH(ADDRESS_WIDTH)
    ) frame_bank_0 (
        .write_clk(source_clk),
        .write_enable(source_memory_write_enable && write_bank == 2'd0),
        .write_address(write_count[ADDRESS_WIDTH-1:0]),
        .write_data(source_palette_code[3:0]),
        .read_clk(pixel_clk),
        .read_enable(pixel_read_enable && display_bank == 2'd0),
        .read_address(read_address), .read_data(bank_read_data_0)
    );

    multitini_c64_frame_bank #(
        .FRAME_PIXELS(FRAME_PIXELS), .ADDRESS_WIDTH(ADDRESS_WIDTH)
    ) frame_bank_1 (
        .write_clk(source_clk),
        .write_enable(source_memory_write_enable && write_bank == 2'd1),
        .write_address(write_count[ADDRESS_WIDTH-1:0]),
        .write_data(source_palette_code[3:0]),
        .read_clk(pixel_clk),
        .read_enable(pixel_read_enable && display_bank == 2'd1),
        .read_address(read_address), .read_data(bank_read_data_1)
    );

    multitini_c64_frame_bank #(
        .FRAME_PIXELS(FRAME_PIXELS), .ADDRESS_WIDTH(ADDRESS_WIDTH)
    ) frame_bank_2 (
        .write_clk(source_clk),
        .write_enable(source_memory_write_enable && write_bank == 2'd2),
        .write_address(write_count[ADDRESS_WIDTH-1:0]),
        .write_data(source_palette_code[3:0]),
        .read_clk(pixel_clk),
        .read_enable(pixel_read_enable && display_bank == 2'd2),
        .read_address(read_address), .read_data(bank_read_data_2)
    );

    multitini_c64_frame_bank #(
        .FRAME_PIXELS(FRAME_PIXELS), .ADDRESS_WIDTH(ADDRESS_WIDTH)
    ) frame_bank_3 (
        .write_clk(source_clk),
        .write_enable(source_memory_write_enable && write_bank == 2'd3),
        .write_address(write_count[ADDRESS_WIDTH-1:0]),
        .write_data(source_palette_code[3:0]),
        .read_clk(pixel_clk),
        .read_enable(pixel_read_enable && display_bank == 2'd3),
        .read_address(read_address), .read_data(bank_read_data_3)
    );

    // Source session barrier and exact-frame publisher. Capture is armed only
    // on source VSYNC; space appearing mid-frame is deliberately ignored.
    always_ff @(posedge source_clk) begin
        if (!source_resetn || !source_ready) begin
            source_session_state      <= SOURCE_WAIT_GRANT_LOW;
            source_session_ack_launch <= 1'b0;
            grant_low_ack_launch      <= 1'b0;
            published_binary_source   <= '0;
            write_count               <= '0;
            line_pixel_count          <= '0;
            captured_lines            <= '0;
            source_de_d               <= 1'b0;
            source_vsync_d            <= 1'b0;
            source_frame_armed        <= 1'b0;
            source_geometry_error     <= 1'b0;
        end else begin
            case (source_session_state)
            SOURCE_WAIT_GRANT_LOW: begin
                source_session_ack_launch <= 1'b0;
                grant_low_ack_launch      <= 1'b0;
                published_binary_source   <= '0;
                write_count               <= '0;
                line_pixel_count          <= '0;
                captured_lines            <= '0;
                source_de_d               <= 1'b0;
                source_vsync_d            <= 1'b0;
                source_frame_armed        <= 1'b0;
                source_geometry_error     <= 1'b0;
                if (!capture_grant_source) begin
                    source_session_state <= SOURCE_WAIT_GRANT_HIGH;
                    grant_low_ack_launch <= 1'b1;
                end
            end

            SOURCE_WAIT_GRANT_HIGH: begin
                source_session_ack_launch <= 1'b0;
                grant_low_ack_launch      <= 1'b1;
                published_binary_source   <= '0;
                write_count               <= '0;
                line_pixel_count          <= '0;
                captured_lines            <= '0;
                source_de_d               <= 1'b0;
                source_vsync_d            <= 1'b0;
                source_frame_armed        <= 1'b0;
                source_geometry_error     <= 1'b0;
                if (capture_grant_source && released_binary_source == 0) begin
                    source_session_state      <= SOURCE_SESSION_RUN;
                    source_session_ack_launch <= 1'b1;
                end
            end

            default: begin // SOURCE_SESSION_RUN
                grant_low_ack_launch <= 1'b1;
                if (!capture_grant_source) begin
                    source_session_state      <= SOURCE_WAIT_GRANT_LOW;
                    source_session_ack_launch <= 1'b0;
                    grant_low_ack_launch      <= 1'b0;
                    published_binary_source   <= '0;
                    write_count               <= '0;
                    line_pixel_count          <= '0;
                    captured_lines            <= '0;
                    source_de_d               <= 1'b0;
                    source_vsync_d            <= 1'b0;
                    source_frame_armed        <= 1'b0;
                    source_geometry_error     <= 1'b0;
                end else begin
                    source_session_ack_launch <= 1'b1;
                    if (source_valid) begin
                        source_de_d    <= source_de;
                        source_vsync_d <= source_vsync;

                        if (source_vsync && !source_vsync_d) begin
                            if (source_frame_armed &&
                                write_count == FRAME_PIXELS &&
                                captured_lines == SOURCE_HEIGHT &&
                                line_pixel_count == 0 &&
                                !source_geometry_error) begin
                                published_binary_source <= published_binary_next;
                                source_frame_armed <=
                                    !source_queue_full_after_publish;
                            end else begin
                                source_frame_armed <= !source_queue_full;
                            end
                            write_count           <= '0;
                            line_pixel_count      <= '0;
                            captured_lines        <= '0;
                            source_geometry_error <= 1'b0;
                        end else if (source_frame_armed) begin
                            if (source_de) begin
                                if (source_memory_write_enable) begin
                                    write_count <= write_count + 1'b1;
                                end else begin
                                    source_geometry_error <= 1'b1;
                                end

                                if (line_pixel_count < SOURCE_WIDTH)
                                    line_pixel_count <= line_pixel_count + 1'b1;
                                else
                                    source_geometry_error <= 1'b1;
                            end else if (source_de_d) begin
                                if (line_pixel_count != SOURCE_WIDTH)
                                    source_geometry_error <= 1'b1;
                                if (captured_lines < SOURCE_HEIGHT)
                                    captured_lines <= captured_lines + 1'b1;
                                else
                                    source_geometry_error <= 1'b1;
                                line_pixel_count <= '0;
                            end
                        end
                    end
                end
            end
            endcase
        end
    end

    // Consume in FIFO order only at output vblank. The release pointer trails
    // acquisition by one displayed frame, preserving display-bank ownership.
    always_ff @(posedge pixel_clk) begin
        if (!pixel_resetn) begin
            pixel_session_state   <= PIXEL_WAIT_ACK_LOW;
            capture_grant_launch  <= 1'b0;
            acquire_binary_pixel  <= '0;
            released_binary_pixel <= '0;
            display_bank           <= 2'd0;
            display_valid          <= 1'b0;
            machine_active         <= 1'b0;
        end else begin
            case (pixel_session_state)
            PIXEL_WAIT_ACK_LOW: begin
                capture_grant_launch  <= 1'b0;
                acquire_binary_pixel  <= '0;
                released_binary_pixel <= '0;
                display_bank           <= 2'd0;
                display_valid          <= 1'b0;
                machine_active         <= 1'b0;
                if (!source_session_ack_pixel &&
                    grant_low_ack_pixel && published_binary_pixel == 0)
                    pixel_session_state <= PIXEL_REQUEST_SESSION;
            end

            PIXEL_REQUEST_SESSION: begin
                capture_grant_launch  <= 1'b1;
                acquire_binary_pixel  <= '0;
                released_binary_pixel <= '0;
                display_bank           <= 2'd0;
                display_valid          <= 1'b0;
                machine_active         <= 1'b0;
                if (!grant_low_ack_pixel) begin
                    capture_grant_launch  <= 1'b0;
                    pixel_session_state   <= PIXEL_WAIT_ACK_LOW;
                end else if (source_session_ack_pixel &&
                    published_binary_pixel == 0)
                    pixel_session_state <= PIXEL_SESSION_RUN;
            end

            PIXEL_SESSION_RUN: begin
                capture_grant_launch <= 1'b1;
                if (!grant_low_ack_pixel || !source_session_ack_pixel) begin
                    capture_grant_launch  <= 1'b0;
                    acquire_binary_pixel  <= '0;
                    released_binary_pixel <= '0;
                    if (vblank_start) begin
                        display_valid       <= 1'b0;
                        machine_active      <= 1'b0;
                        pixel_session_state <= PIXEL_WAIT_ACK_LOW;
                    end else begin
                        pixel_session_state <= PIXEL_FAULT_TO_VBLANK;
                    end
                end else if (vblank_start) begin
                    if (pixel_frame_available) begin
                        display_bank <= acquire_binary_pixel[1:0];
                        acquire_binary_pixel <= acquire_binary_pixel + 1'b1;
                        if (display_valid)
                            released_binary_pixel <=
                                released_binary_pixel + 1'b1;
                        display_valid <= 1'b1;
                    end
                    machine_active <= !diagnostic_request &&
                                      (display_valid || pixel_frame_available);
                end
            end

            default: begin // PIXEL_FAULT_TO_VBLANK
                capture_grant_launch  <= 1'b0;
                acquire_binary_pixel  <= '0;
                released_binary_pixel <= '0;
                if (vblank_start) begin
                    display_valid       <= 1'b0;
                    machine_active      <= 1'b0;
                    pixel_session_state <= PIXEL_WAIT_ACK_LOW;
                end
            end
            endcase
        end
    end

    wire [11:0] view_x=DYNAMIC_DISPLAY ? display_view_x : OUTPUT_X_OFFSET;
    wire [11:0] view_y=DYNAMIC_DISPLAY ? display_view_y : OUTPUT_Y_OFFSET;
    wire [11:0] view_width=DYNAMIC_DISPLAY ? display_view_width : SCALED_WIDTH;
    wire [11:0] view_height=DYNAMIC_DISPLAY ? display_view_height : SCALED_HEIGHT;
    wire [3:0] scale_num=DYNAMIC_DISPLAY ? display_scale_num : OUTPUT_SCALE;
    reg [3:0] vertical_repeat_count;
    // The border overlay consumes the original fixed machine viewport.
    assign viewport_x = view_x;
    assign viewport_y = view_y;
    assign viewport_width = view_width;
    assign viewport_height = view_height;

    // Request two coordinates ahead. The first stage is the synchronous RAM
    // output and the second registers the selected four-bit index, splitting
    // the distributed bank mux from palette expansion and the DVI register.
    assign prefetch_active =
        scan_y >= view_y &&
        scan_y < view_y + view_height &&
        scan_x >= view_x - 2 &&
        scan_x < view_x + view_width - 2;
    assign output_active =
        scan_y >= view_y &&
        scan_y < view_y + view_height &&
        scan_x >= view_x &&
        scan_x < view_x + view_width;
    assign pixel_read_enable = machine_active && display_valid && prefetch_active;

    always_comb begin
        unique case (display_bank)
            2'd0: selected_bank_read_data = bank_read_data_0;
            2'd1: selected_bank_read_data = bank_read_data_1;
            2'd2: selected_bank_read_data = bank_read_data_2;
            default: selected_bank_read_data = bank_read_data_3;
        endcase
        // Both hidden pipeline coordinates remain black at the DVI pins. The
        // final visible coordinate consumes the registered index even though
        // no further RAM request is needed there.
        machine_rgb565 = machine_active && display_valid && output_active ?
                         palette_rgb565(selected_palette_index) : 16'h0000;
    end

    always_ff @(posedge pixel_clk) begin
        if (!pixel_resetn) begin
            read_line_base          <= '0;
            read_address            <= '0;
            horizontal_repeat_count <= '0;
            vertical_repeat_count <= '0;
            selected_palette_index  <= 4'd0;
        end else begin
            selected_palette_index <= selected_bank_read_data;
            if (scan_x == 0) begin
                horizontal_repeat_count <= '0;
                if (scan_y <= view_y) begin
                    vertical_repeat_count <= 0;
                    read_line_base <= '0;
                    read_address   <= '0;
                end else if (scan_y < view_y + view_height &&
                             vertical_repeat_count == scale_num-1) begin
                    vertical_repeat_count <= 0;
                    read_line_base <= read_line_base + SOURCE_WIDTH;
                    read_address   <= read_line_base + SOURCE_WIDTH;
                end else begin
                    vertical_repeat_count <= vertical_repeat_count+1'b1;
                    read_address <= read_line_base;
                end
            end

            if (pixel_read_enable) begin
                if (horizontal_repeat_count == scale_num - 1) begin
                    horizontal_repeat_count <= '0;
                    read_address <= read_address + 1'b1;
                end else begin
                    horizontal_repeat_count <= horizontal_repeat_count + 1'b1;
                end
            end
        end
    end

endmodule
