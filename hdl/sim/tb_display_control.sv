`timescale 1ns/1ps
module tb_display_control;
    import multitini_display_pkg::*;
    logic clk = 0;
    always #5 clk = ~clk;
    logic resetn = 0, request = 0, geometry_ready=1;
    logic [3:0] requested_mode = 4;
    logic quiescent = 0, frame_latched = 0, locked = 1;
    wire [3:0] active_mode;
    wire commit, busy, error, pause_reader, hold_video, clock_resetn;
    wire [10:0] awaddr;
    wire awvalid, wvalid, bready;
    wire [31:0] wdata;
    logic awready = 0, wready = 0, bvalid = 0;
    logic [1:0] bresp = 0;
    multitini_display_control #(.TIMEOUT_CYCLES(512)) dut (
        .clk(clk), .resetn(resetn), .request(request),
        .requested_mode(requested_mode), .geometry_ready(geometry_ready),
        .reader_quiescent(quiescent), .frame_latched(frame_latched),
        .clock_locked(locked), .active_mode(active_mode),
        .commit(commit), .busy(busy),
        .error(error), .reader_pause(pause_reader), .video_hold(hold_video),
        .clock_resetn(clock_resetn), .clock_awaddr(awaddr), .clock_awvalid(awvalid),
        .clock_awready(awready), .clock_wdata(wdata), .clock_wvalid(wvalid),
        .clock_wready(wready), .clock_bresp(bresp), .clock_bvalid(bvalid),
        .clock_bready(bready)
    );
    int cycle = 0, writes = 0, lock_timer = 0, register_index = 0;
    int reset_events = 0;
    logic aw_seen = 0, w_seen = 0, stall_clock = 0, stall_aw = 0;
    logic inject_bresp_error = 0;
    logic [10:0] saved_addr;
    logic [31:0] saved_data;
    logic [31:0] staged_feedback, staged_output;
    // Like the real MMCM, the running DRP configuration survives reset.
    logic [31:0] running_feedback = video_clock_feedback(VIDEO_DEFAULT_MODE);
    logic [31:0] running_output = video_clock_output(VIDEO_DEFAULT_MODE);
    real expected_clock[VIDEO_MODE_COUNT];
    initial begin
        expected_clock[0]=148.5;expected_clock[1]=65.0;expected_clock[2]=67.5;
        expected_clock[3]=108.0;expected_clock[4]=85.5;expected_clock[5]=119.0;
    end
    real running_clock;
    always @(negedge clock_resetn) reset_events++;
    always @(negedge clk) if (commit) begin
        running_clock = 150.0 * (real'(running_feedback[15:8]) +
            real'(running_feedback[25:16]) / 1000.0) /
            real'(running_feedback[7:0]) /
            (real'(running_output[7:0]) + real'(running_output[17:8]) / 1000.0);
        if (running_clock < expected_clock[active_mode] - 0.001 ||
            running_clock > expected_clock[active_mode] + 0.001)
            $fatal(1, "committed mode %0d at wrong pixel clock %f", active_mode, running_clock);
    end
    always @(negedge clk) begin
        cycle++;
        awready = !stall_clock && !stall_aw && cycle % 3 == 0;
        wready = !stall_clock && cycle % 3 == 1;
    end
    always @(posedge clk) begin
        if (!clock_resetn) begin
            aw_seen <= 0; w_seen <= 0; bvalid <= 0;
            locked <= 0;
            lock_timer <= 8;
            register_index <= 0;
            bresp <= 0;
        end else begin
            if (awvalid && awready) begin aw_seen <= 1; saved_addr <= awaddr; end
            if (wvalid && wready) begin w_seen <= 1; saved_data <= wdata; end
            if (bvalid && bready) bvalid <= 0;
            if (aw_seen && w_seen && !bvalid) begin
                if (saved_addr != 11'h200 + register_index * 4)
                    $fatal(1, "clock register ordering/duplicate write");
                if (saved_addr == 11'h200) staged_feedback <= saved_data;
                if (saved_addr == 11'h208) staged_output <= saved_data;
                if (saved_addr == 11'h25c) begin
                    if (saved_data != 3) $fatal(1, "clock LOAD value");
                    locked <= 0;
                    lock_timer <= 12;
                    running_feedback <= staged_feedback;
                    running_output <= staged_output;
                end
                writes++;
                register_index <= register_index == 23 ? 0 : register_index + 1;
                bresp <= inject_bresp_error ? 2'b10 : 2'b00;
                bvalid <= 1; aw_seen <= 0; w_seen <= 0;
            end
            if (lock_timer != 0) begin
                lock_timer <= lock_timer - 1;
                if (lock_timer == 1) locked <= 1;
            end
        end
    end
    task automatic start_mode(input logic [3:0] id);
        @(negedge clk); requested_mode = id; request = 1;
        @(negedge clk); request = 0;
    endtask
    task automatic finish_frame;
        wait (!hold_video);
        repeat (5) @(negedge clk);
        frame_latched = 1;
        @(negedge clk); frame_latched = 0;
        wait (!busy);
    endtask
    int failure_writes, failure_resets;
    initial begin
        repeat (4) @(negedge clk);
        resetn = 1;
        wait (locked);
        for (int m = 0; m < VIDEO_MODE_COUNT; m++) begin
            quiescent = 0;
            start_mode(4'(m));
            repeat (12) @(negedge clk);
            if (!busy || !pause_reader || hold_video || awvalid)
                $fatal(1, "must drain before touching clock");
            quiescent = 1;
            wait (commit);
            if (active_mode != m || !locked)
                $fatal(1, "mode/frame commit requires relock");
            @(negedge clk); geometry_ready=0;
            repeat(40) @(negedge clk);
            if(!hold_video) $fatal(1,"released before geometry acknowledgement");
            geometry_ready=1;
            finish_frame();
            if (error || writes != (m + 1) * 24) $fatal(1, "mode apply failed");
        end
        start_mode(15);
        repeat (4) @(negedge clk);
        if (busy || !error || active_mode != VIDEO_MODE_COUNT - 1)
            $fatal(1, "invalid mode changed scanout");
        stall_clock = 1;
        start_mode(0);
        wait (!clock_resetn);
        stall_clock = 0;
        wait (commit);
        if (active_mode != VIDEO_DEFAULT_MODE || !error)
            $fatal(1, "timeout did not recover default clock");
        finish_frame();
        // Fail after the low pixel clock is already running. Reset alone
        // cannot restore 1080p: recovery must write the default dividers.
        start_mode(0);
        wait (commit);
        wait (!commit);
        wait (!clock_resetn);
        wait (commit);
        if (active_mode != VIDEO_DEFAULT_MODE || !error ||
            running_feedback != video_clock_feedback(VIDEO_DEFAULT_MODE) ||
            running_output != video_clock_output(VIDEO_DEFAULT_MODE))
            $fatal(1, "late failure did not reprogram the default clock");
        finish_frame();
        // Both the requested config and its fallback return SLVERR.
        // Permit one reset/retry, then stop in held FAILED without looping.
        failure_writes = writes;
        failure_resets = reset_events;
        inject_bresp_error = 1;
        start_mode(2);
        wait (busy);
        wait (!busy);
        if (!error || !hold_video || !pause_reader ||
            writes - failure_writes != 2 || reset_events - failure_resets != 1)
            $fatal(1, "repeated BRESP error did not terminate after one fallback");
        repeat (1100) @(negedge clk);
        if (busy || !hold_video || writes - failure_writes != 2 ||
            reset_events - failure_resets != 1)
            $fatal(1, "terminal fallback failure retried without a new request");
        inject_bresp_error = 0;
        start_mode(2);
        wait (commit);
        if (active_mode != 2 || error)
            $fatal(1, "new request cannot recover from held failure");
        finish_frame();
        stall_aw = 1;
        start_mode(0);
        wait (w_seen && awvalid && !wvalid);
        wait (!busy);
        if (!error || !hold_video || !w_seen || !awvalid)
            $fatal(1, "half-accepted clock write did not reach held timeout");
        start_mode(1);
        wait (!clock_resetn);
        stall_aw = 0;
        wait (commit);
        if (active_mode != 1 || error)
            $fatal(1, "stale AXI half-write leaked into new request");
        finish_frame();
        // A DDR owner that never drains must retain its pixel clock even
        // after both timeouts; reset must not strand its handshake.
        quiescent=0;
        failure_writes=writes;
        start_mode(3);
        wait(busy);wait(!busy);
        if(!error || !pause_reader || hold_video || writes!=failure_writes)
            $fatal(1,"drain timeout reset a live DDR owner");
        quiescent=1;
        start_mode(0);
        wait(commit);
        finish_frame();
        $display("VIDEO MODE CONTROL PASS");
        $finish;
    end
    initial begin
        #300000;
        $fatal(1, "mode controller test timeout");
    end
endmodule
