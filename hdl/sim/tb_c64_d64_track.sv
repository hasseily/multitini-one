// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_d64_track;
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
    logic [7:0] media_kind = MT_LOAD_C64_D64;
    logic media_read_only = 1'b1;
    logic [31:0] media_image_size = 32'd205312;
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
    logic [12:0] buffer_address;
    logic [7:0] buffer_data;
    logic [5:0] buffer_track;
    logic mounted;
    logic [31:0] mounted_generation;
    logic disk_busy;
    logic error;

    always #5 clk = !clk;

    multitini_c64_d64_track dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic pulse_mount(
        input logic read_only,
        input integer size,
        input integer generation
    );
        @(negedge clk);
        media_read_only = read_only;
        media_image_size = size;
        media_generation = generation;
        media_mount = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        media_mount = 1'b0;
    endtask

    task automatic pulse_eject(input integer generation);
        @(negedge clk);
        media_generation = generation;
        media_eject = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        media_eject = 1'b0;
    endtask

    task automatic wait_request;
        integer timeout;
        begin : wait_loop
            for (timeout = 0; timeout < 20; timeout = timeout + 1) begin
                @(posedge clk);
                #1;
                if (media_request_valid)
                    disable wait_loop;
            end
            $display("FAIL: timeout waiting for media request");
            $fatal(1);
        end
    endtask

    task automatic send_request_bytes;
        integer index;
        integer total;
        total = media_request_block_count * 256;
        for (index = 0; index < total; index = index + 1) begin
            @(negedge clk);
            load_data = index[7:0] ^ 8'hC3;
            load_valid = 1'b1;
            load_first = index == 0;
            load_last = index == total - 1;
            load_offset = index;
            load_total = total;
            @(posedge clk);
            #1;
            check_value(load_ready, "active request accepts D64 bytes");
            check_value(buffer_write, "accepted byte writes track buffer");
            check_value(buffer_address == index[12:0],
                        "track-buffer byte address");
            check_value(buffer_data == (index[7:0] ^ 8'hC3),
                        "track-buffer byte data");
        end
        @(negedge clk);
        load_valid = 1'b0;
        load_first = 1'b0;
        load_last = 1'b0;
    endtask

    task automatic pulse_complete(
        input integer request_id,
        input integer completed_blocks,
        input integer generation
    );
        @(negedge clk);
        media_complete_request_id = request_id;
        media_complete_status = 32'd0;
        media_complete_block_count = completed_blocks;
        media_complete_generation = generation;
        media_complete = 1'b1;
        @(posedge clk);
        #1;
        @(negedge clk);
        media_complete = 1'b0;
    endtask

    initial begin
        logic [31:0] first_request_id;
        logic [31:0] boundary_request_id;

        repeat (3) @(negedge clk);
        reset = 1'b0;
        @(negedge clk);

        pulse_mount(1'b0, 205312, 99);
        check_value(!mounted && error,
                    "writable D64 mount is rejected by read-only service");
        clear_error = 1'b1;
        @(posedge clk);
        #1;
        clear_error = 1'b0;

        pulse_mount(1'b1, 205312, 1);
        check_value(mounted && mounted_generation == 32'd1,
                    "42-track D64 mount accepted");
        wait_request();
        check_value(!media_request_write, "D64 request is read-only");
        check_value(media_request_lba == 32'd357 &&
                    media_request_block_count == 16'd19 &&
                    buffer_track == 6'd18,
                    "directory track is loaded first with actual block count");
        check_value(media_request_generation == 32'd1,
                    "request carries mount generation");
        first_request_id = media_request_id;

        head_half_track = 7'd82;
        repeat (3) begin
            @(posedge clk);
            #1;
            check_value(media_request_valid &&
                        media_request_id == first_request_id &&
                        media_request_lba == 32'd357 &&
                        media_request_block_count == 16'd19,
                        "request metadata remains stable while outstanding");
        end

        pulse_complete(first_request_id + 1,
                       media_request_block_count, 1);
        check_value(media_request_valid &&
                    media_request_id == first_request_id,
                    "mismatched completion is ignored");

        send_request_bytes();
        pulse_complete(first_request_id, 19, 1);
        check_value(!media_request_valid,
                    "matching complete releases directory request");
        @(posedge clk);
        #1;
        check_value(!media_request_valid,
                    "request valid has a full-clock low gap");
        wait_request();
        check_value(media_request_lba == 32'd785 &&
                    media_request_block_count == 16'd17 &&
                    buffer_track == 6'd42,
                    "track 42 stays within the 802-sector D64 boundary");
        boundary_request_id = media_request_id;
        check_value(boundary_request_id > first_request_id,
                    "request IDs increase across track changes");

        @(negedge clk);
        soft_reset = 1'b1;
        @(posedge clk);
        #1;
        check_value(!media_request_valid && mounted &&
                    mounted_generation == 32'd1,
                    "soft reset drops request but preserves mounted media");
        @(negedge clk);
        soft_reset = 1'b0;
        wait_request();
        check_value(media_request_id > boundary_request_id,
                    "request ID remains monotonic across soft reset");
        check_value(media_request_lba == 32'd357,
                    "soft-reset recovery refreshes directory track first");

        pulse_eject(2);
        check_value(!mounted && !media_request_valid &&
                    mounted_generation == 32'd2 && !error,
                    "new-generation eject invalidates the old mount");
        pulse_mount(1'b1, 174848, 3);
        check_value(mounted && mounted_generation == 32'd3,
                    "mount after eject accepts the next generation");
        wait_request();
        check_value(media_request_generation == 32'd3 &&
                    media_request_lba == 32'd357,
                    "remount request uses the newest generation");
        pulse_eject(4);
        pulse_eject(5);
        check_value(!mounted && !media_request_valid &&
                    mounted_generation == 32'd5 && !error,
                    "empty-drive eject is idempotent with a new generation");

        $display("PASS: read-only C64 D64 track service");
        $finish;
    end
endmodule
