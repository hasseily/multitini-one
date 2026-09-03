// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_g64_track;
    import multitini_c64_defs_pkg::*;

    logic clk = 1'b0;
    logic reset = 1'b1;
    logic soft_reset = 1'b0;
    logic clear_error = 1'b0;
    logic service_enable = 1'b1;
    logic [6:0] head_half_track = 7'd34;
    logic media_mount = 1'b0;
    logic media_eject = 1'b0;
    logic media_complete = 1'b0;
    logic [7:0] media_slot = 8'd0;
    logic [7:0] media_kind = MT_LOAD_C64_G64;
    logic media_read_only = 1'b1;
    logic [31:0] media_image_size = 32'd300000;
    logic [31:0] media_generation = 32'd0;
    logic [31:0] media_complete_request_id = 32'd0;
    logic [31:0] media_complete_status = 32'd0;
    logic [15:0] media_complete_block_count = 16'd0;
    logic [31:0] media_complete_generation = 32'd0;
    logic media_request_valid;
    logic media_request_write;
    logic [31:0] media_request_id;
    logic [31:0] media_request_lba;
    logic [15:0] media_request_block_count;
    logic [31:0] media_request_generation;
    logic [7:0] load_data = 8'd0;
    logic load_valid = 1'b0;
    logic load_ready;
    logic load_first = 1'b0;
    logic load_last = 1'b0;
    logic [31:0] load_offset = 32'd0;
    logic [31:0] load_total = 32'd0;
    logic buffer_write;
    logic [13:0] buffer_address;
    logic [63:0] buffer_data;
    logic [1:0] track_speed;
    logic mounted;
    logic [31:0] mounted_generation;
    logic disk_busy;
    logic error;

    always #5 clk = !clk;
    multitini_c64_g64_track dut (.*);

    task automatic check(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic wait_request;
        integer timeout;
        begin : wait_loop
            for (timeout = 0; timeout < 20; timeout = timeout + 1) begin
                @(posedge clk); #1;
                if (media_request_valid) disable wait_loop;
            end
            $fatal(1, "timeout waiting for G64 request");
        end
    endtask

    task automatic send_track;
        integer index;
        integer writes;
        writes = 0;
        for (index = 0; index < 8192; index = index + 1) begin
            @(negedge clk);
            load_data = index[7:0];
            load_valid = 1'b1;
            load_first = index == 0;
            load_last = index == 8191;
            load_offset = index;
            load_total = 8192;
            @(posedge clk); #1;
            check(load_ready, "G64 read request accepts normalized bytes");
            if (buffer_write) begin
                check(buffer_address == index - 7,
                      "packed G64 buffer address is eight-byte aligned");
                if (index == 7)
                    check(buffer_data == 64'h0706050403020100,
                          "packed G64 word preserves byte order");
                writes = writes + 1;
            end
        end
        @(negedge clk);
        load_valid = 1'b0;
        load_first = 1'b0;
        load_last = 1'b0;
        check(writes == 1024, "exactly 1024 raw-track words written");
    endtask

    initial begin
        logic [31:0] request_id;
        repeat (3) @(negedge clk);
        reset = 1'b0;

        @(negedge clk);
        media_generation = 32'd7;
        media_mount = 1'b1;
        @(posedge clk); #1;
        check(mounted && mounted_generation == 7, "G64 mount accepted");
        @(negedge clk);
        media_mount = 1'b0;

        wait_request();
        check(!media_request_write && media_request_lba == 34 * 32 &&
              media_request_block_count == 32 &&
              media_request_generation == 7,
              "G64 request uses normalized half-track extent");
        request_id = media_request_id;
        send_track();

        @(negedge clk);
        media_complete_request_id = request_id;
        media_complete_status = 0;
        media_complete_block_count = 32;
        media_complete_generation = 7;
        media_complete = 1'b1;
        @(posedge clk); #1;
        check(!media_request_valid && !error,
              "matching complete publishes raw track");
        @(negedge clk);
        media_complete = 1'b0;
        @(posedge clk); #1;
        check(!disk_busy, "loaded current half track is ready");

        head_half_track = 7'd100;
        @(posedge clk); #1;
        check(disk_busy, "head movement blocks stale track data");
        wait_request();
        check(media_request_lba == 83 * 32,
              "G64 head is clamped to last representable half track");

        @(negedge clk);
        media_complete_request_id = media_request_id;
        media_complete_status = 2;
        media_complete_block_count = 0;
        media_complete_generation = 7;
        media_complete = 1'b1;
        @(posedge clk); #1;
        check(error && !media_request_valid,
              "failed G64 request enters the retryable fault state");
        @(negedge clk);
        media_complete = 1'b0;
        clear_error = 1'b1;
        media_generation = 8;
        media_eject = 1'b1;
        @(posedge clk); #1;
        check(!mounted && !error && !media_request_valid,
              "G64 eject wins over a coincident retry clear");
        @(negedge clk);
        clear_error = 1'b0;
        media_eject = 1'b0;

        $display("PASS: read-only C64 G64 raw-track service");
        $finish;
    end
endmodule
