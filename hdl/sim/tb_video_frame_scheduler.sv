// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_video_frame_scheduler #(parameter integer GRACE=16);
    localparam integer PERIOD=GRACE+112;
    reg clk=0;
    always #5 clk=~clk;
    reg resetn=0, enable=0, clear_errors=0, transport_error=0;
    reg [7:0] options=0;
    reg frame_tick=0;
    reg [31:0] capture_producer=0;
    wire [31:0] capture_consumer, display_value, processed_frames, deadline_misses;
    wire display_submit, engine_start, engine_history_reset, busy, quiescent, sticky_error;
    wire [1:0] engine_input_slot, engine_output_slot;
    wire [7:0] engine_options;
    reg engine_busy=0, engine_done=0, engine_error=0;
    reg reader_active_valid=0;
    reg [31:0] reader_active_value=0;
    reg reader_pending=0, reader_block=0, hold_reader_drain=0;
    reg [31:0] reader_pending_value=0;
    integer reader_pending_age=0, reader_delay=0, ack_remaining=0;
    reg ack_pending=0;
    wire reader_submit_ready=enable && !reader_pending && !reader_block;
    wire reader_quiescent=!enable && !reader_pending && !reader_active_valid && !ack_pending;
    integer engine_latency=20, engine_remaining=0;
    reg fail_job=0;
    integer starts=0, completions=0, submissions=0, activations=0, history_resets=0;
    reg [31:0] expected_input=0, last_consumer=0;
    reg previous_done=0, previous_enable=0;
    reg [29:0] last_submit_sequence=0;
    reg check_sequence=1;
    integer scenario=0;
    multitini_video_frame_scheduler #(.DEADLINE_GRACE_CYCLES(GRACE)) dut(.*);

    // The engine is independent of CPU work. DONE follows all modeled DDR
    // responses, not merely the last produced pixel.
    always @(posedge clk) begin
        if (!resetn) begin
            engine_busy<=0; engine_done<=0; engine_error<=0; engine_remaining<=0;
            starts<=0; completions<=0; history_resets<=0;
            expected_input<=0; last_consumer<=0; previous_done<=0; previous_enable<=0;
        end else begin
            previous_done<=engine_done;
            previous_enable<=enable;
            last_consumer<=capture_consumer;
            if (enable && previous_enable && capture_consumer != last_consumer && !previous_done)
                $fatal(1,"scenario %0d released a capture before engine DONE",scenario);
            if (!enable && !engine_busy) expected_input<=capture_producer;
            engine_done<=0;
            if (engine_start) begin
                if (engine_busy) $fatal(1,"started a busy engine");
                if (engine_input_slot !== expected_input[1:0])
                    $fatal(1,"scenario %0d skipped input: expected %0d got slot %0d",scenario,expected_input,engine_input_slot);
                if ((reader_active_valid && engine_output_slot==reader_active_value[1:0]) ||
                    (reader_pending && engine_output_slot==reader_pending_value[1:0]))
                    $fatal(1,"overwrote an active or pending reader slot");
                engine_busy<=1; engine_error<=0; engine_remaining<=engine_latency;
                starts<=starts+1;
                if (engine_history_reset) history_resets<=history_resets+1;
            end else if (engine_busy) begin
                if (engine_remaining==0) begin
                    engine_busy<=0; engine_done<=1; engine_error<=fail_job;
                    completions<=completions+1;
                    expected_input<=expected_input+1'b1;
                end else engine_remaining<=engine_remaining-1;
            end
        end
    end

    // Bundled reader publication has a small crossing delay, only activates
    // at vblank, and acknowledges after a configurable old-burst drain.
    always @(posedge clk) begin
        if (!resetn) begin
            reader_pending<=0; reader_active_valid<=0; reader_active_value<=0;
            reader_pending_value<=0; reader_pending_age<=0;
            ack_pending<=0; ack_remaining<=0;
            submissions<=0; activations<=0; last_submit_sequence<=0;
        end else begin
            if (display_submit) begin
                if (!reader_submit_ready) $fatal(1,"published without reader readiness");
                if (check_sequence && display_value[31:2] !=
                    (last_submit_sequence==30'h3fffffff ? 30'd1 : last_submit_sequence+1'b1))
                    $fatal(1,"publication was reordered or skipped");
                last_submit_sequence<=display_value[31:2];
                reader_pending<=1; reader_pending_value<=display_value;
                reader_pending_age<=0; submissions<=submissions+1;
            end else if (reader_pending) reader_pending_age<=reader_pending_age+1;
            if (enable && frame_tick && reader_pending && !ack_pending && reader_pending_age>=3) begin
                ack_pending<=1; ack_remaining<=reader_delay;
                activations<=activations+1;
            end
            if (ack_pending && !hold_reader_drain) begin
                if (ack_remaining==0) begin
                    reader_active_valid<=enable;
                    reader_active_value<=reader_pending_value;
                    reader_pending<=0; ack_pending<=0;
                end else ack_remaining<=ack_remaining-1;
            end
            if (!enable && !hold_reader_drain) begin
                reader_active_valid<=0; reader_pending<=0; ack_pending<=0;
            end
        end
    end

    task cycles(input integer count);
        integer n;
        begin for(n=0;n<count;n=n+1) @(negedge clk); end
    endtask
    task tick;
        begin frame_tick=1; cycles(1); frame_tick=0; end
    endtask
    task capture;
        begin capture_producer=capture_producer+1'b1; end
    endtask
    task restart;
        begin
            resetn=0; enable=0; frame_tick=0; clear_errors=0; transport_error=0;
            capture_producer=0; options=0; reader_block=0; hold_reader_drain=0;
            reader_delay=0; engine_latency=20; fail_job=0; check_sequence=1;
            cycles(4); resetn=1; cycles(3); enable=1; cycles(3);
        end
    endtask
    task no_fault;
        begin if(sticky_error || deadline_misses) $fatal(1,"unexpected fault in scenario %0d",scenario); end
    endtask
    task finish_frame;
        // Capture fence arrives after tick, as it does in the real HP0 path.
        begin tick(); cycles(5); capture(); cycles(PERIOD-6); end
    endtask
    integer n, saved_starts, saved_submissions, saved_resets;
    reg [31:0] saved_consumer, saved_sequence;
    initial begin
        // Empty initial ticks never arm a false deadline. Each option setting
        // then consumes one consecutive capture per output tick with no CPU.
        scenario=1; restart();
        repeat(3) begin tick(); cycles(PERIOD-1); end
        if(starts || sticky_error) $fatal(1,"empty startup armed cadence");
        capture();
        for(n=0;n<256;n=n+1) begin
            options=n[7:0]; engine_latency=12+(n%5)*17;
            reader_delay=n%9;
            finish_frame(); no_fault();
            if(starts!=n+1 || completions!=n+1 || submissions!=n+1 || activations!=n)
                $fatal(1,"scenario1 cadence n=%0d start=%0d complete=%0d submit=%0d active=%0d",n,starts,completions,submissions,activations);
        end
        if(history_resets!=256 || capture_consumer!=256) $fatal(1,"option/history ownership mismatch");
        // Unchanged options retain history; near-vblank completion still has
        // enough publication crossing time, and the ACK can drain over vblank.
        scenario=2; engine_latency=PERIOD-15; reader_delay=GRACE-4;
        finish_frame(); no_fault(); finish_frame(); no_fault();
        if(history_resets!=256) $fatal(1,"unchanged settings reset history");

        // Disable while DDR is owned: consumer does not advance early, the
        // completed result is discarded, and quiescence waits for reader drain.
        scenario=3; engine_latency=40; tick(); cycles(5);
        saved_consumer=capture_consumer; saved_submissions=submissions;
        hold_reader_drain=1; enable=0; cycles(8);
        if(capture_consumer!=saved_consumer || quiescent) $fatal(1,"disable released live ownership");
        cycles(50);
        if(submissions!=saved_submissions || quiescent) $fatal(1,"disable published or ignored reader drain");
        saved_sequence=dut.sequence_counter;
        hold_reader_drain=0; cycles(5);
        if(!quiescent || busy) $fatal(1,"drained scheduler did not quiesce");
        enable=1; cycles(3); capture(); engine_latency=20;
        check_sequence=0; tick(); cycles(50);
        if(!engine_history_reset || dut.sequence_counter!=saved_sequence+1)
            $fatal(1,"restart reused sequence or retained stale history");
        tick(); cycles(GRACE+3); no_fault();

        // Reader backpressure holds the complete immutable output. When it
        // releases before the deadline the frame still appears in order.
        scenario=4; restart(); capture(); reader_block=1;
        tick(); cycles(45);
        if(completions!=1 || submissions!=0 || !busy) $fatal(1,"did not hold completed result");
        reader_block=0; cycles(10);
        if(submissions!=1) $fatal(1,"held result was not published");
        capture(); tick(); cycles(40); no_fault();

        // A pending old-reader burst may delay acknowledgement after vblank,
        // but must not free its old active output slot early.
        scenario=5; restart(); reader_delay=GRACE-2; engine_latency=2; capture();
        repeat(5) begin finish_frame(); no_fault(); end

        // An engine exceeding the presentation budget is an explicit fault,
        // never a skipped producer counter or catch-up publication.
        scenario=6; restart(); capture(); engine_latency=PERIOD+GRACE+20;
        finish_frame(); tick(); cycles(GRACE+4);
        if(!sticky_error || deadline_misses!=1 || starts!=1) $fatal(1,"late engine was not a deadline fault");
        clear_errors=1; cycles(2);
        if(!sticky_error) $fatal(1,"cleared fault while enabled");
        enable=0; cycles(2);
        if(!sticky_error || quiescent || capture_consumer!=0)
            $fatal(1,"cleared fault or released capture before accepted engine drained");
        clear_errors=0; cycles(70);
        if(submissions || completions!=1 || capture_consumer!=capture_producer)
            $fatal(1,"late job published or disabled drain/rebase failed");
        enable=0; cycles(5); clear_errors=1; cycles(2); clear_errors=0;
        if(sticky_error || deadline_misses || !quiescent) $fatal(1,"quiet error clear failed");

        // Missing capture after warmup faults without reusing or skipping a
        // capture. No manufactured source frame is passed to the engine.
        scenario=7; restart(); capture(); tick(); cycles(PERIOD-1);
        tick(); cycles(PERIOD-1); tick(); cycles(3);
        if(!sticky_error || starts!=1 || capture_consumer!=1 || deadline_misses!=1)
            $fatal(1,"missing capture was hidden");

        // Reader ACK later than grace and engine failure each fault, with no
        // publication of a failed job. Transport faults also work while idle.
        scenario=8; restart(); reader_delay=GRACE+4; capture();
        finish_frame(); tick(); cycles(GRACE+4);
        if(!sticky_error || deadline_misses!=1) $fatal(1,"late reader ACK was hidden");
        scenario=9; restart(); fail_job=1; capture(); tick(); cycles(40);
        if(!sticky_error || submissions || capture_consumer!=1 || processed_frames)
            $fatal(1,"failed engine result escaped");
        scenario=10; restart(); transport_error=1; cycles(2);
        if(!sticky_error || starts) $fatal(1,"idle transport fault ignored");

        // Full ring faults at four outstanding captures; it never selects the
        // newest slot or lets producer wrap alias an unconsumed input.
        scenario=11; restart(); capture_producer=4; cycles(2);
        if(!sticky_error || capture_consumer || starts) $fatal(1,"full ring was silently dropped");

        // Both capture and publication sequence wrap are exercised without
        // changing ordering, using the same startup/restart ownership rules.
        scenario=12; restart(); enable=0; capture_producer=32'hfffffffe; cycles(4);
        dut.sequence_counter=30'h3ffffffe; last_submit_sequence=30'h3ffffffe;
        enable=1; cycles(3); capture();
        repeat(4) begin finish_frame(); no_fault(); end
        if(capture_consumer!=2 || dut.sequence_counter!=3 || last_submit_sequence!=3)
            $fatal(1,"counter wrap broke ordered ownership");

        // A live settings write never changes an accepted job. The following
        // job takes the new settings and resets history exactly once.
        scenario=13; restart(); capture(); tick(); cycles(5);
        options=8'hff;
        if(engine_options!=0) $fatal(1,"options changed inside accepted job");
        cycles(PERIOD-6); capture(); tick(); cycles(35);
        if(engine_options!=8'hff || history_resets!=2) $fatal(1,"next job did not reset for live options");
        cycles(PERIOD-36); capture(); tick(); cycles(35); no_fault();
        if(history_resets!=2) $fatal(1,"unchanged options repeatedly reset history");

        // Existing queued captures are consumed oldest-first one per tick;
        // no latest-frame selection or catch-up batch may collapse the queue.
        scenario=14; restart(); capture_producer=3;
        repeat(3) begin tick(); cycles(PERIOD-1); no_fault(); end
        if(starts!=3 || capture_consumer!=3 || activations!=2)
            $fatal(1,"queued captures were skipped or processed in a burst");
        $display("PASS ordered video scheduler: 14 scenarios, 256 options, fixed cadence, ownership/drain, deadline grace %0d, faults, wrap",GRACE);
        $finish;
    end
    initial begin #2000000; $fatal(1,"scheduler regression timed out"); end
endmodule
