// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module multitini_atari_sio_transport (
    input wire control_clk, control_resetn,
    input wire reg_wr_en, reg_rd_en,
    input wire [11:0] reg_wr_addr, reg_rd_addr,
    input wire [3:0] reg_wr_strb,
    input wire [31:0] reg_wr_data,
    output reg [31:0] reg_rd_data,

    input wire core_clk, core_resetn, guest_reset,
    input wire event_valid,
    input wire [17:0] event_data,
    output wire event_ready,
    output wire reply_valid,
    output wire [16:0] reply_data,
    input wire reply_ready
);
    import multitini_regs_pkg::*;

    // Even a one-cycle guest reset stays visible long enough for the control
    // domain to clear its queues. The mailboxes drain any accepted transfer
    // before accepting data again. Keep both root clocks running through reset.
    wire control_up_core, core_up_control;
    reg [3:0] core_release;
    reg core_up_launch;
    cdc_bit_sync control_up_sync(.clk(core_clk), .resetn(core_resetn),
        .din(control_resetn), .dout(control_up_core));
    wire core_link_resetn = core_resetn && !guest_reset && control_up_core && core_release[3];
    always @(posedge core_clk) begin
        if (!core_resetn || guest_reset || !control_up_core)
            core_release <= 0;
        else
            core_release <= {core_release[2:0], 1'b1};
        core_up_launch <= core_link_resetn;
    end
    cdc_bit_sync core_up_sync(.clk(control_clk), .resetn(control_resetn),
        .din(core_up_launch), .dout(core_up_control));
    wire control_link_resetn = control_resetn && core_up_control;

    wire rx_arrive, rx_room, rx_valid;
    wire [17:0] rx_arriving, rx_front;
    wire tx_ready, tx_valid, tx_depart;
    wire [16:0] tx_front;
    reg tx_fault;
    wire tx_write = reg_wr_en && reg_wr_addr == MT_REG_ATARI_SIO_TX;
    wire tx_write_ok = tx_ready && reg_wr_strb == 4'hf && reg_wr_data[31:17] == 0;
    wire rx_read = reg_rd_en && reg_rd_addr == MT_REG_ATARI_SIO_RX;

    multitini_async_mailbox #(.WIDTH(18), .DRAIN_ON_RESET(1'b1)) event_mailbox (
        .src_clk(core_clk), .src_resetn(core_link_resetn),
        .src_data(event_data), .src_valid(event_valid), .src_ready(event_ready),
        .dst_clk(control_clk), .dst_resetn(control_link_resetn),
        .dst_data(rx_arriving), .dst_valid(rx_arrive), .dst_ready(rx_room));
    multitini_atari_sio_queue #(.WIDTH(18)) event_queue (
        .clk(control_clk), .resetn(control_link_resetn),
        .push_valid(rx_arrive), .push_ready(rx_room), .push_data(rx_arriving),
        .pop_valid(rx_valid), .pop_ready(rx_read), .pop_data(rx_front));

    multitini_atari_sio_queue #(.WIDTH(17)) reply_queue (
        .clk(control_clk), .resetn(control_link_resetn),
        .push_valid(tx_write && tx_write_ok), .push_ready(tx_ready), .push_data(reg_wr_data[16:0]),
        .pop_valid(tx_valid), .pop_ready(tx_depart), .pop_data(tx_front));
    multitini_async_mailbox #(.WIDTH(17), .DRAIN_ON_RESET(1'b1)) reply_mailbox (
        .src_clk(control_clk), .src_resetn(control_link_resetn),
        .src_data(tx_front), .src_valid(tx_valid), .src_ready(tx_depart),
        .dst_clk(core_clk), .dst_resetn(core_link_resetn),
        .dst_data(reply_data), .dst_valid(reply_valid), .dst_ready(reply_ready));

    always @(posedge control_clk) begin
        if (!control_link_resetn)
            tx_fault <= 0;
        else if (tx_write && !tx_write_ok)
            tx_fault <= 1;
        else if (reg_wr_en && reg_wr_addr == MT_REG_ATARI_SIO_STATUS && reg_wr_strb[0] &&
                 |(reg_wr_data & MT_ATARI_SIO_STATUS_TX_FAULT))
            tx_fault <= 0;
    end

    always_comb begin
        reg_rd_data = 0;
        case (reg_rd_addr)
            MT_REG_ATARI_SIO_STATUS:
                reg_rd_data = MT_ATARI_SIO_STATUS_SIGNATURE |
                    {28'd0, !control_link_resetn, tx_fault && control_link_resetn, tx_ready, rx_valid};
            MT_REG_ATARI_SIO_RX:
                if (rx_valid)
                    reg_rd_data = MT_ATARI_SIO_RX_VALID | {14'd0, rx_front};
            default: ;
        endcase
    end
endmodule

// A 512-word BRAM queue with a registered head. The pointer stays on the
// head until it is consumed, including while AXI or a mailbox backpressures.
module multitini_atari_sio_queue #(
    parameter integer WIDTH = 18
)(
    input wire clk, resetn,
    input wire push_valid,
    output wire push_ready,
    input wire [WIDTH-1:0] push_data,
    output wire pop_valid,
    input wire pop_ready,
    output reg [WIDTH-1:0] pop_data
);
    (* ram_style = "block" *) reg [WIDTH-1:0] words [0:511];
    reg [9:0] read_pointer, write_pointer;
    reg head_valid;
    wire full = write_pointer == (read_pointer ^ 10'h200);
    wire fetch_head = resetn && !head_valid && read_pointer != write_pointer;
    wire push = push_valid && push_ready;
    wire pop = pop_valid && pop_ready;
    assign push_ready = resetn && !full;
    assign pop_valid = resetn && head_valid;

    always @(posedge clk) begin
        if (push)
            words[write_pointer[8:0]] <= push_data;
        if (fetch_head)
            pop_data <= words[read_pointer[8:0]];
    end

    always @(posedge clk) begin
        if (!resetn) begin
            read_pointer <= 0;
            write_pointer <= 0;
            head_valid <= 0;
        end else begin
            if (push)
                write_pointer <= write_pointer + 1'b1;
            if (pop) begin
                read_pointer <= read_pointer + 1'b1;
                head_valid <= 0;
            end else if (fetch_head)
                head_valid <= 1;
        end
    end
endmodule
