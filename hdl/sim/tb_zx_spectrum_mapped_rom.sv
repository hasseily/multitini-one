// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module tb_zx_spectrum_mapped_rom;
    reg clk = 0;
    always #8.929 clk = !clk;

    reg reset = 1, guest_reset = 1;
    reg load_valid = 0, load_first = 0, load_last = 0;
    reg [7:0] load_data = 0;
    reg [31:0] load_offset = 0, load_total = 0;
    wire load_ready, ready, load_error, video_valid;
    wire [2:0] rom_loaded;
    wire [7:0] red, green, blue;

    multitini_zx_spectrum_adapter dut (
        .core_clk(clk), .reset(reset), .core_reset_request(guest_reset), .pause_request(1'b0),
        .tape_speed(2'd0), .stock_tape_rom(1'b0),
        .clear_load_error(1'b0), .input_code(16'd0), .input_value(16'd0),
        .input_valid(1'b0), .input_ready(), .load_data(load_data), .load_kind(8'h70),
        .load_valid(load_valid), .load_first(load_first), .load_last(load_last),
        .load_epoch(1'b0), .load_abort(1'b0), .load_abort_epoch(1'b0),
        .load_offset(load_offset), .load_total(load_total), .load_ready(load_ready),
        .ready(ready), .load_error(load_error), .rom_loaded(rom_loaded),
        .video_r(red), .video_g(green), .video_b(blue), .video_x(), .video_y(),
        .video_valid(video_valid), .audio_l(), .audio_r(), .audio_valid()
    );

    // DI, red border, select ROM 1 on a 128K machine, then loop in ROM 0.
    localparam [7:0] bank0 [0:13] = '{
        8'hf3, 8'h3e, 8'h02, 8'hd3, 8'hfe,
        8'h01, 8'hfd, 8'h7f, 8'h3e, 8'h10, 8'hed, 8'h79, 8'h18, 8'hfe
    };
    // ROM 1 continues at PC $000C and jumps to its far end for a green border.
    localparam [7:0] jump [0:2] = '{8'hc3, 8'h2c, 8'h3f};
    localparam [7:0] bank1 [0:5] = '{8'h3e, 8'h04, 8'hd3, 8'hfe, 8'h18, 8'hfe};

    function automatic [7:0] rom_byte(input integer offset);
        if (offset < 14) return bank0[offset];
        if (offset >= 'h400c && offset < 'h400f) return jump[offset - 'h400c];
        if (offset >= 'h7f2c && offset < 'h7f32) return bank1[offset - 'h7f2c];
        return 8'hff;
    endfunction

    task automatic boot(input integer size, input [23:0] expected_color);
        @(negedge clk);
        guest_reset = 1;
        load_total = size;
        for (integer i = 0; i < size; i++) begin
            @(negedge clk);
            load_valid = 1;
            load_first = i == 0;
            load_last = i == size - 1;
            load_offset = i;
            load_data = rom_byte(i);
            @(posedge clk);
            if (!load_ready) $fatal(1, "mapped loader stalled at ROM offset %h", i);
        end
        @(negedge clk);
        load_valid = 0;
        if (load_error || ready || rom_loaded !== 3'b001)
            $fatal(1, "mapped %0d-byte ROM did not commit while Z80 was held", size);
        guest_reset = 0;
        repeat (12000) begin
            @(negedge clk);
            if (video_valid === 1'b1 && {red, green, blue} === expected_color) return;
        end
        $fatal(1, "mapped %0d-byte ROM did not execute code for RGB %h", size, expected_color);
    endtask

    initial begin
        repeat (20) @(negedge clk);
        reset = 0;
        boot(16384, 24'hc00000);
        boot(32768, 24'h00c000);
        $display("PASS mapped Spectrum ROM writes and CPU reads in both banks");
        $finish;
    end
endmodule
