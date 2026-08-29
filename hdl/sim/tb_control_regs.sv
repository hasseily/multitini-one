// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_control_regs;
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
    logic load_submit_ready = 1'b1;
    logic core_load_error = 1'b0;
    logic load_submit;
    logic [7:0] load_kind;
    logic [7:0] load_data;
    logic [31:0] load_offset;
    logic [31:0] load_total;
    logic load_first;
    logic load_last;
    logic load_error_clear;

    always #5 clk = !clk;

    multitini_control_regs #(.SHELL_BUILD_ID(32'h12345678)) dut (
        .clk(clk), .resetn(resetn), .core_id(32'h43363420),
        .core_ready(1'b1), .load_submit_ready(load_submit_ready),
        .core_load_error(core_load_error),
        .wr_en(wr_en), .wr_addr(wr_addr),
        .wr_data(wr_data), .wr_strb(wr_strb), .rd_en(rd_en),
        .rd_addr(rd_addr), .rd_data(rd_data), .control(control),
        .load_submit(load_submit), .load_kind(load_kind),
        .load_data(load_data), .load_offset(load_offset),
        .load_total(load_total), .load_first(load_first),
        .load_last(load_last), .load_error_clear(load_error_clear), .irq(irq)
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
        expect_read(MT_REG_CORE_ID, 32'h43363420, "core ID");
        expect_read(MT_REG_STATUS,
                    MT_STATUS_SHELL_READY | MT_STATUS_VIDEO_ACTIVE |
                    MT_STATUS_CORE_READY | MT_STATUS_STREAM_READY,
                    "muted audio is not active");

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

        write_reg(MT_REG_COMMAND_ARG0, 32'h00000001, 4'hF);
        write_reg(MT_REG_COMMAND_ARG1, 32'd2, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd9, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_STREAM_BEGIN, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd9, 4'hF);
        write_reg(MT_REG_COMMAND_SEQ, 32'd10, 4'hF);
        write_reg(MT_REG_COMMAND_OPCODE, MT_COMMAND_STREAM_ABORT, 4'hF);
        write_reg(MT_REG_COMMAND_DOORBELL, 32'd10, 4'hF);
        expect_read(MT_REG_RESPONSE_STATUS, MT_RESPONSE_OK, "stream abort");
        expect_read(MT_REG_STATUS,
                    MT_STATUS_SHELL_READY | MT_STATUS_VIDEO_ACTIVE |
                    MT_STATUS_AUDIO_ACTIVE | MT_STATUS_CORE_READY |
                    MT_STATUS_STREAM_READY,
                    "stream abort clears active state");

        core_load_error = 1'b1;
        @(negedge clk);
        expect_read(MT_REG_ERROR, MT_ERROR_BAD_READ | MT_ERROR_CORE_LOAD,
                    "core load error becomes sticky");
        core_load_error = 1'b0;
        write_reg(MT_REG_ERROR, MT_ERROR_BAD_READ | MT_ERROR_CORE_LOAD, 4'hF);
        check_value(load_error_clear, "load-error clear pulse");
        expect_read(MT_REG_ERROR, 32'd0, "load errors cleared");

        $display("PASS: control registers");
        $finish;
    end
endmodule
