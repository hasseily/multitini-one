// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_zx_spectrum;
    reg clk = 0;
    always #5 clk = !clk;

    reg reset = 1, guest_reset = 0, clear_error = 0;
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
        .core_clk(clk), .reset(reset), .core_reset_request(guest_reset), .pause_request(1'b0),
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
        $display("PASS ZX Spectrum Z80, paging, raster, audio, input and TAP");
        $finish;
    end

    initial begin
        #100000000;
        $fatal(1, "ZX Spectrum test timeout");
    end
endmodule
