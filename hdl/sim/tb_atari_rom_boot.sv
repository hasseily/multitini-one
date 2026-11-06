// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_atari_xt_rom_boot;
    reg clk = 0;
    always #5 clk = !clk;

    reg reset = 1, guest_reset = 1;
    reg [7:0] load_data = 0, load_kind = 0;
    reg [31:0] load_offset = 0, load_total = 0;
    reg load_valid = 0, load_first = 0, load_last = 0;
    wire load_ready, load_error, ready, video_valid;
    wire [2:0] rom_loaded;
    wire [23:0] rgb;

    multitini_atari_xt_adapter dut (
        .core_clk(clk), .reset(reset), .core_reset_request(guest_reset), .pause_request(1'b0),
        .clear_load_error(1'b0), .input_code(16'd0), .input_value(16'd0), .input_valid(1'b0),
        .input_ready(), .load_data(load_data), .load_kind(load_kind), .load_valid(load_valid),
        .load_first(load_first), .load_last(load_last), .load_epoch(1'b0), .load_offset(load_offset),
        .load_total(load_total), .load_abort(1'b0), .load_abort_epoch(1'b0), .load_ready(load_ready),
        .ready(ready), .load_error(load_error), .rom_loaded(rom_loaded),
        .sio_reset(), .sio_event_valid(), .sio_event_data(), .sio_event_ready(1'b1),
        .sio_reply_valid(1'b0), .sio_reply_data(17'd0), .sio_reply_ready(),
        .video_r(rgb[23:16]), .video_g(rgb[15:8]), .video_b(rgb[7:0]), .video_x(), .video_y(),
        .video_valid(video_valid), .audio_l(), .audio_r(), .audio_valid()
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
        if (load_ready !== 1'b1) $fatal(1, "ROM loader did not consume byte %0d", offset);
    endtask

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

    initial begin
        for (integer i = 0; i < 16384; i++) os_rom[i] = 8'hff;
        os_rom[16'h3ffc] = 8'h00;
        os_rom[16'h3ffd] = 8'hc0;
        emit(8'h78);                                  // SEI
        lda_sta(8'hff, 16'hd301);                      // PORTB directions
        lda_sta(8'h04, 16'hd303);                      // PORTB data mode
        lda_sta(8'hfd, 16'hd301);                      // Enable OS and BASIC
        emit(8'had); emit(8'h00); emit(8'ha0);          // LDA $A000 (BASIC byte 0)
        emit(8'h8d); emit(8'h1a); emit(8'hd0);          // STA $D01A (COLBK)
        emit(8'h4c); emit(8'h16); emit(8'hc0);          // JMP $C016

        repeat (30) @(negedge clk);
        reset = 0;
        for (integer i = 0; i < 16384; i++) send_byte(8'h78, os_rom[i], i, 16384);
        for (integer i = 0; i < 8192; i++) send_byte(8'h79, i == 0 ? 8'h36 : 8'h55, i, 8192);
        @(negedge clk);
        load_valid = 0;
        if (rom_loaded !== 3'b101 || load_error !== 1'b0 || ready !== 1'b0)
            $fatal(1, "OS and BASIC did not stage while the guest was held in reset");
        guest_reset = 0;

        do @(negedge clk); while (video_valid !== 1'b1 || rgb !== 24'hca415f);
        if (ready !== 1'b1 || load_error !== 1'b0) $fatal(1, "Unexpected ROM loader state after boot");
        $display("PASS Atari-XT booted streamed OS and displayed the first BASIC byte as COLBK");
        $finish;
    end

    initial begin
        #1500000;
        $fatal(1, "Atari did not display the streamed BASIC color after reset");
    end
endmodule
