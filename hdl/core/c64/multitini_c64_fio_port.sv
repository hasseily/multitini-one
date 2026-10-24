// SPDX-License-Identifier: GPL-3.0-only
// Native-domain consumer of a related-clock, stable-until-done register FIFO.
module multitini_c64_fio_port #(
    parameter integer FIO_DEPTH = 4,
    parameter integer FIO_ACK_EDGES = 2,
    parameter integer PTR_W = $clog2(FIO_DEPTH) + 1
) (
    input wire clk, reset,
    input wire [FIO_DEPTH*22-1:0] fio_entries,
    input wire [PTR_W-1:0] fio_wp,
    output logic [PTR_W-1:0] fio_done,
    input wire vic_busy, vic_hold_d011, vic_hold_spr,
    output logic vic_wr, vic_clr_m2m, vic_clr_m2d,
    output logic [5:0] vic_wa,
    output logic [7:0] vic_wd,
    output logic sid_wr, sid_latch,
    output logic [9:0] sid_a,
    output logic [7:0] sid_d,
    output logic u64_wr,
    output logic [9:0] u64_a,
    output logic [7:0] u64_d
);
    localparam integer DELAY_W = $clog2(FIO_ACK_EDGES + 2);
    logic [PTR_W-1:0] rp;
    logic [FIO_DEPTH-1:0] pending;
    logic [DELAY_W-1:0] delay_left [FIO_DEPTH];
    wire [PTR_W-1:0] rp_slot = rp & (FIO_DEPTH-1);
    wire [PTR_W-1:0] done_slot = fio_done & (FIO_DEPTH-1);
    wire [21:0] head = fio_entries[rp_slot*22 +: 22];
    wire target_sid = head[21];
    wire [1:0] kind = head[20:19];
    wire [9:0] address = head[17:8];
    wire [7:0] data_byte = head[7:0];
    wire sprite_y = ((address[5:0] <= 6'h0f) && address[0]) || address[5:0] == 6'h15;
    wire held = !target_sid && kind == 0 &&
        ((address[5:0] == 6'h11 && vic_hold_d011) || (sprite_y && vic_hold_spr));
    wire accept = rp != fio_wp && !vic_busy && !held && !reset;
    wire ack = !target_sid && kind == 0 &&
        (address[5:0] == 6'h19 || address[5:0] == 6'h1a);
    integer i;
    initial begin
        if (!(FIO_DEPTH == 1 || FIO_DEPTH == 2 || FIO_DEPTH == 4 || FIO_DEPTH == 8))
            $fatal(1, "FIO_DEPTH must be 1, 2, 4, or 8");
        if (FIO_ACK_EDGES < 1 || FIO_ACK_EDGES > 3) $fatal(1, "FIO_ACK_EDGES range");
    end
    always_ff @(posedge clk) begin
        if (reset) begin
            rp <= 0;
            fio_done <= 0;
            pending <= 0;
            vic_wr <= 0; vic_clr_m2m <= 0; vic_clr_m2d <= 0;
            sid_wr <= 0; sid_latch <= 0; u64_wr <= 0;
            vic_wa <= 0; vic_wd <= 0; sid_a <= 0; sid_d <= 0; u64_a <= 0; u64_d <= 0;
            for (i=0; i<FIO_DEPTH; i=i+1) delay_left[i] <= 0;
        end else begin
            vic_wr <= accept && !target_sid && kind == 0;
            vic_clr_m2m <= accept && !target_sid && kind == 1;
            vic_clr_m2d <= accept && !target_sid && kind == 2;
            sid_wr <= accept && target_sid && kind == 0;
            sid_latch <= accept && target_sid && kind == 3;
            u64_wr <= accept && !target_sid && kind == 0;
            for (i=0; i<FIO_DEPTH; i=i+1)
                if (delay_left[i] != 0) delay_left[i] <= delay_left[i] - 1'b1;
            // An accepted entry lands on the next edge. Delay 2 thus retires
            // at landing+1. Ordered readiness prevents a later short entry
            // overtaking an acknowledge with a longer settling interval.
            if (pending[done_slot] && delay_left[done_slot] <= 1) begin
                fio_done <= fio_done + 1'b1;
                pending[done_slot] <= 0;
            end
            if (accept) begin
                vic_wa <= address[5:0]; vic_wd <= data_byte;
                sid_a <= address; sid_d <= data_byte;
                u64_a <= address; u64_d <= data_byte;
                rp <= rp + 1'b1;
                pending[rp_slot] <= 1;
                delay_left[rp_slot] <= ack ? 1 + FIO_ACK_EDGES : 2;
            end
        end
    end
endmodule
