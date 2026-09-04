// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_gameboy_save;
    import multitini_gameboy_defs_pkg::*;

    logic clk = 1'b0;
    logic reset = 1'b1;
    logic clear_error = 1'b0;
    logic runtime_write_committed = 1'b0;
    logic [23:0] runtime_write_address = 24'd0;
    logic media_mount = 1'b0;
    logic media_eject = 1'b0;
    logic media_complete = 1'b0;
    logic [7:0] media_slot = 8'd0;
    logic [7:0] media_kind = 8'd0;
    logic media_read_only = 1'b0;
    logic [31:0] media_image_size = 32'd0;
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
    logic [7:0] media_write_data;
    logic media_write_data_valid;
    logic media_write_data_last;
    logic media_write_data_ready = 1'b1;
    logic media_state_mounted;
    logic media_state_dirty;
    logic media_state_eject_pending;
    logic memory_read;
    logic [23:0] memory_address;
    logic memory_ready = 1'b1;
    logic [63:0] memory_rdata = 64'd0;
    logic memory_rvalid = 1'b0;
    logic pause_core;
    logic error;
    integer byte_count = 0;
    integer request_count = 0;

    always #5 clk = !clk;

    multitini_gameboy_save #(.AUTO_FLUSH_CYCLES(4)) dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    always @(posedge clk) begin
        memory_rvalid <= 1'b0;
        if (memory_read && memory_ready) begin
            check_value(memory_address[2:0] == 3'd0,
                        "snapshot reads aligned lines");
            memory_rdata <= {
                memory_address[7:0] + 8'd7,
                memory_address[7:0] + 8'd6,
                memory_address[7:0] + 8'd5,
                memory_address[7:0] + 8'd4,
                memory_address[7:0] + 8'd3,
                memory_address[7:0] + 8'd2,
                memory_address[7:0] + 8'd1,
                memory_address[7:0]
            };
            memory_rvalid <= 1'b1;
        end
        if (media_write_data_valid && media_write_data_ready) begin
            check_value(media_write_data == byte_count[7:0],
                        "snapshot bytes remain ordered");
            check_value(media_write_data_last == (byte_count == 255),
                        "LAST marks only the final byte");
            byte_count <= byte_count + 1;
        end
        if (media_request_valid && request_count == 0)
            request_count <= 1;
    end

    task automatic mount_save(input logic [31:0] generation);
        begin
            media_kind = MT_LOAD_GB_SAVE;
            media_image_size = 32'd256;
            media_generation = generation;
            media_mount = 1'b1;
            @(negedge clk);
            media_mount = 1'b0;
        end
    endtask

    task automatic complete_save(input logic [31:0] status);
        begin
            @(negedge clk);
            media_complete_request_id = media_request_id;
            media_complete_status = status;
            media_complete_block_count = media_request_block_count;
            media_complete_generation = media_request_generation;
            media_complete = 1'b1;
            @(negedge clk);
            media_complete = 1'b0;
        end
    endtask

    initial begin
        repeat (3) @(negedge clk);
        reset = 1'b0;
        mount_save(32'd1);
        check_value(media_state_mounted && !media_state_dirty,
                    "valid save mount accepted");

        runtime_write_address = MT_GB_MEMORY_CART_RAM_BASE + 24'h12;
        runtime_write_committed = 1'b1;
        @(negedge clk);
        runtime_write_committed = 1'b0;
        check_value(media_state_dirty, "cartridge RAM write marks save dirty");

        wait (media_request_valid);
        #1;
        check_value(media_request_write && media_request_lba == 0 &&
                    media_request_block_count == 1 &&
                    media_request_generation == 1,
                    "automatic flush requests the whole save image");
        check_value(pause_core, "core pauses for a coherent snapshot");
        wait (byte_count == 256);
        wait (!media_write_data_valid);
        complete_save(32'd0);
        check_value(media_state_mounted && !media_state_dirty && !error,
                    "successful automatic flush keeps save mounted");

        runtime_write_committed = 1'b1;
        @(negedge clk);
        runtime_write_committed = 1'b0;
        media_generation = 32'd2;
        media_eject = 1'b1;
        @(negedge clk);
        media_eject = 1'b0;
        check_value(media_state_eject_pending && pause_core,
                    "dirty eject becomes a persistence barrier");
        byte_count = 0;
        wait (media_request_valid && media_request_generation == 1);
        #1;
        wait (byte_count == 256);
        wait (!media_write_data_valid);
        complete_save(32'd0);
        check_value(!media_state_mounted && !media_state_dirty &&
                    !media_state_eject_pending && !pause_core,
                    "successful barrier ejects only after persistence");

        mount_save(32'd3);
        runtime_write_committed = 1'b1;
        @(negedge clk);
        runtime_write_committed = 1'b0;
        wait (media_request_valid);
        #1;
        byte_count = 0;
        wait (byte_count == 256);
        wait (!media_write_data_valid);
        complete_save(32'd4);
        check_value(media_state_dirty && error && !pause_core,
                    "failed background flush retains dirty data");

        $display("PASS: Game Boy persistent save media bridge");
        $finish;
    end
endmodule
