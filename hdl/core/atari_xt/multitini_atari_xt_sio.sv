// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// POKEY has a timed one-byte receive latch. The ARM-side FIFO holds the rest
// of a reply while each byte traverses that latch at standard SIO speed.
module multitini_atari_xt_sio #(
    parameter integer CLOCK_HZ = 100_000_000
) (
    input  wire        clk, reset, machine_reset, paused,
    input  wire        command_n, tx_valid, rx_mode, rx_read,
    input  wire [7:0]  tx_data,
    output reg         rx_valid, rx_overrun,
    output wire        rx_busy,
    output reg  [7:0]  rx_data,

    // Event [17:10] epoch, [9:8] kind, [7:0] TX byte or zero.
    output wire        event_valid,
    output wire [17:0] event_data,
    input  wire        event_ready,
    // Reply [16] long gap, [15:8] epoch, [7:0] byte.
    input  wire        reply_valid,
    input  wire [16:0] reply_data,
    output wire        reply_ready
);
    localparam [1:0] EVENT_BYTE = 0, EVENT_ASSERT = 1, EVENT_RELEASE = 2, EVENT_OVERFLOW = 3;
    localparam integer FRAME_TICKS = (CLOCK_HZ + 1919) / 1920;
    localparam integer NORMAL_TICKS = integer'((longint'(CLOCK_HZ) * 55 + 99999) / 100000);
    localparam integer LONG_TICKS = (CLOCK_HZ + 999) / 1000;
    localparam integer TIMER_BITS = $clog2(LONG_TICKS + 1);

    reg command_last_n;
    reg [7:0] epoch;
    reg exchange_live, overflow_notice;
    wire command_assert = command_last_n && !command_n;
    wire command_release = !command_last_n && command_n;
    wire [7:0] next_epoch = epoch + 1'b1;

    // Two local holding slots cover coincident TX/COMMAND events and keep an
    // offered payload stable under backpressure. The board FIFO is the bulk
    // queue. Its overflow discards the current exchange and forces resync.
    reg [17:0] pending [0:1];
    reg [17:0] next_pending [0:1];
    reg [1:0] event_count, next_count;
    reg dropped, next_live, next_notice;
    wire [2:0] candidates;
    wire [17:0] candidate_data [0:2];
    assign event_valid = !reset && !machine_reset && event_count != 0;
    assign event_data = pending[0];
    assign candidates[0] = overflow_notice || command_assert;
    assign candidates[1] = !overflow_notice && tx_valid && (exchange_live || command_assert);
    assign candidates[2] = !overflow_notice && command_release && exchange_live;
    assign candidate_data[0] = overflow_notice ? {epoch, EVENT_OVERFLOW, 8'd0} :
                                               {next_epoch, EVENT_ASSERT, 8'd0};
    assign candidate_data[1] = {command_assert ? next_epoch : epoch, EVENT_BYTE, tx_data};
    assign candidate_data[2] = {epoch, EVENT_RELEASE, 8'd0};

    always @* begin
        next_count = event_count;
        next_pending[0] = pending[0];
        next_pending[1] = pending[1];
        next_live = exchange_live;
        next_notice = overflow_notice;
        dropped = 0;
        if (event_valid && event_ready) begin
            next_count = event_count - 1'b1;
            next_pending[0] = pending[1];
        end
        for (integer slot = 0; slot < 3; slot = slot + 1) begin
            if (candidates[slot]) begin
                if (next_count < 2) begin
                    next_pending[next_count[0]] = candidate_data[slot];
                    next_count = next_count + 1'b1;
                    if (slot == 0) begin
                        next_live = !overflow_notice;
                        next_notice = 0;
                    end
                end else begin
                    dropped = 1;
                    next_live = 0;
                    next_notice = 1;
                end
            end
        end
    end

    always @(posedge clk) begin
        if (reset) epoch <= 0;
        else if (!machine_reset && command_assert) epoch <= next_epoch;
        if (reset || machine_reset) begin
            command_last_n <= 1;
            exchange_live <= 0;
            overflow_notice <= 0;
            event_count <= 0;
        end else begin
            command_last_n <= command_n;
            exchange_live <= next_live;
            overflow_notice <= next_notice;
            event_count <= next_count;
        end
        pending[0] <= next_pending[0];
        pending[1] <= next_pending[1];
    end

    reg [TIMER_BITS-1:0] idle_ticks, frame_ticks;
    reg [7:0] receiving;
    reg unread;
    wire stale_reply = reply_valid && (!exchange_live || reply_data[15:8] != epoch);
    wire [TIMER_BITS-1:0] required_idle = reply_data[16] ? TIMER_BITS'(LONG_TICKS - FRAME_TICKS) :
                                                         TIMER_BITS'(NORMAL_TICKS - FRAME_TICKS);
    wire launch_rx = reply_valid && !stale_reply && !paused && !dropped &&
        command_n && !command_release && rx_mode && !rx_busy && idle_ticks >= required_idle;
    assign reply_ready = !reset && !machine_reset && (stale_reply || launch_rx);
    assign rx_busy = frame_ticks != 0;

    always @(posedge clk) begin
        rx_valid <= 0;
        rx_overrun <= 0;
        if (reset || machine_reset || command_assert || dropped) begin
            idle_ticks <= 0;
            frame_ticks <= 0;
            unread <= 0;
            rx_data <= 0;
        end else begin
            if (rx_read) unread <= 0;
            if (command_release) idle_ticks <= 0;
            else if (!paused && idle_ticks < TIMER_BITS'(LONG_TICKS)) idle_ticks <= idle_ticks + 1'b1;
            if (!rx_mode) frame_ticks <= 0;
            else if (!paused && rx_busy) begin
                frame_ticks <= frame_ticks - 1'b1;
                if (frame_ticks == 1) begin
                    rx_data <= receiving;
                    rx_valid <= 1;
                    rx_overrun <= unread && !rx_read;
                    unread <= 1;
                    idle_ticks <= 0;
                end
            end else if (launch_rx) begin
                receiving <= reply_data[7:0];
                frame_ticks <= TIMER_BITS'(FRAME_TICKS);
            end
        end
    end
endmodule
