// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Bounded event transport. Each producer has 16 words of local storage and
// the existing reset-draining asynchronous mailbox owns one in-flight word.
// No multi-bit payload or queue pointer crosses without the XPM handshake.
module multitini_c64_iec_transport (
    input logic host_clk, host_resetn,
    input logic [1:0] control,
    output wire [17:0] control_value,
    input wire [17:0] control_snapshot,
    output wire [35:0] status_value,
    input wire [35:0] status_snapshot,
    output logic [31:0] status, rx_data,
    input logic rx_pop,
    input logic [31:0] tx_data,
    input logic tx_push,
    input logic core_clk, core_resetn, guest_reset,
    output logic run, quiesce,
    input logic endpoint_busy, endpoint_quiescent,
    input logic [15:0] endpoint_token,
    input logic [31:0] event_data,
    input logic event_valid,
    output logic event_ready,
    output logic [31:0] reply_data,
    output logic reply_valid,
    input logic reply_ready
);
    logic [1:0] previous_control;
    logic [15:0] control_epoch;
    wire [15:0] next_epoch=control_epoch+16'(control!=previous_control);
    wire [17:0] core_control=control_snapshot;
    wire [35:0] host_snapshot=status_snapshot;
    wire guest_reset_host;
    logic guest_reset_launch=1'b1;
    wire host_live=host_resetn && control[0] && !guest_reset_host;
    wire core_live=core_resetn && core_control[0] && !guest_reset;
    wire host_rx_valid, rx_mailbox_ready, tx_mailbox_ready;
    logic [4:0] rx_count,tx_count;
    logic [3:0] rx_wr,rx_rd,tx_wr,tx_rd;
    (* ram_style="distributed" *) logic [31:0] rx_queue[0:15],tx_queue[0:15];
    logic fault;
    wire rx_send=core_live && rx_count!=0;
    wire tx_send=host_live && tx_count!=0;
    wire rx_take=rx_send && rx_mailbox_ready;
    wire tx_take=tx_send && tx_mailbox_ready;
    wire rx_put=event_valid && event_ready;
    wire tx_put=tx_push && host_live && tx_count<16;
    assign event_ready=core_live && rx_count<16;
    assign run=core_live;
    assign quiesce=core_control[1];
    // The board shell includes these fields in its shared C64 snapshots.
    // Separate word mailboxes would synchronize the same remote reset twice.
    assign control_value={next_epoch,control};
    assign status_value={core_control[17:2],endpoint_token,core_live,endpoint_busy,
        endpoint_quiescent && rx_count==0 && rx_mailbox_ready,1'b0};
    // Guest reset combines several native controls. Register that expression
    // before its crossing; native cancellation still uses guest_reset directly.
    always_ff @(posedge core_clk) guest_reset_launch<=!core_resetn || guest_reset;
    cdc_bit_sync guest_reset_i(.clk(host_clk),.resetn(host_resetn),
        .din(guest_reset_launch),.dout(guest_reset_host));
    multitini_async_mailbox #(.WIDTH(32),.DRAIN_ON_RESET(1)) rx_i(
        .src_clk(core_clk),.src_resetn(core_live),.src_data(rx_queue[rx_rd]),
        .src_valid(rx_send),.src_ready(rx_mailbox_ready),
        .dst_clk(host_clk),.dst_resetn(host_live),.dst_data(rx_data),
        .dst_valid(host_rx_valid),.dst_ready(rx_pop));
    multitini_async_mailbox #(.WIDTH(32),.DRAIN_ON_RESET(1)) tx_i(
        .src_clk(host_clk),.src_resetn(host_live),.src_data(tx_queue[tx_rd]),
        .src_valid(tx_send),.src_ready(tx_mailbox_ready),
        .dst_clk(core_clk),.dst_resetn(core_live),.dst_data(reply_data),
        .dst_valid(reply_valid),.dst_ready(reply_ready));
    always_comb begin
        status=0;
        status[31:16]=host_snapshot[19:4];
        status[0]=host_rx_valid;
        status[1]=host_live && tx_count<16;
        status[2]=host_snapshot[3];
        status[3]=host_snapshot[2];
        status[4]=fault;
        // A fresh control epoch must make the full round trip. A previous
        // session's empty indication cannot complete a new quiesce request.
        status[5]=control[1] && host_snapshot[35:20]==next_epoch &&
            host_snapshot[1] && !host_rx_valid;
    end
    always_ff @(posedge core_clk) begin
        if(!core_live) begin rx_count<=0;rx_wr<=0;rx_rd<=0;end
        else begin
            if(rx_put) begin rx_queue[rx_wr]<=event_data;rx_wr<=rx_wr+1'b1;end
            if(rx_take) rx_rd<=rx_rd+1'b1;
            case({rx_put,rx_take})
                2'b10:rx_count<=rx_count+1'b1;
                2'b01:rx_count<=rx_count-1'b1;
                default:;
            endcase
        end
    end
    always_ff @(posedge host_clk) begin
        if(!host_resetn) begin control_epoch<=0;previous_control<=0;end
        else begin control_epoch<=next_epoch;previous_control<=control;end
        if(!host_live) begin tx_count<=0;tx_wr<=0;tx_rd<=0;fault<=0;end
        else begin
            if(tx_push && !tx_put) fault<=1;
            if(tx_put) begin tx_queue[tx_wr]<=tx_data;tx_wr<=tx_wr+1'b1;end
            if(tx_take) tx_rd<=tx_rd+1'b1;
            case({tx_put,tx_take})
                2'b10:tx_count<=tx_count+1'b1;
                2'b01:tx_count<=tx_count-1'b1;
                default:;
            endcase
        end
    end
endmodule
