// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module multitini_control_regs #(
    parameter logic [31:0] SHELL_BUILD_ID = 32'h00000000
) (
    input  logic         clk,
    input  logic         resetn,
    input  logic [31:0]  core_id,
    input  logic         core_ready,
    input  logic         load_submit_ready,
    input  logic         core_load_error,
    input  logic         input_submit_ready,
    input  logic         media_request_valid,
    input  logic         media_request_write,
    input  logic [31:0]  media_request_id,
    input  logic [31:0]  media_request_lba,
    input  logic [15:0]  media_request_block_count,
    input  logic [31:0]  media_request_generation,
    input  logic         media_submit_ready,

    input  logic         wr_en,
    input  logic [11:0]  wr_addr,
    input  logic [31:0]  wr_data,
    input  logic [3:0]   wr_strb,
    input  logic         rd_en,
    input  logic [11:0]  rd_addr,
    output logic [31:0]  rd_data,

    output logic [31:0]  control,
    output logic         load_submit,
    output logic [7:0]   load_kind,
    output logic [7:0]   load_data,
    output logic [31:0]  load_offset,
    output logic [31:0]  load_total,
    output logic         load_first,
    output logic         load_last,
    output logic         load_error_clear,
    output logic         input_submit,
    output logic [15:0]  input_code,
    output logic [15:0]  input_value,
    output logic         media_mount,
    output logic         media_eject,
    output logic         media_complete,
    output logic [7:0]   media_slot,
    output logic [7:0]   media_kind,
    output logic         media_read_only,
    output logic [31:0]  media_image_size,
    output logic [31:0]  media_generation,
    output logic [31:0]  media_complete_request_id,
    output logic [31:0]  media_complete_status,
    output logic [15:0]  media_complete_block_count,
    output logic [31:0]  media_complete_generation,
    output logic         irq
);
    import multitini_regs_pkg::*;

    logic [31:0] error_flags;
    logic [31:0] irq_status;
    logic [31:0] irq_enable;
    logic [31:0] scratch;
    logic [31:0] command_seq;
    logic [31:0] command_opcode;
    logic [31:0] command_arg0;
    logic [31:0] command_arg1;
    logic [31:0] command_arg2;
    logic [31:0] command_arg3;
    logic [31:0] response_seq;
    logic [31:0] response_status;
    logic [31:0] response_arg0;
    logic [31:0] response_arg1;
    logic [31:0] count_bad_write;
    logic [31:0] count_bad_read;
    logic [31:0] count_command;
    logic [31:0] count_timeout;
    logic [31:0] status_word;
    logic        stream_active;
    logic [7:0]  stream_kind;
    logic [31:0] stream_total;
    logic [31:0] stream_offset;
    logic        rd_known;
    logic [31:0] error_set_mask;
    logic [31:0] error_clear_mask;
    logic [31:0] irq_clear_mask;
    logic [31:0] media_request_word;

    function automatic logic [31:0] merge_strobes(
        input logic [31:0] old_value,
        input logic [31:0] new_value,
        input logic [3:0] strobes
    );
        logic [31:0] result;
        begin
            result = old_value;
            for (int lane = 0; lane < 4; lane = lane + 1)
                if (strobes[lane])
                    result[lane*8 +: 8] = new_value[lane*8 +: 8];
            merge_strobes = result;
        end
    endfunction

    always_comb begin
        media_request_word = 32'd0;
        if (media_request_valid)
            media_request_word |= MT_MEDIA_REQUEST_VALID;
        if (media_request_write)
            media_request_word |= MT_MEDIA_REQUEST_WRITE;

        status_word = MT_STATUS_SHELL_READY;
        if (control & MT_CONTROL_VIDEO_TEST_ENABLE)
            status_word |= MT_STATUS_VIDEO_ACTIVE;
        if (!(control & MT_CONTROL_AUDIO_MUTE) &&
            ((control & MT_CONTROL_AUDIO_TEST_ENABLE) || core_ready))
            status_word |= MT_STATUS_AUDIO_ACTIVE;
        if (core_ready)
            status_word |= MT_STATUS_CORE_READY;
        if (stream_active)
            status_word |= MT_STATUS_STREAM_ACTIVE;
        if (load_submit_ready)
            status_word |= MT_STATUS_STREAM_READY;
        if (core_load_error)
            status_word |= MT_STATUS_CORE_LOAD_ERROR;
        if (input_submit_ready && core_ready &&
            !(control & MT_CONTROL_CORE_RESET_REQUEST))
            status_word |= MT_STATUS_INPUT_READY;
        if (media_request_valid)
            status_word |= MT_STATUS_MEDIA_REQUEST;
    end

    always_comb begin
        rd_known = 1'b1;
        unique case (rd_addr)
            MT_REG_MAGIC:            rd_data = MT_MAGIC;
            MT_REG_ABI_VERSION:      rd_data = MT_ABI_VERSION;
            MT_REG_SHELL_ID:         rd_data = MT_SHELL_ID;
            MT_REG_SHELL_BUILD_ID:   rd_data = SHELL_BUILD_ID;
            MT_REG_CORE_ID:          rd_data = core_id;
            MT_REG_STATUS:           rd_data = status_word;
            MT_REG_ERROR:            rd_data = error_flags;
            MT_REG_CONTROL:          rd_data = control;
            MT_REG_IRQ_STATUS:       rd_data = irq_status;
            MT_REG_IRQ_ENABLE:       rd_data = irq_enable;
            MT_REG_SCRATCH:          rd_data = scratch;
            MT_REG_COMMAND_SEQ:      rd_data = command_seq;
            MT_REG_COMMAND_OPCODE:   rd_data = command_opcode;
            MT_REG_COMMAND_ARG0:     rd_data = command_arg0;
            MT_REG_COMMAND_ARG1:     rd_data = command_arg1;
            MT_REG_COMMAND_ARG2:     rd_data = command_arg2;
            MT_REG_COMMAND_ARG3:     rd_data = command_arg3;
            MT_REG_RESPONSE_SEQ:     rd_data = response_seq;
            MT_REG_RESPONSE_STATUS:  rd_data = response_status;
            MT_REG_RESPONSE_ARG0:    rd_data = response_arg0;
            MT_REG_RESPONSE_ARG1:    rd_data = response_arg1;
            MT_REG_COUNT_BAD_WRITE:  rd_data = count_bad_write;
            MT_REG_COUNT_BAD_READ:   rd_data = count_bad_read;
            MT_REG_COUNT_COMMAND:    rd_data = count_command;
            MT_REG_COUNT_TIMEOUT:    rd_data = count_timeout;
            MT_REG_MEDIA_REQUEST:    rd_data = media_request_word;
            MT_REG_MEDIA_REQUEST_ID: rd_data = media_request_id;
            MT_REG_MEDIA_REQUEST_LBA: rd_data = media_request_lba;
            MT_REG_MEDIA_REQUEST_BLOCK_COUNT:
                rd_data = {16'd0, media_request_block_count};
            MT_REG_MEDIA_REQUEST_GENERATION:
                rd_data = media_request_generation;
            default: begin
                rd_data = 32'h00000000;
                rd_known = 1'b0;
            end
        endcase
    end

    assign irq = |(irq_status & irq_enable);

    // Merge sticky updates so simultaneous AXI read and write completions
    // cannot lose an error. New errors win over a same-cycle software clear.
    always_comb begin
        error_set_mask = 32'd0;
        error_clear_mask = 32'd0;
        irq_clear_mask = 32'd0;
        if (core_load_error)
            error_set_mask |= MT_ERROR_CORE_LOAD;
        if (rd_en && !rd_known)
            error_set_mask |= MT_ERROR_BAD_READ;
        if (wr_en) begin
            unique case (wr_addr)
            MT_REG_ERROR:
                error_clear_mask = merge_strobes(32'd0, wr_data, wr_strb);
            MT_REG_IRQ_STATUS:
                irq_clear_mask = merge_strobes(32'd0, wr_data, wr_strb);
            MT_REG_COMMAND_DOORBELL: begin
                if (wr_data != command_seq)
                    error_set_mask |= MT_ERROR_BAD_COMMAND;
                else if (command_opcode != MT_COMMAND_PING &&
                         command_opcode != MT_COMMAND_GET_STATUS &&
                         command_opcode != MT_COMMAND_CORE_RESET &&
                         command_opcode != MT_COMMAND_SET_AUDIO &&
                         command_opcode != MT_COMMAND_INPUT_EVENT &&
                         command_opcode != MT_COMMAND_STREAM_BEGIN &&
                         command_opcode != MT_COMMAND_STREAM_DATA &&
                         command_opcode != MT_COMMAND_STREAM_END &&
                         command_opcode != MT_COMMAND_STREAM_ABORT &&
                         command_opcode != MT_COMMAND_MEDIA_MOUNT &&
                         command_opcode != MT_COMMAND_MEDIA_EJECT &&
                         command_opcode != MT_COMMAND_MEDIA_COMPLETE)
                    error_set_mask |= MT_ERROR_BAD_COMMAND;
            end
            MT_REG_CONTROL,
            MT_REG_IRQ_ENABLE,
            MT_REG_SCRATCH,
            MT_REG_COMMAND_SEQ,
            MT_REG_COMMAND_OPCODE,
            MT_REG_COMMAND_ARG0,
            MT_REG_COMMAND_ARG1,
            MT_REG_COMMAND_ARG2,
            MT_REG_COMMAND_ARG3: ;
            default: error_set_mask |= MT_ERROR_BAD_WRITE;
            endcase
        end
    end

    always_ff @(posedge clk) begin
        if (!resetn) begin
            error_flags     <= 32'h00000000;
            control         <= MT_CONTROL_CORE_RESET_REQUEST |
                               MT_CONTROL_VIDEO_TEST_ENABLE |
                               MT_CONTROL_AUDIO_TEST_ENABLE |
                               MT_CONTROL_AUDIO_MUTE;
            irq_status      <= 32'h00000000;
            irq_enable      <= 32'h00000000;
            scratch         <= 32'h00000000;
            command_seq     <= 32'h00000000;
            command_opcode  <= 32'h00000000;
            command_arg0    <= 32'h00000000;
            command_arg1    <= 32'h00000000;
            command_arg2    <= 32'h00000000;
            command_arg3    <= 32'h00000000;
            response_seq    <= 32'h00000000;
            response_status <= MT_RESPONSE_OK;
            response_arg0   <= 32'h00000000;
            response_arg1   <= 32'h00000000;
            count_bad_write <= 32'h00000000;
            count_bad_read  <= 32'h00000000;
            count_command   <= 32'h00000000;
            count_timeout   <= 32'h00000000;
            stream_active   <= 1'b0;
            stream_kind     <= 8'd0;
            stream_total    <= 32'd0;
            stream_offset   <= 32'd0;
            load_submit     <= 1'b0;
            load_kind       <= 8'd0;
            load_data       <= 8'd0;
            load_offset     <= 32'd0;
            load_total      <= 32'd0;
            load_first      <= 1'b0;
            load_last       <= 1'b0;
            load_error_clear <= 1'b0;
            input_submit    <= 1'b0;
            input_code      <= 16'd0;
            input_value     <= 16'd0;
            media_mount     <= 1'b0;
            media_eject     <= 1'b0;
            media_complete  <= 1'b0;
            media_slot      <= 8'd0;
            media_kind      <= 8'd0;
            media_read_only <= 1'b0;
            media_image_size <= 32'd0;
            media_generation <= 32'd0;
            media_complete_request_id <= 32'd0;
            media_complete_status <= MT_RESPONSE_OK;
            media_complete_block_count <= 16'd0;
            media_complete_generation <= 32'd0;
        end else begin
            load_submit <= 1'b0;
            load_error_clear <= 1'b0;
            input_submit <= 1'b0;
            media_mount <= 1'b0;
            media_eject <= 1'b0;
            media_complete <= 1'b0;
            error_flags <= (error_flags & ~error_clear_mask) | error_set_mask;
            irq_status <= (irq_status & ~irq_clear_mask) |
                          ((|error_set_mask) ? 32'd1 : 32'd0);

            if (rd_en && !rd_known) begin
                count_bad_read  <= count_bad_read + 32'd1;
            end

            if (wr_en) begin
                unique case (wr_addr)
                    MT_REG_ERROR:
                        if (|(error_clear_mask & MT_ERROR_CORE_LOAD))
                            load_error_clear <= 1'b1;
                    MT_REG_CONTROL:
                        control <= merge_strobes(control, wr_data, wr_strb);
                    MT_REG_IRQ_STATUS:
                        ;
                    MT_REG_IRQ_ENABLE:
                        irq_enable <= merge_strobes(irq_enable, wr_data, wr_strb);
                    MT_REG_SCRATCH:
                        scratch <= merge_strobes(scratch, wr_data, wr_strb);
                    MT_REG_COMMAND_SEQ:
                        command_seq <= merge_strobes(command_seq, wr_data, wr_strb);
                    MT_REG_COMMAND_OPCODE:
                        command_opcode <= merge_strobes(command_opcode, wr_data, wr_strb);
                    MT_REG_COMMAND_ARG0:
                        command_arg0 <= merge_strobes(command_arg0, wr_data, wr_strb);
                    MT_REG_COMMAND_ARG1:
                        command_arg1 <= merge_strobes(command_arg1, wr_data, wr_strb);
                    MT_REG_COMMAND_ARG2:
                        command_arg2 <= merge_strobes(command_arg2, wr_data, wr_strb);
                    MT_REG_COMMAND_ARG3:
                        command_arg3 <= merge_strobes(command_arg3, wr_data, wr_strb);
                    MT_REG_COMMAND_DOORBELL: begin
                        count_command <= count_command + 32'd1;
                        response_seq <= command_seq;
                        response_arg0 <= 32'h00000000;
                        response_arg1 <= 32'h00000000;
                        if (wr_data != command_seq) begin
                            response_status <= MT_RESPONSE_INVALID_ARGUMENT;
                        end else begin
                            unique case (command_opcode)
                                MT_COMMAND_PING: begin
                                    response_status <= MT_RESPONSE_OK;
                                    response_arg0 <= MT_SHELL_ID;
                                    response_arg1 <= MT_ABI_VERSION;
                                end
                                MT_COMMAND_GET_STATUS: begin
                                    response_status <= MT_RESPONSE_OK;
                                    response_arg0 <= status_word;
                                    response_arg1 <= error_flags;
                                end
                                MT_COMMAND_CORE_RESET: begin
                                    response_status <= MT_RESPONSE_OK;
                                    if (command_arg0[0])
                                        control <= control | MT_CONTROL_CORE_RESET_REQUEST;
                                    else
                                        control <= control & ~MT_CONTROL_CORE_RESET_REQUEST;
                                end
                                MT_COMMAND_SET_AUDIO: begin
                                    response_status <= MT_RESPONSE_OK;
                                    control <= (control &
                                                ~(MT_CONTROL_AUDIO_TEST_ENABLE |
                                                  MT_CONTROL_AUDIO_MUTE)) |
                                               (command_arg0[0] ?
                                                MT_CONTROL_AUDIO_TEST_ENABLE :
                                                32'd0) |
                                               (command_arg1[0] ?
                                                MT_CONTROL_AUDIO_MUTE : 32'd0);
                                end
                                MT_COMMAND_INPUT_EVENT: begin
                                    if (!input_submit_ready || !core_ready ||
                                        (control &
                                         MT_CONTROL_CORE_RESET_REQUEST)) begin
                                        response_status <= MT_RESPONSE_BUSY;
                                    end else begin
                                        response_status <= MT_RESPONSE_OK;
                                        input_submit <= 1'b1;
                                        input_code <= command_arg0[15:0];
                                        input_value <= command_arg1[15:0];
                                    end
                                end
                                MT_COMMAND_STREAM_BEGIN: begin
                                    if (stream_active) begin
                                        response_status <= MT_RESPONSE_BUSY;
                                    end else if (command_arg1 == 32'd0) begin
                                        response_status <= MT_RESPONSE_INVALID_ARGUMENT;
                                    end else begin
                                        response_status <= MT_RESPONSE_OK;
                                        stream_active <= 1'b1;
                                        stream_kind <= command_arg0[7:0];
                                        stream_total <= command_arg1;
                                        stream_offset <= 32'd0;
                                    end
                                end
                                MT_COMMAND_STREAM_DATA: begin
                                    if (!stream_active ||
                                        stream_offset >= stream_total) begin
                                        response_status <= MT_RESPONSE_INVALID_ARGUMENT;
                                    end else if (!load_submit_ready) begin
                                        response_status <= MT_RESPONSE_BUSY;
                                    end else begin
                                        response_status <= MT_RESPONSE_OK;
                                        load_submit <= 1'b1;
                                        load_kind <= stream_kind;
                                        load_data <= command_arg0[7:0];
                                        load_offset <= stream_offset;
                                        load_total <= stream_total;
                                        load_first <= stream_offset == 32'd0;
                                        load_last <= stream_offset + 32'd1 ==
                                                     stream_total;
                                        stream_offset <= stream_offset + 32'd1;
                                    end
                                end
                                MT_COMMAND_STREAM_END: begin
                                    if (!stream_active ||
                                        stream_offset != stream_total) begin
                                        response_status <= MT_RESPONSE_INVALID_ARGUMENT;
                                    end else begin
                                        response_status <= MT_RESPONSE_OK;
                                        stream_active <= 1'b0;
                                    end
                                end
                                MT_COMMAND_STREAM_ABORT: begin
                                    response_status <= MT_RESPONSE_OK;
                                    stream_active <= 1'b0;
                                    stream_kind <= 8'd0;
                                    stream_total <= 32'd0;
                                    stream_offset <= 32'd0;
                                end
                                MT_COMMAND_MEDIA_MOUNT: begin
                                    if ((command_arg0 &
                                         MT_MEDIA_MOUNT_RESERVED_MASK) !=
                                            32'd0 ||
                                        (command_arg0 &
                                         MT_MEDIA_MOUNT_READ_ONLY) ==
                                            32'd0 ||
                                        (command_arg0 & MT_MEDIA_KIND_MASK) ==
                                            32'd0 ||
                                        command_arg1 == 32'd0 ||
                                        command_arg2 == 32'd0 ||
                                        command_arg3 != 32'd0) begin
                                        response_status <=
                                            MT_RESPONSE_INVALID_ARGUMENT;
                                    end else if (!media_submit_ready) begin
                                        response_status <= MT_RESPONSE_BUSY;
                                    end else begin
                                        response_status <= MT_RESPONSE_OK;
                                        media_mount <= 1'b1;
                                        media_slot <= command_arg0[7:0];
                                        media_kind <= command_arg0[15:8];
                                        media_read_only <= command_arg0[16];
                                        media_image_size <= command_arg1;
                                        media_generation <= command_arg2;
                                    end
                                end
                                MT_COMMAND_MEDIA_EJECT: begin
                                    if (command_arg0[31:8] != 24'd0 ||
                                        command_arg1 == 32'd0 ||
                                        command_arg2 != 32'd0 ||
                                        command_arg3 != 32'd0) begin
                                        response_status <=
                                            MT_RESPONSE_INVALID_ARGUMENT;
                                    end else if (!media_submit_ready) begin
                                        response_status <= MT_RESPONSE_BUSY;
                                    end else begin
                                        response_status <= MT_RESPONSE_OK;
                                        media_eject <= 1'b1;
                                        media_slot <= command_arg0[7:0];
                                        media_generation <= command_arg1;
                                    end
                                end
                                MT_COMMAND_MEDIA_COMPLETE: begin
                                    if (!media_request_valid ||
                                        media_request_write ||
                                        media_request_block_count == 16'd0 ||
                                        command_arg0 != media_request_id ||
                                        command_arg1 != MT_RESPONSE_OK ||
                                        command_arg2[31:16] != 16'd0 ||
                                        command_arg2[15:0] !=
                                            media_request_block_count ||
                                        command_arg3 !=
                                            media_request_generation) begin
                                        response_status <=
                                            MT_RESPONSE_INVALID_ARGUMENT;
                                    end else if (!media_submit_ready) begin
                                        response_status <= MT_RESPONSE_BUSY;
                                    end else begin
                                        response_status <= MT_RESPONSE_OK;
                                        response_arg0 <= media_request_id;
                                        response_arg1 <= {
                                            16'd0, media_request_block_count};
                                        media_complete <= 1'b1;
                                        media_complete_request_id <=
                                            command_arg0;
                                        media_complete_status <=
                                            command_arg1;
                                        media_complete_block_count <=
                                            command_arg2[15:0];
                                        media_complete_generation <=
                                            command_arg3;
                                    end
                                end
                                default: begin
                                    response_status <= MT_RESPONSE_UNSUPPORTED;
                                end
                            endcase
                        end
                    end
                    default: begin
                        count_bad_write <= count_bad_write + 32'd1;
                    end
                endcase
            end
        end
    end

endmodule
