// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Autonomous ordered frame ownership. After the first complete capture is
// queued, each output tick reserves exactly one job for the following tick.
// No CPU poll, latest-frame selection, or catch-up burst participates. The
// reader publishes at pixel vblank; its control-domain acknowledgement may
// arrive a few clocks later, including an old AXI burst's drain interval.
module multitini_video_frame_scheduler #(
    parameter integer DEADLINE_GRACE_CYCLES = 256
) (
    input wire clk, resetn, enable, clear_errors, transport_error,
    input wire [5:0] options,
    input wire frame_tick,
    input wire [31:0] capture_producer,
    output reg [31:0] capture_consumer,
    input wire reader_submit_ready, reader_active_valid, reader_quiescent,
    input wire [31:0] reader_active_value,
    output reg display_submit,
    output reg [31:0] display_value,
    output reg engine_start,
    output reg [1:0] engine_input_slot, engine_output_slot,
    output reg [5:0] engine_options,
    output reg engine_history_reset,
    input wire engine_busy, engine_done, engine_error,
    output wire busy, quiescent,
    output reg [31:0] processed_frames, deadline_misses,
    output reg sticky_error
);
    localparam integer GRACE_BITS = DEADLINE_GRACE_CYCLES > 0 ?
                                   $clog2(DEADLINE_GRACE_CYCLES + 1) : 1;
    reg enabled_last, cadence, launch_pending, in_flight, result_ready;
    reg history_valid, publication_pending, deadline_pending;
    reg [5:0] history_options;
    reg [29:0] sequence_counter, launch_sequence, due_sequence, checked_sequence;
    reg [31:0] job_value, pending_value;
    reg [GRACE_BITS-1:0] grace_remaining;
    wire [31:0] available = capture_producer - capture_consumer;
    wire [29:0] next_sequence = sequence_counter == 30'h3fffffff ?
                               30'd1 : sequence_counter + 1'b1;
    wire expected_active = reader_active_valid &&
                           reader_active_value[31:2] == checked_sequence;
    wire deadline_expired = deadline_pending && !expected_active && grace_remaining == 0;
    // A second tick cannot replace an unlaunched reservation: that would
    // silently discard a frame when no capture arrived for the first tick.
    wire reservation_missed = frame_tick && cadence && launch_pending;
    wire grace_overlapped = frame_tick && deadline_pending && !expected_active;
    wire deadline_fault = deadline_expired || reservation_missed || grace_overlapped;
    wire completion_failed = in_flight && engine_done && engine_error;
    wire new_fault = enable && (transport_error || available >= 4 ||
                                deadline_fault || completion_failed);
    reg [3:0] occupied;
    reg [1:0] free_slot;
    always @* begin
        occupied = 0;
        if (reader_active_valid) occupied[reader_active_value[1:0]] = 1'b1;
        if (publication_pending) occupied[pending_value[1:0]] = 1'b1;
        if (!occupied[0]) free_slot = 0;
        else if (!occupied[1]) free_slot = 1;
        else if (!occupied[2]) free_slot = 2;
        else free_slot = 3;
    end
    assign busy = in_flight || engine_busy || result_ready || publication_pending || launch_pending;
    assign quiescent = !enable && !in_flight && !engine_busy &&
                       !result_ready && !publication_pending && reader_quiescent;

    always @(posedge clk) begin
        if (!resetn) begin
            capture_consumer<=0; display_submit<=0; display_value<=0;
            engine_start<=0; engine_input_slot<=0; engine_output_slot<=0;
            engine_options<=0; engine_history_reset<=1;
            enabled_last<=0; cadence<=0; launch_pending<=0; in_flight<=0;
            result_ready<=0; history_valid<=0; history_options<=0;
            publication_pending<=0; deadline_pending<=0;
            sequence_counter<=0; launch_sequence<=0; due_sequence<=0; checked_sequence<=0;
            job_value<=0; pending_value<=0; grace_remaining<=0;
            processed_frames<=0; deadline_misses<=0; sticky_error<=0;
        end else begin
            enabled_last<=enable;
            display_submit<=0;
            engine_start<=0;
            if (publication_pending &&
                ((reader_active_valid && reader_active_value == pending_value) || reader_quiescent))
                publication_pending<=0;

            if (!enable) begin
                cadence<=0; launch_pending<=0; result_ready<=0;
                deadline_pending<=0; history_valid<=0;
                // Rebase only after an accepted job has stopped reading its
                // input. Capture itself is disabled/drained by the wrapper.
                if (!in_flight && !engine_busy) capture_consumer<=capture_producer;
                if (clear_errors && quiescent) begin sticky_error<=0; deadline_misses<=0; end
            end else if (!enabled_last) begin
                cadence<=0; launch_pending<=0; result_ready<=0;
                deadline_pending<=0; history_valid<=0;
            end

            // DONE is the engine's read/write-response fence, even if enable
            // fell or another fault occurred while it owned these slots.
            if (in_flight && engine_done) begin
                in_flight<=0;
                capture_consumer<=capture_consumer+1'b1;
                history_valid<=enable && !engine_error && !sticky_error && !new_fault;
                if (!engine_error) processed_frames<=processed_frames+1'b1;
                result_ready<=enable && !engine_error && !sticky_error && !new_fault;
            end

            if (enable && enabled_last && !sticky_error && !new_fault) begin
                if (deadline_pending) begin
                    if (expected_active) deadline_pending<=0;
                    else if (grace_remaining != 0) grace_remaining<=grace_remaining-1'b1;
                end
                if (frame_tick && (cadence || available != 0)) begin
                    if (cadence) begin
                        checked_sequence<=due_sequence;
                        deadline_pending<=1;
                        grace_remaining<=DEADLINE_GRACE_CYCLES;
                    end
                    cadence<=1;
                    sequence_counter<=next_sequence;
                    due_sequence<=next_sequence;
                    launch_sequence<=next_sequence;
                    launch_pending<=1;
                end
                if (launch_pending && !in_flight && !engine_busy && !result_ready && available != 0) begin
                    launch_pending<=0; in_flight<=1;
                    engine_start<=1;
                    engine_input_slot<=capture_consumer[1:0];
                    engine_output_slot<=free_slot;
                    engine_options<=options;
                    engine_history_reset<=!history_valid || options != history_options;
                    history_options<=options;
                    job_value<={launch_sequence,free_slot};
                end
                if (result_ready && !publication_pending && reader_submit_ready) begin
                    display_submit<=1; display_value<=job_value;
                    publication_pending<=1; pending_value<=job_value;
                    result_ready<=0;
                end
            end
            if (new_fault) begin
                sticky_error<=1;
                if (deadline_fault && !sticky_error) deadline_misses<=deadline_misses+1'b1;
            end
            if (sticky_error || new_fault) begin
                launch_pending<=0; result_ready<=0; deadline_pending<=0;
                history_valid<=0;
            end
        end
    end
endmodule
