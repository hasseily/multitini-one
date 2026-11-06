// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_atari_xt;
    reg clk = 0;
    always #5 clk = !clk;

    reg reset = 1, guest_reset = 1;
    reg [7:0] load_data = 0, load_kind = 0;
    reg load_valid = 0, load_first = 0, load_last = 0, load_epoch = 0;
    reg load_abort = 0, load_abort_epoch = 0, clear_error = 0;
    reg [31:0] load_offset = 0, load_total = 0;
    reg [15:0] input_code = 0, input_value = 0;
    reg input_valid = 0;
    wire ready, load_ready, load_error, video_valid, audio_valid;
    wire [2:0] rom_loaded;
    wire [11:0] video_x, video_y;
    wire [7:0] red, green, blue;
    wire signed [23:0] audio_l, audio_r;
    multitini_atari_xt_adapter dut (
        .core_clk(clk), .reset(reset), .core_reset_request(guest_reset), .pause_request(1'b0),
        .clear_load_error(clear_error), .input_code(input_code), .input_value(input_value),
        .input_valid(input_valid), .input_ready(), .load_data(load_data), .load_kind(load_kind),
        .load_valid(load_valid), .load_first(load_first), .load_last(load_last),
        .load_epoch(load_epoch), .load_offset(load_offset), .load_total(load_total),
        .load_abort(load_abort), .load_abort_epoch(load_abort_epoch), .load_ready(load_ready),
        .ready(ready), .load_error(load_error), .rom_loaded(rom_loaded),
        .sio_reset(), .sio_event_valid(), .sio_event_data(), .sio_event_ready(1'b1),
        .sio_reply_valid(1'b0), .sio_reply_data(17'd0), .sio_reply_ready(),
        .video_r(red), .video_g(green), .video_b(blue), .video_x(video_x), .video_y(video_y),
        .video_valid(video_valid), .audio_l(audio_l), .audio_r(audio_r), .audio_valid(audio_valid)
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
        if (!load_ready) $fatal(1, "Atari loader did not consume byte");
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

    // Source-built 6502 diagnostic ROM. No Atari system ROM is needed.
    reg [7:0] os_rom [0:16383];
    integer cursor = 0;
    task automatic emit(input [7:0] value);
        os_rom[cursor] = value;
        cursor++;
    endtask

    task automatic lda_sta(input [7:0] data, input [15:0] address);
        emit(8'ha9); emit(data);
        emit(8'h8d); emit(address[7:0]); emit(address[15:8]);
    endtask

    task automatic copy_byte(input [15:0] src, dst);
        emit(8'had); emit(src[7:0]); emit(src[15:8]);
        emit(8'h8d); emit(dst[7:0]); emit(dst[15:8]);
    endtask

    task automatic make_os;
        integer loop_offset;
        for (integer i = 0; i < 16384; i++) os_rom[i] = 8'hff;
        os_rom[16'h1000] = 8'hb9;
        os_rom[16'h3ffc] = 8'h00;
        os_rom[16'h3ffd] = 8'hc0;
        emit(8'h78);                      // SEI
        lda_sta(8'hff, 16'hd301);         // PORTB directions
        lda_sta(8'h04, 16'hd303);         // PORTB data mode
        lda_sta(8'h7d, 16'hd301);         // Enable OS, BASIC, self-test
        copy_byte(16'h5000, 16'h0201);
        lda_sta(8'hfd, 16'hd301);         // Disable self-test
        lda_sta(8'h82, 16'h8000);         // RAM under the lower cartridge window
        lda_sta(8'h04, 16'hd302);         // PORTA data mode
        lda_sta(8'h42, 16'h3000);         // Display list, mode 2 + LMS
        lda_sta(8'h00, 16'h3001);
        lda_sta(8'h40, 16'h3002);         // Character data at $4000
        lda_sta(8'h41, 16'h3003);         // Jump and wait for vertical blank
        lda_sta(8'h00, 16'h3004);
        lda_sta(8'h30, 16'h3005);
        lda_sta(8'h00, 16'h4000);
        lda_sta(8'h00, 16'hd402);
        lda_sta(8'h30, 16'hd403);
        lda_sta(8'he0, 16'hd409);         // Glyph bytes live in the OS ROM
        lda_sta(8'h36, 16'hd01a);         // COLBK
        lda_sta(8'h1f, 16'hd201);         // POKEY channel 1 volume-only DAC
        lda_sta(8'h03, 16'hd20f);         // Enable POKEY keyboard scanning
        lda_sta(8'h22, 16'hd400);         // Enable ANTIC display list DMA
        loop_offset = cursor;
        copy_byte(16'ha000, 16'h0200);
        copy_byte(16'hd300, 16'h0202);
        copy_byte(16'hd010, 16'h0203);
        copy_byte(16'hd01f, 16'h0204);
        copy_byte(16'hd209, 16'h0205);
        copy_byte(16'hd20f, 16'h0206);
        copy_byte(16'hd013, 16'h0207);
        copy_byte(16'h8000, 16'h0208);
        emit(8'h4c); emit(loop_offset[7:0]); emit(8'hc0);
    endtask

    reg saw_antic_rom = 0;
    reg saw_color = 0;
    reg saw_pcm = 0;
    always @(posedge clk) begin
        if (ready && dut.antic_rom_hit && dut.antic_addr[15:8] == 8'he0) saw_antic_rom <= 1;
        if (video_valid && {red, green, blue} != 0) saw_color <= 1;
        if (audio_valid && audio_l > 0 && audio_l == audio_r) saw_pcm <= 1;
    end

    task automatic check_frame;
        do @(posedge clk); while (!(video_valid && video_x == 0 && video_y == 0));
        for (integer index = 1; index < 384 * 240; index++) begin
            do @(posedge clk); while (!video_valid);
            if (video_x != index % 384 || video_y != index / 384)
                $fatal(1, "Atari raster discontinuity at %d (%d,%d)", index, video_x, video_y);
        end
    endtask

    initial begin
        make_os();
        repeat (20) @(negedge clk);
        reset = 0;
        for (integer i = 0; i < 16384; i++) send_byte(8'h78, os_rom[i], i, 16384);
        if (rom_loaded !== 3'b001 || ready || load_error) $fatal(1, "OS did not stage");
        for (integer i = 0; i < 8192; i++) send_byte(8'h79, 8'ha5, i, 8192);
        if (rom_loaded !== 3'b101 || ready || load_error) $fatal(1, "BASIC did not stage");
        @(negedge clk);
        guest_reset = 0;
        repeat (50000) @(negedge clk);
        if (!ready || dut.ram[16'h0200] !== 8'ha5 || dut.ram[16'h0201] !== 8'hb9)
            $fatal(1, "SALLY did not boot OS and read BASIC/self-test ROM (PC=%h A000=%h 5000=%h)",
                   dut.machine.dbg_pc, dut.ram[16'h0200], dut.ram[16'h0201]);
        $display("Atari-XT synthetic OS reset vector, BASIC and self-test mapping PASS");

        input_event(16'h0102, 1); // Left, player 1
        input_event(16'h0201, 1); // Down, player 2
        input_event(16'h0104, 1); // Fire, player 1
        input_event(16'h0005, 1); // START
        input_event(16'h0012, 1); // Left Shift
        input_event(16'h001c, 1); // A
        repeat (20000) @(negedge clk);
        if (dut.ram[16'h0202] !== 8'hdb || dut.ram[16'h0203] !== 8'h00 ||
            dut.ram[16'h0204] !== 8'h06 || dut.ram[16'h0205] !== 8'h7f || dut.ram[16'h0206][2] !== 1'b0)
            $fatal(1, "Atari input not observed (PORTA=%h TRIG0=%h CONSOL=%h KBCODE=%h SKSTAT=%h)",
                   dut.ram[16'h0202], dut.ram[16'h0203], dut.ram[16'h0204], dut.ram[16'h0205], dut.ram[16'h0206]);
        input_event(16'h001c, 0);
        repeat (8000) @(negedge clk);
        if (dut.ram[16'h0206][2] !== 1'b1) $fatal(1, "POKEY key remained held");

        check_frame();
        if (!saw_antic_rom || !saw_color || !saw_pcm)
            $fatal(1, "Atari video or audio output missing ANTIC ROM=%b RGB=%b PCM=%b",
                   saw_antic_rom, saw_color, saw_pcm);

        @(negedge clk);
        guest_reset = 1;
        abort_stream(); // No queued byte may disturb already committed ROMs.
        send_byte(8'h7a, 8'hc1, 0, 8192);
        if (rom_loaded !== 3'b101 || load_error) $fatal(1, "stale stream disrupted boot ROMs");
        load_epoch = 1;
        send_byte(8'h7a, 8'hc1, 0, 8192);
        abort_stream();
        @(negedge clk);
        guest_reset = 0;
        repeat (4) @(negedge clk);
        if (ready || rom_loaded !== 3'b101 || !load_error) $fatal(1, "partial cart did not hold reset");

        guest_reset = 1;
        clear_error = 1;
        @(negedge clk);
        clear_error = 0;
        load_epoch = 0;
        for (integer i = 0; i < 16384; i++) send_byte(8'h7a, i < 8192 ? 8'h81 : 8'ha1, i, 16384);
        @(negedge clk);
        guest_reset = 0;
        repeat (50000) @(negedge clk);
        if (!ready || load_error || rom_loaded !== 3'b111 || dut.ram[16'h0200] !== 8'ha1 ||
            dut.ram[16'h0207] !== 8'h01 || dut.ram[16'h0208] !== 8'h81)
            $fatal(1, "16K cart map or presence pin incorrect");
        guest_reset = 1;
        send_byte(8'h7b, 0, 0, 1);
        @(negedge clk);
        guest_reset = 0;
        repeat (50000) @(negedge clk);
        if (!ready || load_error || rom_loaded !== 3'b101 || dut.ram[16'h0200] !== 8'ha5 || dut.ram[16'h0207] !== 8'h00)
            $fatal(1, "eject did not restore BASIC and release presence pin");
        guest_reset = 1;
        for (integer i = 0; i < 8192; i++) send_byte(8'h7a, 8'ha8, i, 8192);
        @(negedge clk);
        guest_reset = 0;
        repeat (50000) @(negedge clk);
        if (!ready || load_error || rom_loaded !== 3'b111 || dut.ram[16'h0200] !== 8'ha8 ||
            dut.ram[16'h0207] !== 8'h01 || dut.ram[16'h0208] !== 8'h82)
            $fatal(1, "8K cart covered RAM below $A000 or failed to override BASIC");
        $display("PASS Atari-XT SALLY, ANTIC2, POKEY, PIA, input, and cartridges");
        $finish;
    end

    initial begin
        #40000000;
        $fatal(1, "Atari-XT test timeout");
    end
endmodule
