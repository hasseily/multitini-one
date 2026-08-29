// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Validates the common byte stream, feeds the upstream cartridge/boot-ROM
// metadata ports as little-endian words, and packs cartridge bytes into the
// common 64-bit line-memory service.
module multitini_gameboy_loader (
    input  logic         clk,
    input  logic         reset,
    input  logic         clear_error,

    input  logic [7:0]   load_data,
    input  logic         load_valid,
    output logic         load_ready,
    input  logic         load_first,
    input  logic         load_last,
    input  logic [7:0]   load_kind,
    input  logic [31:0]  load_offset,
    input  logic [31:0]  load_total,

    output logic         ioctl_download,
    output logic         cart_download,
    output logic         cgb_boot_download,
    output logic         ioctl_wr,
    output logic [24:0]  ioctl_addr,
    output logic [15:0]  ioctl_dout,

    output logic [23:0]  memory_addr,
    output logic         memory_write,
    output logic [63:0]  memory_wdata,
    output logic [7:0]   memory_wstrb,
    output logic         memory_valid,
    input  logic         memory_ready,
    input  logic [63:0]  memory_rdata,
    input  logic         memory_rvalid,

    output logic         cart_loaded,
    output logic         boot_override_loaded,
    output logic [31:0]  cart_size,
    output logic         active,
    output logic         error
);
    import multitini_gameboy_defs_pkg::*;

    typedef enum logic [2:0] {
        STATE_IDLE,
        STATE_RECEIVE,
        STATE_MEMORY_ISSUE,
        STATE_MEMORY_WAIT,
        STATE_BOOT_FINISH
    } state_t;

    state_t state;
    logic [7:0] active_kind;
    logic [31:0] active_total;
    logic [31:0] expected_offset;
    logic [7:0] word_low;
    logic [63:0] line_data;
    logic [7:0] line_strobes;
    logic last_line;

    function automatic logic supported_object(
        input logic [7:0] kind,
        input logic [31:0] total
    );
        case (kind)
        MT_LOAD_GBC_BOOT:
            supported_object = total == MT_SIZE_GBC_BOOT;
        MT_LOAD_GB_CART:
            supported_object = total >= MT_SIZE_GB_CART_MIN &&
                               total <= MT_SIZE_GB_CART_MAX;
        default:
            supported_object = 1'b0;
        endcase
    endfunction

    assign load_ready = !reset &&
                        (state == STATE_IDLE || state == STATE_RECEIVE);
    assign active = state != STATE_IDLE;
    assign ioctl_download = active;
    assign cart_download = active && active_kind == MT_LOAD_GB_CART;
    assign cgb_boot_download = active && active_kind == MT_LOAD_GBC_BOOT;
    assign memory_write = 1'b1;
    assign memory_valid = state == STATE_MEMORY_ISSUE;

    always_ff @(posedge clk) begin : receive_stream
        logic framing_good;
        logic [63:0] next_line_data;
        logic [7:0] next_line_strobes;
        logic end_of_line;

        ioctl_wr <= 1'b0;
        if (reset) begin
            state <= STATE_IDLE;
            active_kind <= 8'd0;
            active_total <= 32'd0;
            expected_offset <= 32'd0;
            word_low <= 8'd0;
            line_data <= 64'd0;
            line_strobes <= 8'd0;
            last_line <= 1'b0;
            ioctl_addr <= 25'd0;
            ioctl_dout <= 16'd0;
            memory_addr <= 24'd0;
            memory_wdata <= 64'd0;
            memory_wstrb <= 8'd0;
            cart_loaded <= 1'b0;
            boot_override_loaded <= 1'b0;
            cart_size <= 32'd0;
            error <= 1'b0;
        end else begin
            if (clear_error)
                error <= 1'b0;

            if (state == STATE_MEMORY_ISSUE && memory_ready)
                state <= STATE_MEMORY_WAIT;

            if (state == STATE_MEMORY_WAIT && memory_rvalid) begin
                line_data <= 64'd0;
                line_strobes <= 8'd0;
                if (last_line) begin
                    cart_loaded <= 1'b1;
                    cart_size <= active_total;
                    state <= STATE_IDLE;
                end else begin
                    state <= STATE_RECEIVE;
                end
            end

            if (load_valid && load_ready) begin
                framing_good = 1'b0;
                if (state == STATE_IDLE) begin
                    framing_good = load_first && load_offset == 32'd0 &&
                                   !load_last &&
                                   supported_object(load_kind, load_total);
                    if (framing_good) begin
                        active_kind <= load_kind;
                        active_total <= load_total;
                        expected_offset <= 32'd1;
                        line_data <= 64'd0;
                        line_strobes <= 8'd0;
                        state <= STATE_RECEIVE;
                        if (load_kind == MT_LOAD_GB_CART) begin
                            cart_loaded <= 1'b0;
                            cart_size <= 32'd0;
                        end else begin
                            boot_override_loaded <= 1'b0;
                        end
                    end
                end else begin
                    framing_good = !load_first &&
                                   load_kind == active_kind &&
                                   load_total == active_total &&
                                   load_offset == expected_offset &&
                                   load_last ==
                                       (load_offset + 32'd1 == active_total);
                    if (framing_good)
                        expected_offset <= expected_offset + 32'd1;
                end

                if (!framing_good) begin
                    error <= 1'b1;
                    state <= STATE_IDLE;
                    cart_loaded <= active_kind == MT_LOAD_GB_CART ?
                                   1'b0 : cart_loaded;
                    boot_override_loaded <= active_kind == MT_LOAD_GBC_BOOT ?
                                            1'b0 : boot_override_loaded;
                end else begin
                    if (!load_offset[0]) begin
                        word_low <= load_data;
                    end else begin
                        ioctl_wr <= 1'b1;
                        ioctl_addr <= {1'b0, load_offset[23:1], 1'b0};
                        ioctl_dout <= {load_data, word_low};
                    end

                    if (load_kind == MT_LOAD_GB_CART) begin
                        next_line_data = load_first ? 64'd0 : line_data;
                        next_line_strobes = load_first ? 8'd0 : line_strobes;
                        next_line_data[load_offset[2:0] * 8 +: 8] = load_data;
                        next_line_strobes[load_offset[2:0]] = 1'b1;
                        end_of_line = load_offset[2:0] == 3'd7 || load_last;
                        if (end_of_line) begin
                            memory_addr <= MT_GB_MEMORY_ROM_BASE +
                                           {load_offset[23:3], 3'b000};
                            memory_wdata <= next_line_data;
                            memory_wstrb <= next_line_strobes;
                            last_line <= load_last;
                            state <= STATE_MEMORY_ISSUE;
                        end else begin
                            line_data <= next_line_data;
                            line_strobes <= next_line_strobes;
                        end
                    end else if (load_last) begin
                        boot_override_loaded <= 1'b1;
                        state <= STATE_BOOT_FINISH;
                    end
                end
            end

            // Keep the boot-download qualifier asserted for the registered
            // final ioctl write pulse, then end the object cleanly.
            if (state == STATE_BOOT_FINISH)
                state <= STATE_IDLE;
        end
    end

    wire unused_memory_rdata = ^memory_rdata;
endmodule
