// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_atari_xt_rom_boot #(parameter bit COMPY_320 = 0);
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

    task automatic expect_byte(input [15:0] address, input [7:0] data);
        emit(8'had); emit(address[7:0]); emit(address[15:8]); // LDA address
        emit(8'hc9); emit(data);                            // CMP #data
        emit(8'hf0); emit(8'h03);                           // BEQ past failure jump
        emit(8'h4c); emit(8'h00); emit(8'hcf);              // JMP $CF00
    endtask

    task automatic make_compy_rom;
        lda_sta(8'h91, 16'h4000);
        lda_sta(8'h92, 16'h5000);
        lda_sta(8'h93, 16'h7fff);
        for (integer bank = 0; bank < 16; bank++) begin
            lda_sta({bank[3:2], 2'b00, bank[1:0], 2'b11}, 16'hd301);
            lda_sta(8'ha0 | 8'(bank), 16'h4000);
            lda_sta(8'hb0 | 8'(bank), 16'h5000);
            lda_sta(8'hc0 | 8'(bank), 16'h7fff);
        end
        for (integer bank = 0; bank < 16; bank++) begin
            lda_sta({bank[3:2], 2'b10, bank[1:0], 2'b11}, 16'hd301);
            expect_byte(16'h4000, 8'ha0 | 8'(bank));
            expect_byte(16'h5000, 8'hb0 | 8'(bank));
            expect_byte(16'h7fff, 8'hc0 | 8'(bank));
        end
        lda_sta(8'hfd, 16'hd301); // Base RAM, OS and BASIC
        expect_byte(16'h4000, 8'h91);
        expect_byte(16'h5000, 8'h92);
        expect_byte(16'h7fff, 8'h93);

        // The pass color must come from live ANTIC DMA in bank 15 while
        // SALLY sees base RAM. A bank failure uses a different border color.
        lda_sta(8'h00, 16'h4000);
        lda_sta(8'hcf, 16'hd301);
        lda_sta(8'h55, 16'h4000); // Mode E pixels select COLPF0
        lda_sta(8'hfd, 16'hd301);
        emit(8'had); emit(8'h00); emit(8'ha0); // LDA streamed BASIC byte 0
        emit(8'h8d); emit(8'h16); emit(8'hd0); // STA COLPF0
        lda_sta(8'h4e, 16'h3000); lda_sta(8'h00, 16'h3001); lda_sta(8'h40, 16'h3002);
        lda_sta(8'h41, 16'h3003); lda_sta(8'h00, 16'h3004); lda_sta(8'h30, 16'h3005);
        lda_sta(8'hdf, 16'hd301); // ANTIC-only extension, bank 15
        expect_byte(16'h4000, 8'h00);
        lda_sta(8'h00, 16'hd402);
        lda_sta(8'h30, 16'hd403);
        lda_sta(8'h22, 16'hd400);
        emit(8'h4c); emit(8'h05); emit(8'hcf); // Park without altering colors

        cursor = 16'h0f00;
        lda_sta(8'h24, 16'hd01a);             // Failure border color
        emit(8'h4c); emit(8'h05); emit(8'hcf);
    endtask

    initial begin
        for (integer i = 0; i < 16384; i++) os_rom[i] = 8'hff;
        os_rom[16'h3ffc] = 8'h00;
        os_rom[16'h3ffd] = 8'hc0;
        emit(8'h78);                                  // SEI
        lda_sta(8'hff, 16'hd301);                      // PORTB directions
        lda_sta(8'h04, 16'hd303);                      // PORTB data mode
        lda_sta(8'hfd, 16'hd301);                      // Enable OS and BASIC
        if (COMPY_320) make_compy_rom();
        else begin
            emit(8'had); emit(8'h00); emit(8'ha0);      // LDA $A000 (BASIC byte 0)
            emit(8'h8d); emit(8'h1a); emit(8'hd0);      // STA $D01A (COLBK)
            emit(8'h4c); emit(8'h16); emit(8'hc0);      // JMP $C016
        end

        repeat (30) @(negedge clk);
        reset = 0;
        for (integer i = 0; i < 16384; i++) send_byte(8'h78, os_rom[i], i, 16384);
        for (integer i = 0; i < 8192; i++) send_byte(8'h79, i == 0 ? 8'h36 : 8'h55, i, 8192);
        @(negedge clk);
        load_valid = 0;
        if (rom_loaded !== 3'b101 || load_error !== 1'b0 || ready !== 1'b0)
            $fatal(1, "OS and BASIC did not stage while the guest was held in reset");
        guest_reset = 0;

        do begin
            @(negedge clk);
            if (COMPY_320 && video_valid === 1'b1 && rgb === 24'h9a2f00)
                $fatal(1, "Mapped Compy CPU probe saw aliased or unreadable RAM");
        end while (video_valid !== 1'b1 || rgb !== 24'hca415f);
        if (ready !== 1'b1 || load_error !== 1'b0) $fatal(1, "Unexpected ROM loader state after boot");
        if (COMPY_320) $display("PASS Atari-XT mapped Compy 16 CPU banks and independent ANTIC fetch");
        else $display("PASS Atari-XT booted streamed OS and displayed the first BASIC byte as COLBK");
        $finish;
    end

    initial begin
        #(COMPY_320 ? 22000000 : 1500000);
        $fatal(1, "Atari did not display the expected color after reset (Compy=%b)", COMPY_320);
    end
endmodule

module tb_atari_xt_compy_boot;
    tb_atari_xt_rom_boot #(.COMPY_320(1'b1)) testbench();
endmodule
