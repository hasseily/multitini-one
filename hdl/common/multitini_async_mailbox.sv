// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// One-entry coherent mailbox for sparse control traffic between unrelated
// clocks. Xilinx's handshake macro carries the coherent multi-bit payload and
// exposes an acknowledgement only after the destination accepts it.
module multitini_async_mailbox #(
    parameter integer WIDTH = 32,
    parameter bit DRAIN_ON_RESET = 1'b0
) (
    input  logic             src_clk,
    input  logic             src_resetn,
    input  logic [WIDTH-1:0] src_data,
    input  logic             src_valid,
    output logic             src_ready,

    input  logic             dst_clk,
    input  logic             dst_resetn,
    output logic [WIDTH-1:0] dst_data,
    output logic             dst_valid,
    input  logic             dst_ready
);
    logic [WIDTH-1:0] payload_hold;
    logic [WIDTH-1:0] destination_data;
    logic             source_send;
    logic             source_received;
    logic             source_busy;
    logic             destination_request;
    logic             destination_ack;

    wire link_recovered_source, link_recovering_destination;
    assign src_ready = src_resetn && !source_busy && !source_received && link_recovered_source;
    assign dst_valid = dst_resetn && destination_request && !destination_ack && !link_recovering_destination;
    assign dst_data = destination_data;

    // XPM requires an external acknowledgement to be registered in the
    // destination domain and held until dest_req is released.  Gating valid
    // after acceptance also guarantees exactly one downstream transfer while
    // the acknowledgement makes its round trip to the source clock.
    generate if (DRAIN_ON_RESET) begin : g_drain_reset
        // XPM has no runtime reset. A soft reset therefore drains its level
        // handshake instead of cancelling src_send. Configuration-time INIT
        // values are implemented by 7-series flip-flops. Destination recovery
        // suppresses stale payloads until source idle has made a round trip.
        reg source_idle=0, recovering=1;
        reg [3:0] settle=0;
        (* ASYNC_REG = "TRUE" *) reg [1:0] idle_destination=0, recovered_source=0;
        initial begin
            payload_hold='0;source_send=0;source_busy=0;destination_ack=0;
        end
        assign link_recovered_source=recovered_source[1];
        assign link_recovering_destination=recovering;
        always @(posedge src_clk) begin
            source_idle<=src_resetn && !source_busy && !source_received;
            if(!src_resetn)recovered_source<=0;
            else recovered_source<={recovered_source[0],!recovering};
            if(!source_busy)source_send<=0;
            if(source_busy && source_received)begin source_send<=0;source_busy<=0;end
            else if(src_valid && src_ready)begin
                payload_hold<=src_data;source_send<=1;source_busy<=1;
            end
        end
        always @(posedge dst_clk) begin
            idle_destination<={idle_destination[0],source_idle};
            if(!dst_resetn)begin recovering<=1;settle<=0;end
            else if(recovering)begin
                if(settle!=15)settle<=settle+1'b1;
                if(settle==15 && idle_destination[1] && !destination_request && !destination_ack)
                    recovering<=0;
            end
            if(!destination_request)destination_ack<=0;
            else if(destination_request && !destination_ack && (dst_ready || recovering || !dst_resetn))destination_ack<=1;
        end
    end else begin : g_reset
    assign link_recovered_source=1'b1;
    assign link_recovering_destination=1'b0;
    always_ff @(posedge dst_clk) begin
        if (!dst_resetn) begin
            destination_ack <= 1'b0;
        end else if (!destination_request) begin
            destination_ack <= 1'b0;
        end else if (!destination_ack && dst_ready) begin
            destination_ack <= 1'b1;
        end
    end

    always_ff @(posedge src_clk) begin
        if (!src_resetn) begin
            payload_hold  <= '0;
            source_send    <= 1'b0;
            source_busy    <= 1'b0;
        end else begin
            if (!source_busy) begin
                source_send <= 1'b0;
            end
            if (src_valid && src_ready) begin
                payload_hold   <= src_data;
                source_send    <= 1'b1;
                source_busy    <= 1'b1;
            end else if (source_busy && source_received) begin
                source_send <= 1'b0;
                source_busy <= 1'b0;
            end
        end
    end

    end endgenerate

    xpm_cdc_handshake #(
        .DEST_EXT_HSK(1),
        .DEST_SYNC_FF(4),
        .INIT_SYNC_FF(0),
        .SIM_ASSERT_CHK(1),
        .SRC_SYNC_FF(4),
        .WIDTH(WIDTH)
    ) handshake_i (
        .dest_ack(destination_ack),
        .dest_clk(dst_clk),
        .dest_out(destination_data),
        .dest_req(destination_request),
        .src_clk(src_clk),
        .src_in(payload_hold),
        .src_rcv(source_received),
        .src_send(source_send)
    );
endmodule
