// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Latest-value coherent crossing. A submitted word stays in the mailbox until
// acknowledged; writes made meanwhile are sent on the next free handshake.
module multitini_c64_expansion_config (
    input logic control_clk, control_resetn,
    input logic [31:0] requested_config,
    input logic core_clk, core_resetn,
    output logic [4:0] config_value
);
    import multitini_regs_pkg::*;
    logic [4:0] sent_config, destination;
    logic ready, valid, force_send;
    wire core_running;
    wire changed = (force_send || requested_config[4:0] != sent_config) && core_running;
    cdc_bit_sync core_reset_sync (
        .clk(control_clk), .resetn(control_resetn),
        .din(core_resetn), .dout(core_running)
    );
    multitini_async_mailbox #(.WIDTH(5), .DRAIN_ON_RESET(1'b1)) mailbox_i (
        .src_clk(control_clk), .src_resetn(control_resetn),
        .src_data(requested_config[4:0]), .src_valid(changed), .src_ready(ready),
        .dst_clk(core_clk), .dst_resetn(core_resetn),
        .dst_data(destination), .dst_valid(valid), .dst_ready(1'b1)
    );
    always_ff @(posedge control_clk) begin
        if (!control_resetn) begin
            sent_config <= MT_C64_EXPANSION_CONFIG_DEFAULT[4:0];
            force_send <= 1'b1;
        end else if (!core_running) force_send <= 1'b1;
        else if (changed && ready) begin
            sent_config <= requested_config[4:0];
            force_send <= 1'b0;
        end
    end
    always_ff @(posedge core_clk) begin
        if (!core_resetn) config_value <= MT_C64_EXPANSION_CONFIG_DEFAULT[4:0];
        else if (valid) config_value <= destination;
    end
endmodule
