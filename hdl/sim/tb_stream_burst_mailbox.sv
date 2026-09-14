// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Exercise the packed ABI 0.8 stream all the way through the asynchronous
// one-entry mailbox used by the board shell.  A permanently-ready stand-in
// cannot expose source-side pulses that are issued before mailbox ready has
// completed its low/high acknowledgement cycle.
module tb_stream_burst_mailbox;
    import multitini_regs_pkg::*;

    logic control_clk = 1'b0;
    logic control_resetn = 1'b0;
    logic core_clk = 1'b0;
    logic core_resetn = 1'b0;
    logic wr_en = 1'b0;
    logic [11:0] wr_addr = 12'd0;
    logic [31:0] wr_data = 32'd0;
    logic [3:0] wr_strb = 4'd0;
    logic [11:0] rd_addr = 12'd0;
    logic [31:0] rd_data;

    logic load_submit;
    logic load_submit_ready;
    logic [7:0] load_kind;
    logic [7:0] load_data;
    logic [31:0] load_offset;
    logic [31:0] load_total;
    logic load_first;
    logic load_last;
    logic load_epoch;
    logic load_abort;
    logic load_abort_epoch;
    logic load_abort_ready;
    logic [0:0] abort_mailbox_source;
    logic [0:0] abort_mailbox_destination;
    logic destination_abort;
    logic destination_abort_epoch;
    logic [82:0] mailbox_source;
    logic [82:0] mailbox_destination;
    logic destination_valid;
    logic destination_ready = 1'b0;
    logic [7:0] destination_kind;
    logic [7:0] destination_data;
    logic [31:0] destination_offset;
    logic [31:0] destination_total;
    logic destination_first;
    logic destination_last;
    logic destination_epoch;

    integer accepted_count = 0;
    logic stream_under_test = 1'b0;
    logic abort_test_active = 1'b0;

    always #5.0 control_clk = !control_clk;
    always #7.0 core_clk = !core_clk;

    assign mailbox_source = {load_kind, load_data, load_offset, load_total,
                             load_first, load_last, load_epoch};
    assign {destination_kind, destination_data, destination_offset,
            destination_total, destination_first, destination_last,
            destination_epoch} = mailbox_destination;
    assign abort_mailbox_source = load_abort_epoch;
    assign destination_abort_epoch = abort_mailbox_destination[0];

    multitini_control_regs #(.SHELL_BUILD_ID(32'h12345678)) control_regs_i (
        .clk(control_clk),
        .resetn(control_resetn),
        .core_id(32'h43363420),
        .core_variant_index(32'd0),
        .core_ready(1'b1),
        .load_submit_ready(load_submit_ready),
        .core_load_error(1'b0),
        .input_submit_ready(1'b1),
        .media_request_valid(1'b0),
        .media_request_write(1'b0),
        .media_request_id(32'd0),
        .media_request_lba(32'd0),
        .media_request_block_count(16'd0),
        .media_request_generation(32'd0),
        .media_data_valid(1'b0),
        .media_data(8'd0),
        .media_data_last(1'b0),
        .media_state_mounted(1'b0),
        .media_state_dirty(1'b0),
        .media_state_eject_pending(1'b0),
        .media_submit_ready(1'b1),
        .ui_active_visible(1'b0),
        .ui_active_bank(1'b0),
        .ui_submit_ready(1'b1),
        .wr_en(wr_en),
        .wr_addr(wr_addr),
        .wr_data(wr_data),
        .wr_strb(wr_strb),
        .rd_en(1'b0),
        .rd_addr(rd_addr),
        .rd_data(rd_data),
        .control(),
        .load_submit(load_submit),
        .load_kind(load_kind),
        .load_data(load_data),
        .load_offset(load_offset),
        .load_total(load_total),
        .load_first(load_first),
        .load_last(load_last),
        .load_epoch(load_epoch),
        .load_abort(load_abort),
        .load_abort_epoch(load_abort_epoch),
        .load_error_clear(),
        .input_submit(),
        .input_code(),
        .input_value(),
        .media_mount(),
        .media_eject(),
        .media_complete(),
        .media_slot(),
        .media_kind(),
        .media_read_only(),
        .media_image_size(),
        .media_generation(),
        .media_complete_request_id(),
        .media_complete_status(),
        .media_complete_block_count(),
        .media_complete_generation(),
        .media_data_pop(),
        .ui_submit(),
        .ui_visible(),
        .ui_bank(),
        .irq()
    );

    multitini_async_mailbox #(.WIDTH(83)) load_mailbox_i (
        .src_clk(control_clk),
        .src_resetn(control_resetn),
        .src_data(mailbox_source),
        .src_valid(load_submit),
        .src_ready(load_submit_ready),
        .dst_clk(core_clk),
        .dst_resetn(core_resetn),
        .dst_data(mailbox_destination),
        .dst_valid(destination_valid),
        .dst_ready(destination_ready)
    );

    multitini_async_mailbox #(.WIDTH(1)) abort_mailbox_i (
        .src_clk(control_clk),
        .src_resetn(control_resetn),
        .src_data(abort_mailbox_source),
        .src_valid(load_abort),
        .src_ready(load_abort_ready),
        .dst_clk(core_clk),
        .dst_resetn(core_resetn),
        .dst_data(abort_mailbox_destination),
        .dst_valid(destination_abort),
        .dst_ready(1'b1)
    );

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic write_reg(input logic [11:0] address,
                             input logic [31:0] value);
        @(negedge control_clk);
        wr_addr = address;
        wr_data = value;
        wr_strb = 4'hF;
        wr_en = 1'b1;
        @(negedge control_clk);
        wr_en = 1'b0;
        wr_strb = 4'd0;
        #1;
    endtask

    task automatic expect_read(input logic [11:0] address,
                               input logic [31:0] expected,
                               input string message);
        rd_addr = address;
        #1;
        if (rd_data !== expected)
            $display("READ MISMATCH addr=%03h expected=%08h actual=%08h",
                     address, expected, rd_data);
        check_value(rd_data === expected, message);
    endtask

    task automatic command(input logic [31:0] command_sequence,
                           input logic [31:0] opcode,
                           input logic [31:0] arg0,
                           input logic [31:0] arg1);
        write_reg(MT_REG_COMMAND_ARG0, arg0);
        write_reg(MT_REG_COMMAND_ARG1, arg1);
        write_reg(MT_REG_COMMAND_SEQ, command_sequence);
        write_reg(MT_REG_COMMAND_OPCODE, opcode);
        write_reg(MT_REG_COMMAND_DOORBELL, command_sequence);
    endtask

    task automatic validate_destination(input integer index);
        logic [7:0] expected_data;
        begin
            case (index)
                0: expected_data = 8'h11;
                1: expected_data = 8'h22;
                2: expected_data = 8'h33;
                3: expected_data = 8'h44;
                4: expected_data = 8'hAA;
                5: expected_data = 8'hBB;
                6: expected_data = 8'hCC;
                default: expected_data = 8'hXX;
            endcase
            check_value(destination_kind == 8'h21,
                        "destination kind is preserved");
            check_value(destination_data == expected_data,
                        "destination bytes remain ordered and exact");
            check_value(destination_offset == index,
                        "destination offsets are consecutive");
            check_value(destination_total == 32'd7,
                        "destination total is preserved");
            check_value(destination_first == (index == 0),
                        "first flag matches offset zero");
            check_value(destination_last == (index == 6),
                        "last flag matches final offset");
            check_value(destination_epoch == 1'b0,
                        "first stream epoch is preserved");
        end
    endtask

    task automatic accept_next(input integer stalled_core_cycles);
        logic [82:0] held_payload;
        integer target_count;
        begin
            target_count = accepted_count + 1;
            wait (destination_valid === 1'b1);
            #1;
            validate_destination(accepted_count);
            held_payload = mailbox_destination;
            repeat (stalled_core_cycles) begin
                @(negedge core_clk);
                #1;
                check_value(destination_valid,
                            "destination valid holds under backpressure");
                check_value(mailbox_destination === held_payload,
                            "destination payload holds under backpressure");
            end
            destination_ready = 1'b1;
            wait (accepted_count == target_count);
            @(negedge core_clk);
            destination_ready = 1'b0;
        end
    endtask

    always @(posedge core_clk) begin
        if (destination_valid && destination_ready) begin
            if (stream_under_test) begin
                validate_destination(accepted_count);
            end else if (abort_test_active && accepted_count == 7) begin
                check_value(destination_kind == 8'h15 &&
                            destination_offset == 0 && destination_first &&
                            destination_epoch == 1'b0,
                            "queued pre-abort byte retains aborted epoch");
            end else if (abort_test_active && accepted_count == 8) begin
                check_value(destination_kind == 8'h15 &&
                            destination_data == 8'h5a &&
                            destination_offset == 0 && destination_first &&
                            destination_last && destination_epoch == 1'b1,
                            "post-abort stream uses a fresh epoch");
            end
            accepted_count <= accepted_count + 1;
        end
    end

    // DRAINED promises that the final byte has crossed the actual mailbox,
    // not merely that the packed holding register advanced its local offset.
    always @(negedge control_clk) begin
        if (control_resetn && stream_under_test && accepted_count < 7)
            check_value((control_regs_i.stream_status_word &
                         MT_STREAM_STATUS_DRAINED) == 32'd0,
                        "DRAINED remains clear until destination accepts all bytes");
    end

    initial begin
        fork
            begin
                #50000;
                $display("FAIL: packed stream mailbox timeout");
                $fatal(1);
            end
        join_none

        repeat (4) @(negedge control_clk);
        control_resetn = 1'b1;
        repeat (3) @(negedge core_clk);
        core_resetn = 1'b1;

        command(32'd1, MT_COMMAND_STREAM_BEGIN, 32'h00000021, 32'd7);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "packed stream begins");
        stream_under_test = 1'b1;

        write_reg(MT_REG_STREAM_DATA_BURST, 32'h44332211);
        accept_next(3);
        accept_next(1);
        accept_next(4);
        accept_next(2);

        rd_addr = MT_REG_STREAM_STATUS;
        #1;
        while ((rd_data & MT_STREAM_STATUS_READY) !==
               MT_STREAM_STATUS_READY)
            @(negedge control_clk);
        write_reg(MT_REG_STREAM_DATA_BURST, 32'hDDCCBBAA);
        accept_next(2);
        accept_next(5);

        // Hold the final byte at the core boundary and prove that END is busy
        // and DRAINED stays clear until that transfer is really accepted.
        wait (destination_valid === 1'b1);
        #1;
        validate_destination(6);
        repeat (4) @(negedge core_clk);
        command(32'd2, MT_COMMAND_STREAM_END, 32'd0, 32'd0);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_BUSY,
                    "stream end is busy while final mailbox byte is stalled");
        check_value(accepted_count == 6,
                    "final stalled byte was not accepted early");
        check_value((control_regs_i.stream_status_word &
                     MT_STREAM_STATUS_DRAINED) == 32'd0,
                    "stream is not drained before final destination accept");

        accept_next(1);
        rd_addr = MT_REG_STREAM_STATUS;
        #1;
        while ((rd_data & MT_STREAM_STATUS_DRAINED) !==
               MT_STREAM_STATUS_DRAINED)
            @(negedge control_clk);
        expect_read(MT_REG_STREAM_STATUS, MT_STREAM_STATUS_DRAINED,
                    "stream reports drained after final mailbox acknowledgement");

        command(32'd3, MT_COMMAND_STREAM_END, 32'd0, 32'd0);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "stream ends after complete mailbox drain");
        expect_read(MT_REG_STREAM_STATUS, 32'd0,
                    "stream status clears after end");
        check_value(accepted_count == 7,
                    "all seven packed payload bytes crossed exactly once");

        stream_under_test = 1'b0;

        // Leave a packed first byte queued at the destination, then abort it.
        // The abort crosses coherently with epoch zero; the stale mailbox byte
        // remains tagged zero, while the abort advances later bytes to one.
        command(32'd4, MT_COMMAND_STREAM_BEGIN, 32'h00000015, 32'd5);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "abort-test stream begins");
        write_reg(MT_REG_STREAM_DATA_BURST, 32'h44332211);
        wait (destination_valid === 1'b1);
        #1;
        check_value(destination_offset == 0 && destination_first &&
                    destination_epoch == 1'b0,
                    "first packed byte is queued before abort");
        check_value(load_abort_ready,
                    "coherent abort mailbox is ready");
        command(32'd5, MT_COMMAND_STREAM_ABORT, 32'd0, 32'd0);
        check_value(load_abort && load_abort_epoch == 1'b0,
                    "abort launches with queued byte epoch");
        wait (destination_abort === 1'b1);
        #1;
        check_value(destination_abort_epoch == 1'b0,
                    "abort pulse and epoch cross coherently");
        wait (destination_abort === 1'b0);
        wait (load_abort_ready === 1'b1);
        command(32'd50, MT_COMMAND_STREAM_ABORT, 32'd0, 32'd0);
        check_value(load_abort && load_abort_epoch == 1'b0,
                    "repeated abort retains the quarantined epoch");
        wait (destination_abort === 1'b1);
        #1;
        check_value(destination_abort_epoch == 1'b0,
                    "repeated abort crosses with the same epoch");

        abort_test_active = 1'b1;
        destination_ready = 1'b1;
        wait (accepted_count == 8);
        @(negedge core_clk);
        destination_ready = 1'b0;
        wait (load_submit_ready === 1'b1);

        command(32'd6, MT_COMMAND_STREAM_BEGIN, 32'h00000015, 32'd1);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "new stream begins after queued abort");
        command(32'd7, MT_COMMAND_STREAM_DATA, 32'h0000005a, 32'd0);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK,
                    "new stream byte submits after queued abort");
        wait (destination_valid === 1'b1);
        destination_ready = 1'b1;
        wait (accepted_count == 9);
        @(negedge core_clk);
        destination_ready = 1'b0;
        check_value(accepted_count == 9,
                    "queued old byte and fresh byte each crossed exactly once");

        $display("PASS: packed stream through asynchronous mailbox");
        $finish;
    end
endmodule
