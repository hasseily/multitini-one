// SPDX-License-Identifier: GPL-3.0-only
module multitini_apple_iigs_doc_irq (
    input  logic       clk,
    input  logic       reset,
    input  logic       slot,
    input  logic       request,
    input  logic [4:0] request_osc,
    input  logic       cancel,
    input  logic [4:0] cancel_osc,
    input  logic       ack,
    input  logic [7:0] ack_value,
    output logic [7:0] oir,
    output logic       irq
);
    // Each oscillator owns one queue node. A control write can remove it
    // without shifting the queue or leaving a stale entry behind.
    logic [31:0] waiting;
    logic [4:0] next_osc [32];
    logic [4:0] prev_osc [32];
    logic [4:0] head, tail;
    logic [4:0] visible_osc;
    logic [1:0] present_window;
    logic release_slot;

    wire cancel_waiting = cancel && waiting[cancel_osc];
    wire can_present = slot && !irq && !release_slot;
    wire present_waiting = can_present && !cancel_waiting && |waiting;
    wire remove_waiting = cancel_waiting || present_waiting;
    wire [4:0] removed = cancel_waiting ? cancel_osc : head;
    wire [4:0] before_removed = prev_osc[removed];
    wire [4:0] after_removed = next_osc[removed];
    wire empty_after_remove = !(|waiting) || (remove_waiting && head == tail);
    wire [4:0] new_tail = remove_waiting && tail == removed ? before_removed : tail;

    wire new_request = request && !waiting[request_osc] &&
                       !(irq && visible_osc == request_osc) &&
                       !(cancel && cancel_osc == request_osc);
    wire present_new = can_present && !(|waiting) && new_request;
    wire append = new_request && !present_new;

    assign oir = {~irq, 1'b1, visible_osc, 1'b1};

    always_ff @(posedge clk) begin
        if (reset) begin
            waiting <= '0;
            head <= '0;
            tail <= '0;
            visible_osc <= 5'd31;
            irq <= 1'b0;
            present_window <= '0;
            release_slot <= 1'b0;
        end else begin
            if (slot) begin
                present_window <= {present_window[0], present_waiting || present_new};
                release_slot <= 1'b0;
            end

            if (present_waiting || present_new) begin
                visible_osc <= present_waiting ? head : request_osc;
                irq <= 1'b1;
            end

            // The GLU sees the preceding DOC byte at each service edge.
            // Protect both publication and the following slot, when a read
            // can first fetch the new OIR byte. A stale idle byte never ACKs.
            if (ack && irq && !(|present_window) && ack_value == oir) begin
                irq <= 1'b0;
                release_slot <= 1'b1;
            end

            if (remove_waiting) begin
                waiting[removed] <= 1'b0;
                if (!empty_after_remove) begin
                    if (removed == head)
                        head <= after_removed;
                    else
                        next_osc[before_removed] <= after_removed;
                    if (removed == tail)
                        tail <= before_removed;
                    else
                        prev_osc[after_removed] <= before_removed;
                end
            end

            if (append) begin
                waiting[request_osc] <= 1'b1;
                if (empty_after_remove) begin
                    head <= request_osc;
                end else begin
                    next_osc[new_tail] <= request_osc;
                    prev_osc[request_osc] <= new_tail;
                end
                tail <= request_osc;
            end
        end
    end
endmodule
