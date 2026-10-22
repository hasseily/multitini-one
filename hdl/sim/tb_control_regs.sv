// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_control_regs #(parameter logic [31:0] ACCEL_CAPS=0,
                         parameter logic [31:0] C64_CAPS=0);
    import multitini_regs_pkg::*;

    logic clk = 1'b0;
    logic resetn = 1'b0;
    logic wr_en = 1'b0;
    logic [11:0] wr_addr = 12'd0;
    logic [31:0] wr_data = 32'd0;
    logic [3:0] wr_strb = 4'd0;
    logic rd_en = 1'b0;
    logic [11:0] rd_addr = 12'd0;
    logic [31:0] rd_data;
    logic [31:0] control;
    logic irq;
    logic effect_busy=0,effect_active_valid=0,effect_rejected=0;
    logic [31:0] effect_active_sequence=0;
    wire effect_write,effect_commit;
    wire [9:0] effect_address;
    wire [31:0] effect_data,effect_commit_sequence;
    logic load_submit_ready = 1'b1;
    logic core_load_error = 1'b0;
    logic [1:0] vectrex_overlay_status = 2'b00;
    logic load_submit;
    logic [7:0] load_kind;
    logic [7:0] load_data;
    logic [31:0] load_offset;
    logic [31:0] load_total;
    logic load_first;
    logic load_last;
    logic load_epoch;
    logic load_abort;
    logic load_abort_epoch;
    logic [7:0] load_abort_kind;
    logic load_error_clear;
    logic input_submit_ready = 1'b1;
    logic input_submit;
    logic [15:0] input_code;
    logic [15:0] input_value;
    logic media_request_valid = 1'b0;
    logic media_request_write = 1'b0;
    logic [31:0] media_request_id = 32'd0;
    logic [31:0] media_request_lba = 32'd0;
    logic [15:0] media_request_block_count = 16'd0;
    logic [31:0] media_request_generation = 32'd0;
    logic media_data_valid = 1'b0;
    logic [7:0] media_data = 8'd0;
    logic media_data_last = 1'b0;
    logic media_state_mounted = 1'b0;
    logic media_state_dirty = 1'b0;
    logic media_state_eject_pending = 1'b0;
    logic media_submit_ready = 1'b1;
    logic media_mount;
    logic media_eject;
    logic media_complete;
    logic [7:0] media_slot;
    logic [7:0] media_kind;
    logic media_read_only;
    logic [31:0] media_image_size;
    logic [31:0] media_generation;
    logic [31:0] media_complete_request_id;
    logic [31:0] media_complete_status;
    logic [15:0] media_complete_block_count;
    logic [31:0] media_complete_generation;
    logic media_data_pop;
    logic ui_active_visible = 1'b0;
    logic ui_active_bank = 1'b0;
    logic ui_active_dense = 1'b0;
    logic ui_submit_ready = 1'b1;
    logic ui_submit;
    logic ui_visible;
    logic ui_bank;
    logic ui_dense;

    always #5 clk = !clk;

    multitini_control_regs #(.SHELL_BUILD_ID(32'h12345678),.IIGS_ACCEL_CAPS(ACCEL_CAPS),
        .C64_EXPANSION_CAPS(C64_CAPS)) dut (
        .effect_busy(effect_busy),.effect_active_valid(effect_active_valid),.effect_rejected(effect_rejected),
        .effect_active_sequence(effect_active_sequence),.effect_write(effect_write),.effect_commit(effect_commit),
        .effect_address(effect_address),.effect_data(effect_data),.effect_commit_sequence(effect_commit_sequence),
        .clk(clk), .resetn(resetn), .core_id(32'h43363420),
        .core_variant_index(32'd1),
        .core_ready(1'b1), .load_submit_ready(load_submit_ready),
        .core_load_error(core_load_error),
        .vectrex_overlay_status(vectrex_overlay_status),
        .input_submit_ready(input_submit_ready),
        .media_request_valid(media_request_valid),
        .media_request_write(media_request_write),
        .media_request_id(media_request_id),
        .media_request_lba(media_request_lba),
        .media_request_block_count(media_request_block_count),
        .media_request_generation(media_request_generation),
        .media_data_valid(media_data_valid), .media_data(media_data),
        .media_data_last(media_data_last),
        .media_state_mounted(media_state_mounted),
        .media_state_dirty(media_state_dirty),
        .media_state_eject_pending(media_state_eject_pending),
        .media_submit_ready(media_submit_ready),
        .ui_active_visible(ui_active_visible),
        .ui_active_bank(ui_active_bank),
        .ui_active_dense(ui_active_dense),
        .ui_submit_ready(ui_submit_ready),
        .wr_en(wr_en), .wr_addr(wr_addr),
        .wr_data(wr_data), .wr_strb(wr_strb), .rd_en(rd_en),
        .rd_addr(rd_addr), .rd_data(rd_data), .control(control),
        .load_submit(load_submit), .load_kind(load_kind),
        .load_data(load_data), .load_offset(load_offset),
        .load_total(load_total), .load_first(load_first),
        .load_last(load_last), .load_epoch(load_epoch),
        .load_abort(load_abort), .load_abort_epoch(load_abort_epoch),
        .load_abort_kind(load_abort_kind),
        .load_error_clear(load_error_clear),
        .input_submit(input_submit), .input_code(input_code),
        .input_value(input_value),
        .media_mount(media_mount), .media_eject(media_eject),
        .media_complete(media_complete), .media_slot(media_slot),
        .media_kind(media_kind), .media_read_only(media_read_only),
        .media_image_size(media_image_size),
        .media_generation(media_generation),
        .media_complete_request_id(media_complete_request_id),
        .media_complete_status(media_complete_status),
        .media_complete_block_count(media_complete_block_count),
        .media_complete_generation(media_complete_generation),
        .media_data_pop(media_data_pop),
        .ui_submit(ui_submit), .ui_visible(ui_visible), .ui_bank(ui_bank), .ui_dense(ui_dense),
        .irq(irq)
    );

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic write_reg(input logic [11:0] address,
                             input logic [31:0] value,
                             input logic [3:0] strobes);
        @(negedge clk);
        wr_addr = address;
        wr_data = value;
        wr_strb = strobes;
        wr_en = 1'b1;
        @(negedge clk);
        wr_en = 1'b0;
        #1;
    endtask

    task automatic expect_read(input logic [11:0] address,
                               input logic [31:0] expected,
                               input string message);
        rd_addr = address;
        #1;
        check_value(rd_data === expected, message);
    endtask

    initial begin
        repeat (3) @(negedge clk);
        resetn = 1'b1;
        @(negedge clk);
        check_value(control == 32'h0000000F, "safe reset control value");
        expect_read(MT_REG_MAGIC, MT_MAGIC, "magic");
        expect_read(MT_REG_SHELL_BUILD_ID, 32'h12345678, "build ID");
        expect_read(MT_REG_ABI_VERSION,MT_ABI_VERSION,"ABI identity");
        expect_read(MT_REG_IIGS_ACCEL_CAPS,ACCEL_CAPS,"installed IIgs speed capability");
        expect_read(MT_REG_C64_EXPANSION_CAPS,C64_CAPS,"installed C64 expansion capability");
        expect_read(MT_REG_C64_EXPANSION_CONFIG,C64_CAPS[31]?32'hb:32'd0,"C64 expansion reset configuration");
        for(integer expansion=0;expansion<16;expansion++)begin
            logic[31:0] expected_config;
            expected_config=(expansion&8)|((expansion&7)==2?2:((expansion&7)==3?3:0));
            if(!C64_CAPS[31])expected_config=0;
            write_reg(MT_REG_C64_EXPANSION_CONFIG,32'hfffffff0|expansion,4'hf);
            expect_read(MT_REG_C64_EXPANSION_CONFIG,expected_config,"C64 reserved bits/modes sanitized");
            write_reg(MT_REG_C64_EXPANSION_CONFIG,32'h12345678,4'he);
            expect_read(MT_REG_C64_EXPANSION_CONFIG,expected_config,"C64 upper-byte strobes preserve config");
            write_reg(MT_REG_C64_EXPANSION_CONFIG,32'd0,4'h0);
            expect_read(MT_REG_C64_EXPANSION_CONFIG,expected_config,"C64 zero strobe preserves config");
        end
        write_reg(MT_REG_C64_EXPANSION_CONFIG,32'h0000000b,4'h1);
        expect_read(MT_REG_C64_EXPANSION_CONFIG,C64_CAPS[31]?32'hb:32'd0,"C64 low-byte update");
        expect_read(MT_REG_CORE_ID, 32'h43363420, "core ID");
        expect_read(MT_REG_CORE_VARIANT_INDEX, 32'd1, "core variant index");
        expect_read(MT_REG_STATUS,
                    MT_STATUS_SHELL_READY | MT_STATUS_VIDEO_ACTIVE |
                    MT_STATUS_CORE_READY | MT_STATUS_STREAM_READY,
                    "muted audio is not active");

        expect_read(MT_REG_UI_STATUS, MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT,
                    "UI mailbox initially ready");
        write_reg(MT_REG_UI_COMMIT,
                  MT_UI_COMMIT_VISIBLE | MT_UI_COMMIT_BANK, 4'hF);
        check_value(ui_submit && ui_visible && ui_bank,
                    "UI bank commit submitted coherently");
        ui_active_visible = 1'b1;
        ui_active_bank = 1'b1;
        #1;
        expect_read(MT_REG_UI_STATUS,
                    MT_UI_STATUS_VISIBLE | MT_UI_STATUS_BANK |
                    MT_UI_STATUS_READY | MT_UI_STATUS_DENSE_LAYOUT,
                    "UI active state reported");

        write_reg(MT_REG_UI_COMMIT, MT_UI_COMMIT_VISIBLE |
                  MT_UI_COMMIT_BANK | MT_UI_COMMIT_DENSE, 4'hF);
        check_value(ui_submit && ui_visible && ui_bank && ui_dense,
                    "dense mode joins atomic commit");
        ui_active_dense = 1'b1;
        #1;
        expect_read(MT_REG_UI_STATUS, MT_UI_STATUS_READY |
                    MT_UI_STATUS_DENSE_LAYOUT | MT_UI_STATUS_VISIBLE |
                    MT_UI_STATUS_BANK | MT_UI_STATUS_DENSE,
                    "dense active mode reported independently of capability");

        ui_submit_ready = 1'b0;
        write_reg(MT_REG_UI_COMMIT, MT_UI_COMMIT_VISIBLE, 4'hF);
        check_value(!ui_submit, "busy UI mailbox rejects commit");
        expect_read(MT_REG_ERROR, MT_ERROR_BAD_WRITE,
                    "busy UI commit sets bad-write error");
        write_reg(MT_REG_ERROR, MT_ERROR_BAD_WRITE, 4'hF);
        ui_submit_ready = 1'b1;

        write_reg(MT_REG_SCRATCH, 32'hA5A55A5A, 4'b0101);
        expect_read(MT_REG_SCRATCH, 32'h00A5005A, "byte strobes");

        write_reg(MT_REG_COMMAND_SEQ, 32'd1, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_PING, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd1, 4'hF);
        expect_read(MT_REG_RESPONSE_SEQ, 32'd1, "ping sequence");
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK, "ping status");
        expect_read(MT_REG_RESPONSE_ARG0, MT_SHELL_ID, "ping shell ID");

        write_reg(MT_REG_COMMAND_SEQ, 32'd2, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_SET_AUDIO, 4'hF);
        write_reg(MT_REG_COMMAND_ARG0, 32'd1, 4'hF);
        write_reg(MT_REG_COMMAND_ARG1, 32'd0, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd2, 4'hF);
        check_value((control & (MT_CONTROL_AUDIO_TEST_ENABLE |
                           MT_CONTROL_AUDIO_MUTE)) ==
               MT_CONTROL_AUDIO_TEST_ENABLE,
               "audio enable and mute update is atomic");
        expect_read(MT_REG_STATUS,
                    MT_STATUS_SHELL_READY | MT_STATUS_VIDEO_ACTIVE |
                    MT_STATUS_AUDIO_ACTIVE | MT_STATUS_CORE_READY |
                    MT_STATUS_STREAM_READY,
                    "unmuted diagnostic audio is active");
        write_reg(MT_REG_CONTROL,
                  control & ~MT_CONTROL_AUDIO_TEST_ENABLE, 4'hF);
        expect_read(MT_REG_STATUS,
                    MT_STATUS_SHELL_READY | MT_STATUS_VIDEO_ACTIVE |
                    MT_STATUS_AUDIO_ACTIVE | MT_STATUS_CORE_READY |
                    MT_STATUS_STREAM_READY,
                    "unmuted ready-machine audio is active");

        write_reg(MT_REG_COMMAND_SEQ, 32'd3, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, 32'hFFFFFFFF, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd3, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_UNSUPPORTED,
                    "unsupported command response");
        expect_read(MT_REG_ERROR, MT_ERROR_BAD_COMMAND,
                    "unsupported command error");

        rd_addr = 12'hFFC;
        rd_en = 1'b1;
        @(negedge clk);
        rd_en = 1'b0;
        @(negedge clk);
        expect_read(MT_REG_COUNT_BAD_READ, 32'd1, "bad read counter");
        expect_read(MT_REG_ERROR, MT_ERROR_BAD_COMMAND | MT_ERROR_BAD_READ,
                    "bad read error");

        // Clear BAD_COMMAND while another invalid read lands. The new read
        // error must survive the same-cycle clear operation.
        @(negedge clk);
        rd_addr = 12'hFF8;
        rd_en = 1'b1;
        wr_addr = MT_REG_ERROR;
        wr_data = MT_ERROR_BAD_COMMAND | MT_ERROR_BAD_READ;
        wr_strb = 4'hF;
        wr_en = 1'b1;
        @(negedge clk);
        rd_en = 1'b0;
        wr_en = 1'b0;
        #1;
        expect_read(MT_REG_ERROR, MT_ERROR_BAD_READ,
                    "simultaneous set wins over clear");

        write_reg(MT_REG_COMMAND_ARG0, 32'h00000001, 4'hF);
        write_reg(MT_REG_COMMAND_ARG1, 32'd2, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd4, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_STREAM_BEGIN, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd4, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK, "stream begin");
        expect_read(MT_REG_STATUS,
                    MT_STATUS_SHELL_READY | MT_STATUS_VIDEO_ACTIVE |
                    MT_STATUS_AUDIO_ACTIVE | MT_STATUS_CORE_READY |
                    MT_STATUS_STREAM_ACTIVE |
                    MT_STATUS_STREAM_READY,
                    "stream active status");

        load_submit_ready = 1'b0;
        write_reg(MT_REG_COMMAND_ARG0, 32'h000000AA, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd5, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_STREAM_DATA, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd5, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_BUSY,
                    "mailbox backpressure response");
        check_value(!load_submit, "busy stream byte not submitted");

        load_submit_ready = 1'b1;
        write_reg(MT_REG_COMMAND_SEQ, 32'd6, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd6, 4'hF);
        check_value(load_submit && load_kind == 8'h01 && load_data == 8'hAA,
                    "first stream payload");
        check_value(load_offset == 32'd0 && load_total == 32'd2 &&
                    load_first && !load_last,
                    "first stream framing");

        write_reg(MT_REG_COMMAND_ARG0, 32'h00000055, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd7, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd7, 4'hF);
        check_value(load_submit && load_data == 8'h55 &&
                    load_offset == 32'd1 && !load_first && load_last,
                    "last stream framing");

        write_reg(MT_REG_COMMAND_SEQ, 32'd8, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_STREAM_END, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd8, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK, "stream end");

        // ABI 0.8 packed writes buffer four bytes and drain them through the
        // same ordered load interface, including a partial final word.
        write_reg(MT_REG_COMMAND_ARG0, 32'h00000021, 4'hF);
        write_reg(MT_REG_COMMAND_ARG1, 32'd5, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd100, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_STREAM_BEGIN, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd100, 4'hF);
        load_submit_ready = 1'b0;
        write_reg(MT_REG_STREAM_DATA_BURST, 32'h44332211, 4'hF);
        expect_read(MT_REG_STREAM_STATUS, 32'd0,
                    "burst word applies backpressure");
        load_submit_ready = 1'b1;
        @(negedge clk); #1;
        check_value(load_submit && load_kind == 8'h21 &&
                    load_data == 8'h11 && load_offset == 32'd0 &&
                    load_first && !load_last,
                    "burst first byte framing");
        @(negedge clk); #1;
        check_value(load_submit && load_data == 8'h22 &&
                    load_offset == 32'd1, "burst second byte");
        @(negedge clk); #1;
        check_value(load_submit && load_data == 8'h33 &&
                    load_offset == 32'd2, "burst third byte");
        @(negedge clk); #1;
        check_value(load_submit && load_data == 8'h44 &&
                    load_offset == 32'd3 && !load_last,
                    "burst fourth byte");
        @(negedge clk); #1;
        check_value(!load_submit,
                    "fourth burst byte accepted before next word");
        expect_read(MT_REG_STREAM_STATUS, MT_STREAM_STATUS_READY,
                    "next burst word accepted");
        load_submit_ready = 1'b0;
        write_reg(MT_REG_STREAM_DATA_BURST, 32'hAABBCC55, 4'hF);
        load_submit_ready = 1'b1;
        @(negedge clk); #1;
        check_value(load_submit && load_data == 8'h55 &&
                    load_offset == 32'd4 && !load_first && load_last,
                    "partial burst final byte framing");
        @(negedge clk); #1;
        check_value(!load_submit,
                    "partial burst final byte accepted before drain");
        expect_read(MT_REG_STREAM_STATUS, MT_STREAM_STATUS_DRAINED,
                    "burst stream drained");
        write_reg(MT_REG_COMMAND_SEQ, 32'd101, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_STREAM_END, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd101, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "burst stream end");

        write_reg(MT_REG_COMMAND_ARG0, 32'h00000001, 4'hF);
        write_reg(MT_REG_COMMAND_ARG1, 32'd2, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd9, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_STREAM_BEGIN, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd9, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd10, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_STREAM_ABORT, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd10, 4'hF);
        check_value(load_abort && load_abort_epoch == dut.stream_epoch && load_abort_kind == 8'h01 &&
                    dut.stream_aborted,
                    "stream abort emits and marks the current epoch");
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK, "stream abort");
        write_reg(MT_REG_COMMAND_SEQ, 32'd110, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_STREAM_ABORT, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd110, 4'hF);
        check_value(load_abort && load_abort_epoch == dut.stream_epoch && load_abort_kind == 8'h01 &&
                    dut.stream_aborted,
                    "repeated stream abort keeps the same epoch");
        expect_read(MT_REG_STATUS,
                    MT_STATUS_SHELL_READY | MT_STATUS_VIDEO_ACTIVE |
                    MT_STATUS_AUDIO_ACTIVE | MT_STATUS_CORE_READY |
                    MT_STATUS_STREAM_READY,
                    "stream abort clears active state");

        write_reg(MT_REG_COMMAND_ARG0, 32'd0, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd11, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_CORE_RESET, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd11, 4'hF);
        expect_read(MT_REG_STATUS,
                    MT_STATUS_SHELL_READY | MT_STATUS_VIDEO_ACTIVE |
                    MT_STATUS_AUDIO_ACTIVE | MT_STATUS_CORE_READY |
                    MT_STATUS_STREAM_READY | MT_STATUS_INPUT_READY,
                    "input service ready after core reset release");

        input_submit_ready = 1'b0;
        write_reg(MT_REG_COMMAND_ARG0, 32'h0000001C, 4'hF);
        write_reg(MT_REG_COMMAND_ARG1, 32'h00000001, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd12, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_INPUT_EVENT, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd12, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_BUSY,
                    "input mailbox backpressure");
        check_value(!input_submit, "busy input event not submitted");

        input_submit_ready = 1'b1;
        write_reg(MT_REG_COMMAND_SEQ, 32'd13, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd13, 4'hF);
        check_value(input_submit && input_code == 16'h001C &&
                    input_value == 16'h0001,
                    "input event submitted coherently");

        // Mounts are explicit, path-free events. Writable media is allowed;
        // malformed payloads must not reach the core-facing pulse.
        write_reg(MT_REG_COMMAND_ARG0, 32'h00001200, 4'hF);
        write_reg(MT_REG_COMMAND_ARG1, 32'd174848, 4'hF);
        write_reg(MT_REG_COMMAND_ARG2, 32'h0000002A, 4'hF);
        write_reg(MT_REG_COMMAND_ARG3, 32'd0, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd14, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_MEDIA_MOUNT, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd14, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "writable media mount accepted");
        check_value(media_mount && !media_read_only,
                    "writable mount pulse and flag");

        write_reg(MT_REG_COMMAND_ARG0,
                  MT_MEDIA_MOUNT_READ_ONLY | 32'h00001200, 4'hF);
        media_submit_ready = 1'b0;
        write_reg(MT_REG_COMMAND_SEQ, 32'd15, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd15, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_BUSY,
                    "media mailbox backpressure");
        check_value(!media_mount, "busy media command not submitted");

        media_submit_ready = 1'b1;
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd15, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "read-only media mount accepted");
        check_value(media_mount && !media_eject && !media_complete &&
                    media_slot == 8'd0 && media_kind == 8'h12 &&
                    media_read_only && media_image_size == 32'd174848 &&
                    media_generation == 32'h0000002A,
                    "mount pulse and payload");

        write_reg(MT_REG_COMMAND_ARG0, 32'd0, 4'hF);
        write_reg(MT_REG_COMMAND_ARG1, 32'h0000002A, 4'hF);
        write_reg(MT_REG_COMMAND_ARG2, 32'd0, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd16, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_MEDIA_EJECT, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd16, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "media eject accepted");
        check_value(media_eject && !media_mount && !media_complete &&
                    media_slot == 8'd0 &&
                    media_generation == 32'h0000002A,
                    "eject pulse and payload");

        media_request_valid = 1'b1;
        media_request_write = 1'b0;
        media_request_id = 32'h11223344;
        media_request_lba = 32'd357;
        media_request_block_count = 16'd2;
        media_request_generation = 32'h0000002A;
        #1;
        expect_read(MT_REG_STATUS,
                    MT_STATUS_SHELL_READY | MT_STATUS_VIDEO_ACTIVE |
                    MT_STATUS_AUDIO_ACTIVE | MT_STATUS_CORE_READY |
                    MT_STATUS_STREAM_READY | MT_STATUS_INPUT_READY |
                    MT_STATUS_MEDIA_REQUEST,
                    "pending media request status");
        expect_read(MT_REG_MEDIA_REQUEST, MT_MEDIA_REQUEST_VALID,
                    "read request flags");
        expect_read(MT_REG_MEDIA_REQUEST_ID, 32'h11223344,
                    "media request ID");
        expect_read(MT_REG_MEDIA_REQUEST_LBA, 32'd357,
                    "media request LBA");
        expect_read(MT_REG_MEDIA_REQUEST_BLOCK_COUNT, 32'd2,
                    "media request block count");
        expect_read(MT_REG_MEDIA_REQUEST_GENERATION, 32'h0000002A,
                    "media request generation");

        media_state_mounted = 1'b1;
        media_state_dirty = 1'b1;
        media_state_eject_pending = 1'b1;
        media_data = 8'hC7;
        media_data_valid = 1'b1;
        media_data_last = 1'b1;
        #1;
        expect_read(MT_REG_MEDIA_STATE,
                    MT_MEDIA_STATE_MOUNTED | MT_MEDIA_STATE_DIRTY |
                    MT_MEDIA_STATE_EJECT_PENDING,
                    "media state flags");
        expect_read(MT_REG_MEDIA_DATA,
                    MT_MEDIA_DATA_VALID | MT_MEDIA_DATA_LAST |
                    (32'hC7 << MT_MEDIA_DATA_BYTE_SHIFT),
                    "media write byte");
        @(negedge clk);
        rd_addr = MT_REG_MEDIA_DATA;
        rd_en = 1'b1;
        @(negedge clk);
        check_value(media_data_pop, "media data read pops byte");
        rd_en = 1'b0;
        media_data_valid = 1'b0;
        media_data_last = 1'b0;
        media_state_dirty = 1'b0;
        media_state_eject_pending = 1'b0;

        // A stale ID, partial success count, or stale generation is rejected.
        // Explicit failures are forwarded with a zero completed count so the
        // core can preserve dirty state and leave its request deterministically.
        write_reg(MT_REG_COMMAND_ARG0, 32'h11223345, 4'hF);
        write_reg(MT_REG_COMMAND_ARG1, MT_RESPONSE_OK, 4'hF);
        write_reg(MT_REG_COMMAND_ARG2, 32'd2, 4'hF);
        write_reg(MT_REG_COMMAND_ARG3, 32'h0000002A, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd17, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_MEDIA_COMPLETE, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd17, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_INVALID_ARGUMENT,
                    "stale media request ID rejected");
        check_value(!media_complete, "stale completion pulse suppressed");

        write_reg(MT_REG_COMMAND_ARG0, 32'h11223344, 4'hF);
        write_reg(MT_REG_COMMAND_ARG1, MT_RESPONSE_IO_ERROR, 4'hF);
        write_reg(MT_REG_COMMAND_ARG2, 32'd0, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd18, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd18, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "failed media completion forwarded");
        check_value(media_complete &&
                    media_complete_status == MT_RESPONSE_IO_ERROR &&
                    media_complete_block_count == 16'd0,
                    "failed completion pulse and payload");

        write_reg(MT_REG_COMMAND_ARG1, MT_RESPONSE_OK, 4'hF);
        write_reg(MT_REG_COMMAND_ARG2, 32'd1, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd19, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd19, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_INVALID_ARGUMENT,
                    "partial media completion rejected");

        write_reg(MT_REG_COMMAND_ARG2, 32'd2, 4'hF);
        write_reg(MT_REG_COMMAND_ARG3, 32'h00000029, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd20, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd20, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_INVALID_ARGUMENT,
                    "stale media generation rejected");

        write_reg(MT_REG_COMMAND_ARG3, 32'h0000002A, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd21, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd21, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "matching media completion accepted");
        expect_read(MT_REG_RESPONSE_ARG0, 32'h11223344,
                    "completion response ID");
        expect_read(MT_REG_RESPONSE_ARG1, 32'd2,
                    "completion response block count");
        check_value(media_complete && !media_mount && !media_eject &&
                    media_complete_request_id == 32'h11223344 &&
                    media_complete_status == MT_RESPONSE_OK &&
                    media_complete_block_count == 16'd2 &&
                    media_complete_generation == 32'h0000002A,
                    "completion pulse and payload");

        media_request_write = 1'b1;
        #1;
        expect_read(MT_REG_MEDIA_REQUEST,
                    MT_MEDIA_REQUEST_VALID | MT_MEDIA_REQUEST_WRITE,
                    "write request flag is observable");
        write_reg(MT_REG_COMMAND_SEQ, 32'd22, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd22, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "matching write completion accepted");
        check_value(media_complete, "write completion pulse emitted");

        media_request_valid = 1'b0;
        media_request_write = 1'b0;
        #1;
        expect_read(MT_REG_STATUS,
                    MT_STATUS_SHELL_READY | MT_STATUS_VIDEO_ACTIVE |
                    MT_STATUS_AUDIO_ACTIVE | MT_STATUS_CORE_READY |
                    MT_STATUS_STREAM_READY | MT_STATUS_INPUT_READY,
                    "media request status clears with valid");

        core_load_error = 1'b1;
        @(negedge clk);
        expect_read(MT_REG_ERROR, MT_ERROR_BAD_READ | MT_ERROR_CORE_LOAD,
                    "core load error becomes sticky");
        core_load_error = 1'b0;
        write_reg(MT_REG_ERROR, MT_ERROR_BAD_READ | MT_ERROR_CORE_LOAD, 4'hF);
        check_value(load_error_clear, "load-error clear pulse");
        expect_read(MT_REG_ERROR, 32'd0, "load errors cleared");

        expect_read(MT_REG_BORDER_CONFIG,32'h670,"border default");
        expect_read(MT_REG_VECTREX_VIDEO_CONFIG,32'hB,"Vectrex video default");
        write_reg(MT_REG_VECTREX_VIDEO_CONFIG,32'hFFFFFFFF,4'hF);
        expect_read(MT_REG_VECTREX_VIDEO_CONFIG,32'h1F,"Vectrex reserved bits");
        write_reg(MT_REG_VECTREX_VIDEO_CONFIG,32'h00,4'h2);
        expect_read(MT_REG_VECTREX_VIDEO_CONFIG,32'h1F,"Vectrex byte strobes");
        write_reg(MT_REG_VECTREX_VIDEO_CONFIG,32'hB,4'h1);
        expect_read(MT_REG_VECTREX_VIDEO_CONFIG,32'hB,"Vectrex config restore");
        expect_read(MT_REG_VECTREX_OVERLAY_STATUS,32'h0,"Vectrex status reset");
        vectrex_overlay_status = 2'b01;
        expect_read(MT_REG_VECTREX_OVERLAY_STATUS,32'h1,"Vectrex loaded status");
        vectrex_overlay_status = 2'b10;
        expect_read(MT_REG_VECTREX_OVERLAY_STATUS,32'h2,"Vectrex error status");
        write_reg(MT_REG_BORDER_CONFIG,32'hFFFFFFFF,4'hF);
        expect_read(MT_REG_BORDER_CONFIG,32'h14F1,"border clamp and reserved bits");
        write_reg(MT_REG_BORDER_CONFIG,32'h00000008,4'h1);
        expect_read(MT_REG_BORDER_CONFIG,32'h1408,"border byte strobes");
        write_reg(MT_REG_BORDER_CONFIG,0,4'h2);
        expect_read(MT_REG_BORDER_CONFIG,32'h108,"padding minimum");
        expect_read(12'h0FC,32'd0,"unused register reads zero");
        @(negedge clk);rd_en=1'b1;
        @(negedge clk);rd_en=1'b0;
        expect_read(MT_REG_ERROR,MT_ERROR_BAD_READ,"unused register read rejected");
        write_reg(12'h0FC,32'd1,4'hF);
        expect_read(MT_REG_ERROR,MT_ERROR_BAD_READ | MT_ERROR_BAD_WRITE,
                    "unused register write rejected");
        write_reg(MT_REG_ERROR,32'hffffffff,4'hf);
        expect_read(MT_REG_VIDEO_EFFECT_ADDRESS,0,"effect address reset");
        expect_read(MT_REG_VIDEO_EFFECT_STATUS,8,"effect capability present");
        write_reg(MT_REG_VIDEO_EFFECT_ADDRESS,32'h123,4'hf);
        expect_read(MT_REG_VIDEO_EFFECT_ADDRESS,32'h123,"effect address write");
        write_reg(MT_REG_VIDEO_EFFECT_ADDRESS,32'h234,4'h1);
        expect_read(MT_REG_VIDEO_EFFECT_ADDRESS,32'h123,"effect partial address rejected");
        write_reg(MT_REG_VIDEO_EFFECT_ADDRESS,32'h400,4'hf);
        expect_read(MT_REG_VIDEO_EFFECT_ADDRESS,32'h123,"effect wide address rejected");
        write_reg(MT_REG_VIDEO_EFFECT_DATA,32'h87654321,4'hf);
        check_value(effect_write && effect_data==32'h87654321,"effect complete word pulse");
        write_reg(MT_REG_VIDEO_EFFECT_DATA,32'h12345678,4'h7);
        check_value(!effect_write && effect_data==32'h87654321,"effect partial word rejected");
        write_reg(MT_REG_VIDEO_EFFECT_COMMIT,32'h10203040,4'hf);
        check_value(effect_commit && effect_commit_sequence==32'h10203040,"effect commit pulse");
        effect_busy=1;
        write_reg(MT_REG_VIDEO_EFFECT_DATA,32'hffffffff,4'hf);
        check_value(!effect_write && effect_data==32'h87654321,"effect busy write rejected");
        write_reg(MT_REG_VIDEO_EFFECT_COMMIT,32'hffffffff,4'hf);
        check_value(!effect_commit && effect_commit_sequence==32'h10203040,"effect busy commit rejected");
        effect_active_valid=1;effect_rejected=1;effect_active_sequence=32'h10203040;
        expect_read(MT_REG_VIDEO_EFFECT_STATUS,15,"effect status bits");
        expect_read(MT_REG_VIDEO_EFFECT_SEQUENCE,32'h10203040,"effect committed sequence");
        expect_read(MT_REG_ERROR,MT_ERROR_BAD_WRITE,"effect malformed writes are reported");
        write_reg(MT_REG_IIGS_ACCEL_CAPS,0,4'hF);
        expect_read(MT_REG_IIGS_ACCEL_CAPS,ACCEL_CAPS,"speed capability is read-only");
        $display("PASS: control registers");
        $finish;
    end
endmodule

// Explicit top avoids Windows batch argument rewriting of xelab generic values.
module tb_control_regs_c64_caps;
    tb_control_regs #(.C64_CAPS(32'h80000003)) test_i();
endmodule
