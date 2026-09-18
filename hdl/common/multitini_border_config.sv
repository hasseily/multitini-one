// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Coherent latest-value crossing. The mailbox holds each submitted word until
// acknowledgement; subsequent writes remain in the control register and are
// sent when free. Pending pixel-domain values become active only at vblank.
module multitini_border_config (
    input logic control_clk, control_resetn,
    input logic [31:0] requested_config,
    input logic pixel_clk, pixel_resetn, vblank_start,
    output logic [12:0] frame_config,
    output wire control_pixel_running
);
    import multitini_regs_pkg::*;
    logic [12:0] sent_config, destination, pending_config;
    logic ready, valid;
    logic force_send;
    wire pixel_running;
    assign control_pixel_running = pixel_running;
    wire [12:0] requested = requested_config[12:0];
    wire changed = (force_send || requested != sent_config) && pixel_running;
    cdc_bit_sync pixel_reset_sync (
        .clk(control_clk), .resetn(control_resetn),
        .din(pixel_resetn), .dout(pixel_running)
    );
    multitini_async_mailbox #(.WIDTH(13)) mailbox_i (
        .src_clk(control_clk), .src_resetn(control_resetn),
        .src_data(requested), .src_valid(changed), .src_ready(ready),
        .dst_clk(pixel_clk), .dst_resetn(pixel_resetn),
        .dst_data(destination), .dst_valid(valid), .dst_ready(1'b1)
    );
    always_ff @(posedge control_clk) begin
        if (!control_resetn) begin
            sent_config <= MT_BORDER_CONFIG_DEFAULT;
            force_send <= 1;
        end else if (!pixel_running) force_send <= 1;
        else if (changed && ready) begin
            sent_config <= requested;
            force_send <= 0;
        end
    end
    always_ff @(posedge pixel_clk) begin
        if (!pixel_resetn) begin
            pending_config <= MT_BORDER_CONFIG_DEFAULT;
            frame_config <= MT_BORDER_CONFIG_DEFAULT;
        end else begin
            if (valid) pending_config <= destination;
            if (vblank_start)
                frame_config <= valid ? destination : pending_config;
        end
    end
endmodule
