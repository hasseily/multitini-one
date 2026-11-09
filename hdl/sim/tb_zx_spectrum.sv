// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_zx_spectrum;
    reg clk = 0;
    always #5 clk = !clk;

    reg reset = 1, guest_reset = 0, clear_error = 0;
    reg pause_request = 0, stock_tape_rom = 0;
    reg [1:0] tape_speed = 0;
    reg [7:0] load_data = 0, load_kind = 0;
    reg load_valid = 0, load_first = 0, load_last = 0, load_epoch = 0;
    reg load_abort = 0, load_abort_epoch = 0;
    reg [31:0] load_offset = 0, load_total = 0;
    reg [15:0] input_code = 0, input_value = 0;
    reg input_valid = 0;
    wire ready, load_ready, load_error, video_valid, audio_valid;
    wire [2:0] rom_loaded;
    wire [11:0] video_x, video_y;
    wire signed [23:0] audio_l;
    multitini_zx_spectrum_adapter dut (
        .core_clk(clk), .reset(reset), .core_reset_request(guest_reset), .pause_request(pause_request),
        .tape_speed(tape_speed), .stock_tape_rom(stock_tape_rom),
        .clear_load_error(clear_error), .input_code(input_code), .input_value(input_value),
        .input_valid(input_valid), .input_ready(), .load_data(load_data), .load_kind(load_kind),
        .load_valid(load_valid), .load_first(load_first), .load_last(load_last),
        .load_epoch(load_epoch), .load_abort(load_abort), .load_abort_epoch(load_abort_epoch),
        .load_offset(load_offset), .load_total(load_total), .load_ready(load_ready),
        .ready(ready), .load_error(load_error), .rom_loaded(rom_loaded),
        .video_r(), .video_g(), .video_b(), .video_x(video_x), .video_y(video_y),
        .video_valid(video_valid), .audio_l(audio_l), .audio_r(), .audio_valid(audio_valid)
    );

    task automatic send_byte(input [7:0] kind, data, input integer offset, size);
        @(negedge clk);
        load_kind = kind;
        load_data = data;
        load_offset = offset;
        load_total = size;
        load_valid = 1;
        load_first = offset == 0;
        load_last = offset + 1 == size;
        @(posedge clk);
        if (!load_ready) $fatal(1, "loader did not consume byte");
        #1 load_valid = 0;
    endtask

    task automatic input_event(input [15:0] code, value);
        @(negedge clk);
        input_code = code;
        input_value = value;
        input_valid = 1;
        @(posedge clk);
        #1 input_valid = 0;
    endtask

    task automatic abort_stream;
        @(negedge clk);
        load_abort_epoch = load_epoch;
        load_abort = 1;
        @(negedge clk);
        load_abort = 0;
    endtask

    // Diagnostic program assembled from individual Z80 opcodes. It writes
    // the beeper/border, stores screen RAM, exercises #7FFD and writes AY R8.
    // The program is generated here and distributed with no Sinclair ROM.
    localparam [7:0] program_bytes [0:51] = '{
        8'hf3, 8'h31, 8'hff, 8'hbf, 8'h3e, 8'h12, 8'hd3, 8'hfe,
        8'h3e, 8'ha5, 8'h32, 8'h00, 8'h40, 8'h3e, 8'h42, 8'h32, 8'h00, 8'hc0,
        8'h01, 8'hfd, 8'h7f, 8'h3e, 8'h03, 8'hed, 8'h79,
        8'h3e, 8'h53, 8'h32, 8'h00, 8'hc0, 8'h3e, 8'h20, 8'hed, 8'h79,
        8'h3e, 8'h03, 8'hed, 8'h79,
        8'h01, 8'hfd, 8'hff, 8'h3e, 8'h08, 8'hed, 8'h79,
        8'h06, 8'hbf, 8'h3e, 8'h0f, 8'hed, 8'h79, 8'h76
    };

    task automatic boot(input integer size);
        for (integer i = 0; i < size; i++)
            send_byte(8'h70, i < 52 ? program_bytes[i] : 8'hff, i, size);
        if (load_error || !ready || !rom_loaded[0]) $fatal(1, "ROM did not commit");
        repeat (5000) @(negedge clk);
        if (dut.border !== 3'd2 || dut.speaker !== 1'b1 || dut.ram[17'h14000] !== 8'ha5)
            $fatal(1, "Z80 did not drive border, beeper and RAM");
        if (size == 32768) begin
            if (dut.ram[0] !== 8'h42 || dut.ram[17'h0c000] !== 8'h53 || dut.paging !== 6'h20)
                $fatal(1, "128K paging or lock failed");
            if (dut.ay_a !== 8'hff) $fatal(1, "AY register write did not reach the DAC");
        end else if (dut.ram[0] !== 8'h53 || dut.paging !== 0)
            $fatal(1, "48K exposed the 128K page latch");
        wait (audio_valid);
        if (!(audio_l > 0)) $fatal(1, "PCM output did not follow the beeper");
    endtask

    task automatic check_frame;
        do @(posedge clk); while (!(video_valid && video_x == 0 && video_y == 0));
        for (integer pixel = 1; pixel < 344 * 280; pixel++) begin
            do @(posedge clk); while (!video_valid);
            if (video_x != pixel % 344 || video_y != pixel / 344)
                $fatal(1, "raster discontinuity at %d (%d,%d)", pixel, video_x, video_y);
        end
    endtask

    // Source-built stand-in for LD-BYTES. The stock entry has a sentinel
    // here, and the direct entry calls the byte feeder twice before return.
    localparam [7:0] direct_program [0:42] = '{
        8'h31, 8'hff, 8'hbf, 8'h01, 8'hfd, 8'h7f, 8'h3e, 8'h10, 8'hed, 8'h79,
        8'hcd, 8'h62, 8'h05, 8'h32, 8'h00, 8'h80, 8'h3e, 8'h00, 8'hce, 8'h00, 8'h32, 8'h04, 8'h80,
        8'hcd, 8'h62, 8'h05, 8'h32, 8'h01, 8'h80, 8'h3e, 8'h00, 8'hce, 8'h00, 8'h32, 8'h05, 8'h80,
        8'hcd, 8'h62, 8'h05, 8'h32, 8'h02, 8'h80, 8'h76
    };
    localparam [7:0] direct_entry [0:12] = '{
        8'hcd, 8'hca, 8'h05, 8'hd0, 8'h7d, 8'h32, 8'h03, 8'h80,
        8'hcd, 8'hca, 8'h05, 8'hd0, 8'h7d
    };
    localparam [7:0] direct_tap [0:7] = '{8'h03, 8'h00, 8'h00, 8'h11, 8'h22, 8'h01, 8'h00, 8'h33};

    function automatic [7:0] direct_rom_byte(input [13:0] address, input bit basic_bank);
        if (address == 7 && !basic_bank) return 8'h00;
        if (address < 43) return direct_program[address];
        if (address >= 14'h059f && address < 14'h05ac) return direct_entry[address - 14'h059f];
        case (address)
            14'h0562: return 8'h3e; // LD A,EE; SCF; RET
            14'h0563: return 8'hee;
            14'h0564: return 8'h37;
            14'h0565: return 8'hc9;
            14'h05ac: return 8'hc9;
            14'h05d8: return 8'h37; // Successful stub byte returns with carry set
            14'h05d9: return 8'hc9;
            default: return 8'hff;
        endcase
    endfunction

    task automatic check_instant(input integer size, input bit stock, basic_bank, expected);
        @(negedge clk);
        pause_request = 1;
        stock_tape_rom = stock;
        tape_speed = 2;
        load_epoch = !load_epoch;
        for (integer i = 0; i < size; i++)
            send_byte(8'h70, direct_rom_byte(i[13:0], basic_bank), i, size);
        load_epoch = !load_epoch;
        for (integer i = 0; i < 8; i++) send_byte(8'h71, direct_tap[i], i, 8);
        if (load_error || !ready || rom_loaded[1:0] != 2'b11) $fatal(1, "Instant test media load failed");
        repeat (20) @(negedge clk);
        pause_request = 0;
        repeat (25000) @(negedge clk);
        if (dut.ram[17'h08002] !== 8'hee || dut.instant_active || dut.tape.playing || dut.fast_tape_active)
            $fatal(1, "Instant test did not return to the stock ROM");
        if (expected) begin
            if (dut.ram[17'h08000] !== 8'h11 || dut.ram[17'h08001] !== 8'h33 ||
                dut.ram[17'h08003] !== 8'h33 || dut.ram[17'h08004] !== 8'h01 ||
                dut.ram[17'h08005] !== 8'h00 || dut.tape.block_start !== 18'd8)
                $fatal(1, "Instant byte order, carry handling, or block cursor wrong");
        end else if (dut.ram[17'h08000] !== 8'hee || dut.ram[17'h08001] !== 8'hee ||
            dut.tape.block_start !== 18'd0) $fatal(1, "Instant patched an unrecognized ROM bank");
    endtask

    localparam [7:0] tzx_stream [0:48] = '{
        8'h06, 8'h01,                                        // Set high
        8'h01, 8'h01, 8'h00, 8'h02, 8'h00,                   // Two 1-T-state pulses
        8'h02, 8'h03, 8'h01, 8'h00, 8'h01, 8'h00, 8'h03, 8'h00, // Pulse sequence
        8'h03, 8'h02, 8'h00, 8'h03, 8'h00, 8'h02, 8'h01, 8'h00, 8'h00, 8'h80, // PWM 10
        8'h04, 8'h00, 8'h00,                                 // Stop without changing EAR
        8'h05, 8'h02, 8'h00, 8'h03, 8'h01, 8'h00, 8'h00, 8'hc0, // Direct samples 110
        8'h06, 8'h01, 8'h04, 8'h02, 8'h00,                   // Set high, 2 ms pause
        8'h07,                                              // Stop on 48K only
        8'h06, 8'h01, 8'h01, 8'h05, 8'h00, 8'h01, 8'h00     // High for 5 T-states
    };

    integer tzx_ticks = 0, last_tzx_tick;
    bit saw_tzx_underflow = 0, saw_normal_phase_before_stall = 0, saw_fast_resume = 0;
    always @(posedge clk) begin
        if (dut.tape_tzx && dut.tape_tick && dut.tape_playing && !pause_request)
            tzx_ticks <= tzx_ticks + 1;
        if (dut.tape_underflow) begin
            saw_tzx_underflow <= 1;
            if (dut.tape_tick || (dut.tape_tick_due && dut.cpu_p))
                $fatal(1, "TZX underflow advanced CPU or tape on a missed tick");
            if (!dut.fast_tape_active && !dut.tape_stalled && !dut.tape_tick_due && dut.ce_cpu_p) begin
                if (!dut.cpu_p) $fatal(1, "Normal TZX held CPU before the first missed tape tick");
                saw_normal_phase_before_stall <= 1;
            end
        end
        if (dut.tape_stalled && !dut.tape_tick && dut.cpu_p)
            $fatal(1, "TZX resumed CPU before the tape tick");
        if (dut.tape_stalled && dut.tape_tick && dut.fast_tape_active) begin
            if (!dut.cpu_p) $fatal(1, "Fast TZX did not resume CPU and tape together");
            saw_fast_resume <= 1;
        end
    end

    task automatic play_tape;
        input_event(16'h0005, 0);
        input_event(16'h0005, 1);
    endtask

    task automatic expect_tzx_event(input integer interval, input bit level, active);
        do @(posedge clk); while (!dut.tape.tzx_player.pop_event);
        if (interval >= 0 && tzx_ticks != last_tzx_tick + interval)
            $fatal(1, "TZX timing expected %0d T states, saw %0d", interval, tzx_ticks - last_tzx_tick);
        last_tzx_tick = tzx_ticks;
        #1;
        if (dut.tape_ear !== level || dut.tape_playing !== active)
            $fatal(1, "TZX level/play state expected %b/%b, saw %b/%b", level, active, dut.tape_ear, dut.tape_playing);
    endtask

    task automatic check_tzx(input bit fast, model_128);
        tape_speed = fast ? 2'd1 : 2'd2;
        stock_tape_rom = 1;
        load_epoch = !load_epoch;
        for (integer i = 0; i < 49; i++) send_byte(8'h72, tzx_stream[i], i, 49);
        if (load_error || !rom_loaded[1] || !dut.tape_tzx || dut.instant_available)
            $fatal(1, "TZX stream did not commit or exposed Instant TAP");
        repeat (24) @(negedge clk);
        if (dut.tape_playing || dut.tape_ear) $fatal(1, "Mounted TZX played without input");
        play_tape();
        expect_tzx_event(-1, 1, 1);
        if (dut.fast_tape_active !== fast) $fatal(1, "Wrong TZX playback speed");
        expect_tzx_event(1, 0, 1);
        expect_tzx_event(1, 1, 1);
        expect_tzx_event(1, 0, 1);
        expect_tzx_event(1, 1, 1);
        expect_tzx_event(3, 0, 1);
        expect_tzx_event(3, 1, 1);
        expect_tzx_event(3, 0, 1);
        expect_tzx_event(2, 1, 1);
        expect_tzx_event(2, 0, 0);
        repeat (24) @(negedge clk);
        if (dut.tape_playing || dut.tape_ear) $fatal(1, "TZX zero pause did not stop at current level");
        play_tape();
        expect_tzx_event(-1, 1, 1);
        expect_tzx_event(2, 1, 1);
        expect_tzx_event(2, 0, 1);
        expect_tzx_event(2, 1, 1);
        @(negedge dut.tape_ear);
        #1;
        if (tzx_ticks != last_tzx_tick + 3501) $fatal(1, "TZX pause did not hold EAR for 1 ms");
        if (model_128) expect_tzx_event(7000, 1, 1);
        else begin
            expect_tzx_event(7000, 0, 0);
            play_tape();
            expect_tzx_event(-1, 1, 1);
        end
        expect_tzx_event(5, 0, 0);
        play_tape();
        expect_tzx_event(-1, 1, 1);
        input_event(16'h0006, 0);
        input_event(16'h0006, 1);
        repeat (3) @(negedge clk);
        if (dut.tape_ear || dut.tape_playing || dut.tape.tzx_player.at_end)
            $fatal(1, "TZX rewind did not reset the transport");
    endtask

    task automatic check_tzx_underflow(input bit fast);
        // A chain of zero-time commands exceeds the initial prefetch lead.
        // Playback must still see two adjacent one-T-state pulses.
        localparam integer tape_length = 5 + 128 + 5;
        localparam [7:0] tone [0:4] = '{8'h01, 8'h01, 8'h00, 8'h01, 8'h00};
        tape_speed = fast ? 2'd1 : 2'd0;
        load_epoch = !load_epoch;
        for (integer i = 0; i < tape_length; i++) begin
            if (i < 5) send_byte(8'h72, tone[i], i, tape_length);
            else if (i < 133) send_byte(8'h72, i[0] ? 8'h06 : 8'h01, i, tape_length);
            else send_byte(8'h72, tone[i - 133], i, tape_length);
        end
        @(negedge clk);
        saw_tzx_underflow = 0;
        saw_normal_phase_before_stall = 0;
        saw_fast_resume = 0;
        play_tape();
        expect_tzx_event(-1, 0, 1);
        expect_tzx_event(1, 1, 1);
        expect_tzx_event(1, 0, 0);
        if (!saw_tzx_underflow) $fatal(1, "TZX underflow path was not exercised");
        if (fast ? !saw_fast_resume : !saw_normal_phase_before_stall)
            $fatal(1, "TZX did not exercise the expected CPU phase around a stall");
    endtask

    initial begin
        repeat (20) @(negedge clk);
        reset = 0;
        boot(16384);
        check_frame();

        // An abort of a new stream may precede its first byte and uses the
        // same epoch as the last successful ROM. It must preserve that ROM.
        abort_stream();
        send_byte(8'h71, 2, 0, 4);
        if (!ready || rom_loaded[1] || load_error) $fatal(1, "stale stream disrupted boot");
        load_epoch = 1;
        send_byte(8'h71, 2, 0, 4);
        send_byte(8'h71, 0, 1, 4);
        send_byte(8'h71, 8'hff, 2, 4);
        send_byte(8'h71, 8'hff, 3, 4);
        if (!rom_loaded[1] || load_error) $fatal(1, "valid TAP did not commit");
        repeat (30) @(negedge clk);
        if (dut.tape.playing || dut.tape_ear) $fatal(1, "new tape started without input");
        input_event(16'h0005, 1); // F1 play
        @(posedge dut.tape_ear);
        @(negedge dut.tape_ear);
        begin
            time start_time;
            start_time = $time;
            @(posedge dut.tape_ear);
            if ($time - start_time != 2168 * 16 * 10) $fatal(1, "TAP pilot period wrong");
        end
        input_event(16'h0005, 1); // Repeat make must not pause tape
        repeat (3) @(negedge clk);
        if (!dut.tape.playing) $fatal(1, "repeat F1 make toggled tape");
        input_event(16'h0005, 0);
        input_event(16'h0005, 1); // Pause
        repeat (3) @(negedge clk);
        if (dut.tape.playing) $fatal(1, "F1 did not pause");
        input_event(16'h0006, 1); // F2 rewind
        repeat (3) @(negedge clk);
        if (dut.tape_ear || dut.tape.address != 0) $fatal(1, "F2 did not rewind");

        tape_speed = 1;
        input_event(16'h0005, 0);
        input_event(16'h0005, 1);
        wait (dut.fast_tape_active);
        @(posedge dut.tape_ear);
        @(negedge dut.tape_ear);
        begin
            time start_time;
            start_time = $time;
            @(posedge dut.tape_ear);
            if ($time - start_time != 2168 * 2 * 10) $fatal(1, "Fast TAP pilot period wrong");
        end
        input_event(16'h0005, 0);
        input_event(16'h0005, 1);
        wait (!dut.fast_tape_active);
        if (dut.tape.playing) $fatal(1, "Fast TAP did not stop on F1 pause");
        input_event(16'h0006, 0);
        input_event(16'h0006, 1);
        tape_speed = 0;

        input_event(16'h0103, 1); // right, player one
        input_event(16'h0204, 1); // fire, player two
        if (dut.joystick !== 5'b10001) $fatal(1, "Kempston mapping wrong");
        input_event(16'h0052, 1); // PC apostrophe generates SYMBOL SHIFT+P
        if (!dut.keyboard.rows[5][0] || !dut.keyboard.rows[7][1]) $fatal(1, "quote chord not held");
        input_event(16'h0052, 0);
        if (dut.keyboard.rows[5][0] || dut.keyboard.rows[7][1]) $fatal(1, "quote chord stuck");

        @(negedge clk);
        guest_reset = 1;
        repeat (3) @(negedge clk);
        if (ready || !rom_loaded[1] || dut.joystick != 0) $fatal(1, "guest reset did not quiesce");
        guest_reset = 0;
        send_byte(8'h71, 8, 0, 4);
        send_byte(8'h71, 0, 1, 4);
        if (rom_loaded[1] || !load_error || !ready) $fatal(1, "truncated TAP exposed playable data");
        @(negedge clk);
        clear_error = 1;
        @(negedge clk);
        clear_error = 0;
        boot(32768);
        check_frame();
        check_tzx(1'b1, 1'b1);
        check_instant(16384, 1'b1, 1'b0, 1'b1);
        check_instant(32768, 1'b1, 1'b1, 1'b1);
        check_instant(32768, 1'b1, 1'b0, 1'b0);
        check_instant(16384, 1'b0, 1'b0, 1'b0);
        check_tzx(1'b0, 1'b0);
        check_tzx_underflow(1'b0);
        check_tzx_underflow(1'b1);
        $display("PASS ZX Spectrum Z80, paging, raster, audio, input and TAP");
        $finish;
    end

    initial begin
        #100000000;
        $fatal(1, "ZX Spectrum test timeout");
    end
endmodule
