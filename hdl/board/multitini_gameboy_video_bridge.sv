// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// One dual-clock RGB565 frame bank. Keeping the registered read inside this
// wrapper gives Vivado the canonical simple-dual-port block-RAM template.
module multitini_gameboy_frame_bank #(
    parameter integer FRAME_PIXELS = 160 * 144,
    parameter integer ADDRESS_WIDTH = $clog2(FRAME_PIXELS)
) (
    input  logic                     write_clk,
    input  logic                     write_enable,
    input  logic [ADDRESS_WIDTH-1:0] write_address,
    input  logic [15:0]              write_data,
    input  logic                     read_clk,
    input  logic                     read_enable,
    input  logic [ADDRESS_WIDTH-1:0] read_address,
    output logic [15:0]              read_data
);
    (* ram_style = "block" *) logic [15:0] memory [0:FRAME_PIXELS-1];

    always_ff @(posedge write_clk) begin
        if (write_enable)
            memory[write_address] <= write_data;
    end

    always_ff @(posedge read_clk) begin
        if (read_enable)
            read_data <= memory[read_address];
    end
endmodule

// Tear-free 160x144 Game Boy frame capture and centered 6x scaling into the
// common 1920x1080 scan. Four banks decouple the approximately 59.7 Hz LCD
// producer from the 60 Hz output: the producer publishes complete frames only,
// the consumer swaps only at output vblank, and a missing frame repeats cleanly.
module multitini_gameboy_video_bridge #(
    parameter integer SOURCE_WIDTH    = 160,
    parameter integer SOURCE_HEIGHT   = 144,
    parameter integer OUTPUT_SCALE    = 6,
    parameter integer OUTPUT_X_OFFSET = 480,
    parameter integer OUTPUT_Y_OFFSET = 108
) (
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
    localparam integer REPEAT_COUNT_WIDTH =
        OUTPUT_SCALE > 1 ? $clog2(OUTPUT_SCALE) : 1;
    localparam integer SOURCE_ROW_WIDTH =
        SOURCE_HEIGHT > 1 ? $clog2(SOURCE_HEIGHT) : 1;
    localparam integer SCALED_WIDTH = SOURCE_WIDTH * OUTPUT_SCALE;
    localparam integer SCALED_HEIGHT = SOURCE_HEIGHT * OUTPUT_SCALE;
    localparam integer POINTER_WIDTH = 3; // wrap bit plus two bank bits

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

    logic [COUNT_WIDTH-1:0] write_count;
    logic source_vsync_d;
    logic source_frame_armed;
    logic source_geometry_error;
    logic source_memory_write_enable;
    logic [15:0] source_rgb565;

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

    logic [ADDRESS_WIDTH-1:0] read_line_base;
    logic [ADDRESS_WIDTH-1:0] read_address;
    logic [REPEAT_COUNT_WIDTH-1:0] horizontal_repeat_count;
    logic [REPEAT_COUNT_WIDTH-1:0] vertical_repeat_count;
    logic [SOURCE_ROW_WIDTH-1:0] source_row_index;
    logic vertical_fetch_active;
    logic prefetch_active;
    logic output_active;
    logic pixel_read_enable;
    logic [15:0] bank_read_data_0;
    logic [15:0] bank_read_data_1;
    logic [15:0] bank_read_data_2;
    logic [15:0] bank_read_data_3;
    logic [15:0] selected_bank_read_data;
    logic [15:0] selected_rgb565;

    assign source_rgb565 = {source_r[7:3], source_g[7:2], source_b[7:3]};
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

    cdc_bit_sync grant_low_ack_to_pixel (
        .clk(pixel_clk), .resetn(pixel_resetn),
        .din(grant_low_ack_launch), .dout(grant_low_ack_pixel)
    );

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
        write_count < FRAME_PIXELS;

    multitini_gameboy_frame_bank #(
        .FRAME_PIXELS(FRAME_PIXELS), .ADDRESS_WIDTH(ADDRESS_WIDTH)
    ) frame_bank_0 (
        .write_clk(source_clk),
        .write_enable(source_memory_write_enable && write_bank == 2'd0),
        .write_address(write_count[ADDRESS_WIDTH-1:0]),
        .write_data(source_rgb565), .read_clk(pixel_clk),
        .read_enable(pixel_read_enable && display_bank == 2'd0),
        .read_address(read_address), .read_data(bank_read_data_0)
    );

    multitini_gameboy_frame_bank #(
        .FRAME_PIXELS(FRAME_PIXELS), .ADDRESS_WIDTH(ADDRESS_WIDTH)
    ) frame_bank_1 (
        .write_clk(source_clk),
        .write_enable(source_memory_write_enable && write_bank == 2'd1),
        .write_address(write_count[ADDRESS_WIDTH-1:0]),
        .write_data(source_rgb565), .read_clk(pixel_clk),
        .read_enable(pixel_read_enable && display_bank == 2'd1),
        .read_address(read_address), .read_data(bank_read_data_1)
    );

    multitini_gameboy_frame_bank #(
        .FRAME_PIXELS(FRAME_PIXELS), .ADDRESS_WIDTH(ADDRESS_WIDTH)
    ) frame_bank_2 (
        .write_clk(source_clk),
        .write_enable(source_memory_write_enable && write_bank == 2'd2),
        .write_address(write_count[ADDRESS_WIDTH-1:0]),
        .write_data(source_rgb565), .read_clk(pixel_clk),
        .read_enable(pixel_read_enable && display_bank == 2'd2),
        .read_address(read_address), .read_data(bank_read_data_2)
    );

    multitini_gameboy_frame_bank #(
        .FRAME_PIXELS(FRAME_PIXELS), .ADDRESS_WIDTH(ADDRESS_WIDTH)
    ) frame_bank_3 (
        .write_clk(source_clk),
        .write_enable(source_memory_write_enable && write_bank == 2'd3),
        .write_address(write_count[ADDRESS_WIDTH-1:0]),
        .write_data(source_rgb565), .read_clk(pixel_clk),
        .read_enable(pixel_read_enable && display_bank == 2'd3),
        .read_address(read_address), .read_data(bank_read_data_3)
    );

    // A source frame is defined by two rising LCD-VSYNC edges and must contain
    // exactly 160*144 one-cycle pixel-valid events. Any short, long, or blanked
    // frame is discarded as a unit.
    always_ff @(posedge source_clk) begin
        if (!source_resetn || !source_ready) begin
            source_session_state      <= SOURCE_WAIT_GRANT_LOW;
            source_session_ack_launch <= 1'b0;
            grant_low_ack_launch      <= 1'b0;
            published_binary_source   <= '0;
            write_count               <= '0;
            source_vsync_d             <= 1'b0;
            source_frame_armed        <= 1'b0;
            source_geometry_error     <= 1'b0;
        end else begin
            source_vsync_d <= source_vsync;
            case (source_session_state)
            SOURCE_WAIT_GRANT_LOW: begin
                source_session_ack_launch <= 1'b0;
                grant_low_ack_launch      <= 1'b0;
                published_binary_source   <= '0;
                write_count               <= '0;
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
                    source_frame_armed        <= 1'b0;
                    source_geometry_error     <= 1'b0;
                end else begin
                    source_session_ack_launch <= 1'b1;
                    if (source_vsync && !source_vsync_d) begin
                        if (source_frame_armed &&
                            write_count == FRAME_PIXELS &&
                            !source_geometry_error) begin
                            published_binary_source <= published_binary_next;
                            source_frame_armed <=
                                !source_queue_full_after_publish;
                        end else begin
                            source_frame_armed <= !source_queue_full;
                        end
                        write_count           <= '0;
                        source_geometry_error <= 1'b0;
                    end else if (source_frame_armed && source_valid) begin
                        if (source_memory_write_enable)
                            write_count <= write_count + 1'b1;
                        else
                            source_geometry_error <= 1'b1;
                    end
                end
            end
            endcase
        end
    end

    // Acquire only at output vblank. Releasing trails acquisition by one
    // displayed frame, so the producer can never write the visible bank.
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
                    capture_grant_launch <= 1'b0;
                    pixel_session_state  <= PIXEL_WAIT_ACK_LOW;
                end else if (source_session_ack_pixel &&
                             published_binary_pixel == 0) begin
                    pixel_session_state <= PIXEL_SESSION_RUN;
                end
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

    // The border overlay consumes the original fixed machine viewport.
    assign viewport_x = OUTPUT_X_OFFSET;
    assign viewport_y = OUTPUT_Y_OFFSET;
    assign viewport_width = SCALED_WIDTH;
    assign viewport_height = SCALED_HEIGHT;

    // Prefetch two coordinates ahead of the registered DVI output, matching
    // the common C64 path's proven synchronous-RAM alignment.
    assign prefetch_active =
        vertical_fetch_active && scan_x >= OUTPUT_X_OFFSET - 2 &&
        scan_x < OUTPUT_X_OFFSET + SCALED_WIDTH - 2;
    assign output_active =
        vertical_fetch_active && scan_x >= OUTPUT_X_OFFSET &&
        scan_x < OUTPUT_X_OFFSET + SCALED_WIDTH;
    assign pixel_read_enable = machine_active && display_valid && prefetch_active;

    always_comb begin
        unique case (display_bank)
            2'd0: selected_bank_read_data = bank_read_data_0;
            2'd1: selected_bank_read_data = bank_read_data_1;
            2'd2: selected_bank_read_data = bank_read_data_2;
            default: selected_bank_read_data = bank_read_data_3;
        endcase
        machine_rgb565 = machine_active && display_valid && output_active ?
                         selected_rgb565 : 16'h0000;
    end

    always_ff @(posedge pixel_clk) begin
        if (!pixel_resetn) begin
            read_line_base          <= '0;
            read_address            <= '0;
            horizontal_repeat_count <= '0;
            vertical_repeat_count   <= '0;
            source_row_index        <= '0;
            vertical_fetch_active   <= 1'b0;
            selected_rgb565         <= 16'h0000;
        end else begin
            selected_rgb565 <= selected_bank_read_data;
            if (scan_x == 0) begin
                horizontal_repeat_count <= '0;
                if (scan_y == 0) begin
                    read_line_base <= '0;
                    read_address   <= '0;
                    vertical_repeat_count <= '0;
                    source_row_index      <= '0;
                    vertical_fetch_active <= 1'b0;
                end else if (scan_y == OUTPUT_Y_OFFSET) begin
                    read_line_base          <= '0;
                    read_address            <= '0;
                    vertical_repeat_count   <= '0;
                    source_row_index        <= '0;
                    vertical_fetch_active   <= 1'b1;
                end else if (vertical_fetch_active) begin
                    if (vertical_repeat_count == OUTPUT_SCALE - 1) begin
                        vertical_repeat_count <= '0;
                        if (source_row_index == SOURCE_HEIGHT - 1) begin
                            read_line_base        <= '0;
                            read_address          <= '0;
                            source_row_index      <= '0;
                            vertical_fetch_active <= 1'b0;
                        end else begin
                            read_line_base   <= read_line_base + SOURCE_WIDTH;
                            read_address     <= read_line_base + SOURCE_WIDTH;
                            source_row_index <= source_row_index + 1'b1;
                        end
                    end else begin
                        vertical_repeat_count <= vertical_repeat_count + 1'b1;
                        read_address          <= read_line_base;
                    end
                end
            end

            if (pixel_read_enable) begin
                if (horizontal_repeat_count == OUTPUT_SCALE - 1) begin
                    horizontal_repeat_count <= '0;
                    read_address <= read_address + 1'b1;
                end else begin
                    horizontal_repeat_count <= horizontal_repeat_count + 1'b1;
                end
            end
        end
    end
endmodule
