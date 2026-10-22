// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module multitini_control_regs #(
    parameter logic [31:0] SHELL_BUILD_ID = 32'h00000000,
    parameter logic [31:0] IIGS_ACCEL_CAPS = 32'd0,
    parameter logic [31:0] C64_EXPANSION_CAPS = 32'd0
) (
    input  logic         clk,
    input  logic         resetn,
    input  logic [31:0]  core_id,
    input  logic [31:0]  core_variant_index,
    input  logic         core_ready,
    input  logic         load_submit_ready,
    input  logic         core_load_error,
    input  logic [1:0]   vectrex_overlay_status,
    input  logic         input_submit_ready,
    input  logic         media_request_valid,
    input  logic         media_request_write,
    input  logic [31:0]  media_request_id,
    input  logic [31:0]  media_request_lba,
    input  logic [15:0]  media_request_block_count,
    input  logic [31:0]  media_request_generation,
    input  logic         media_data_valid,
    input  logic [7:0]   media_data,
    input  logic         media_data_last,
    input  logic         media_state_mounted,
    input  logic         media_state_dirty,
    input  logic         media_state_eject_pending,
    input  logic         media_submit_ready,
    input  logic         ui_active_visible,
    input  logic         ui_active_bank,
    input  logic         ui_active_dense,
    input  logic         ui_submit_ready,
    input  logic         effect_busy, effect_active_valid, effect_rejected,
    input  logic [31:0]  effect_active_sequence,

    input  logic         wr_en,
    input  logic [11:0]  wr_addr,
    input  logic [31:0]  wr_data,
    input  logic [3:0]   wr_strb,
    input  logic         rd_en,
    input  logic [11:0]  rd_addr,
    output logic [31:0]  rd_data,

    output logic [31:0]  control,
    output logic [31:0]  border_config,
    output logic [31:0]  vectrex_video_config,
    output logic [31:0]  c64_expansion_config,
    output logic         effect_write, effect_commit,
    output logic [9:0]   effect_address,
    output logic [31:0]  effect_data, effect_commit_sequence,
    output logic         load_submit,
    output logic [7:0]   load_kind,
    output logic [7:0]   load_data,
    output logic [31:0]  load_offset,
    output logic [31:0]  load_total,
    output logic         load_first,
    output logic         load_last,
    output logic         load_epoch,
    output logic         load_abort,
    output logic         load_abort_epoch,
    output logic [7:0]   load_abort_kind,
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
    output logic         media_data_pop,
    output logic         ui_submit,
    output logic         ui_visible,
    output logic         ui_bank,
    output logic         ui_dense,
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
    logic [31:0] stream_burst_word;
    logic [2:0]  stream_burst_count;
    logic [1:0]  stream_burst_index;
    logic        stream_burst_pending;
    logic        stream_epoch;
    logic        stream_aborted;
    logic [31:0] stream_status_word;
    logic        rd_known;
    logic [31:0] error_set_mask;
    logic [31:0] error_clear_mask;
    logic [31:0] irq_clear_mask;
    logic [31:0] media_request_word;
    logic [31:0] media_data_word;
    logic [31:0] media_state_word;
    logic [31:0] ui_status_word;

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

    function automatic logic [31:0] clamp_border(input logic [31:0] value);
        logic [31:0] result;
        begin
            result = value & 32'h00001FFF;
            if (result[3:1] > 4) result[3:1] = 0;
            if (result[12:8] == 0) result[12:8] = 1;
            else if (result[12:8] > 20) result[12:8] = 20;
            clamp_border = result;
        end
    endfunction

    always_comb begin
        media_request_word = 32'd0;
        if (media_request_valid)
            media_request_word |= MT_MEDIA_REQUEST_VALID;
        if (media_request_write)
            media_request_word |= MT_MEDIA_REQUEST_WRITE;

        media_data_word = 32'd0;
        if (media_data_valid) begin
            media_data_word |= MT_MEDIA_DATA_VALID;
            if (media_data_last)
                media_data_word |= MT_MEDIA_DATA_LAST;
            media_data_word |=
                ({24'd0, media_data} << MT_MEDIA_DATA_BYTE_SHIFT);
        end

        media_state_word = 32'd0;
        if (media_state_mounted)
            media_state_word |= MT_MEDIA_STATE_MOUNTED;
        if (media_state_dirty)
            media_state_word |= MT_MEDIA_STATE_DIRTY;
        if (media_state_eject_pending)
            media_state_word |= MT_MEDIA_STATE_EJECT_PENDING;

        ui_status_word = MT_UI_STATUS_DENSE_LAYOUT;
        if (ui_active_visible)
            ui_status_word |= MT_UI_STATUS_VISIBLE;
        if (ui_active_bank)
            ui_status_word |= MT_UI_STATUS_BANK;
        if (ui_active_dense)
            ui_status_word |= MT_UI_STATUS_DENSE;
        if (ui_submit_ready)
            ui_status_word |= MT_UI_STATUS_READY;

        stream_status_word = 32'd0;
        if (stream_active && !stream_burst_pending && !load_submit &&
            stream_offset < stream_total)
            stream_status_word |= MT_STREAM_STATUS_READY;
        if (stream_active && !stream_burst_pending && !load_submit &&
            load_submit_ready &&
            stream_offset == stream_total)
            stream_status_word |= MT_STREAM_STATUS_DRAINED;

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
        if (media_data_valid)
            status_word |= MT_STATUS_MEDIA_DATA;
    end

    function automatic logic [31:0] clamp_c64_expansion(input logic [31:0] value);
        logic [31:0] result;
        result = value & MT_C64_EXPANSION_CONFIG_MASK;
        if (value[2:0] != 3'd2 && value[2:0] != 3'd3) result[2:0] = 3'd0;
        return C64_EXPANSION_CAPS[31] ? result : 32'd0;
    endfunction

    always_comb begin
        rd_known = 1'b1;
        unique case (rd_addr)
            MT_REG_MAGIC:            rd_data = MT_MAGIC;
            MT_REG_ABI_VERSION:      rd_data = MT_ABI_VERSION;
            MT_REG_SHELL_ID:         rd_data = MT_SHELL_ID;
            MT_REG_SHELL_BUILD_ID:   rd_data = SHELL_BUILD_ID;
            MT_REG_CORE_ID:          rd_data = core_id;
            MT_REG_CORE_VARIANT_INDEX: rd_data = core_variant_index;
            MT_REG_IIGS_ACCEL_CAPS:   rd_data = IIGS_ACCEL_CAPS;
            MT_REG_C64_EXPANSION_CAPS: rd_data = C64_EXPANSION_CAPS;
            MT_REG_C64_EXPANSION_CONFIG: rd_data = c64_expansion_config;
            MT_REG_STATUS:           rd_data = status_word;
            MT_REG_ERROR:            rd_data = error_flags;
            MT_REG_CONTROL:          rd_data = control;
            MT_REG_BORDER_CONFIG:    rd_data = border_config;
            MT_REG_VECTREX_VIDEO_CONFIG: rd_data = vectrex_video_config;
            MT_REG_VECTREX_OVERLAY_STATUS: rd_data = {30'd0, vectrex_overlay_status};
            MT_REG_VIDEO_EFFECT_ADDRESS: rd_data = {22'd0, effect_address};
            MT_REG_VIDEO_EFFECT_STATUS: rd_data = {28'd0, 1'b1, effect_rejected,
                                                   effect_active_valid, effect_busy};
            MT_REG_VIDEO_EFFECT_SEQUENCE: rd_data = effect_active_sequence;
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
            MT_REG_MEDIA_DATA:       rd_data = media_data_word;
            MT_REG_MEDIA_STATE:      rd_data = media_state_word;
            MT_REG_UI_STATUS:        rd_data = ui_status_word;
            MT_REG_STREAM_STATUS:    rd_data = stream_status_word;
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
            MT_REG_UI_COMMIT:
                if ((wr_data & ~(MT_UI_COMMIT_VISIBLE |
                                 MT_UI_COMMIT_BANK | MT_UI_COMMIT_DENSE)) != 32'd0 ||
                    !ui_submit_ready)
                    error_set_mask |= MT_ERROR_BAD_WRITE;
            MT_REG_STREAM_DATA_BURST:
                if (wr_strb != 4'hF || !stream_active ||
                    stream_burst_pending || load_submit ||
                    stream_offset >= stream_total)
                    error_set_mask |= MT_ERROR_BAD_WRITE;
            MT_REG_VIDEO_EFFECT_ADDRESS:
                if (wr_strb != 4'hF || wr_data[31:10] != 0)
                    error_set_mask |= MT_ERROR_BAD_WRITE;
            MT_REG_VIDEO_EFFECT_DATA, MT_REG_VIDEO_EFFECT_COMMIT:
                if (wr_strb != 4'hF || effect_busy)
                    error_set_mask |= MT_ERROR_BAD_WRITE;
            MT_REG_CONTROL,
            MT_REG_BORDER_CONFIG,
            MT_REG_VECTREX_VIDEO_CONFIG,
            MT_REG_C64_EXPANSION_CONFIG,
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
            border_config   <= MT_BORDER_CONFIG_DEFAULT;
            vectrex_video_config <= MT_VECTREX_VIDEO_CONFIG_DEFAULT;
            c64_expansion_config <= clamp_c64_expansion(MT_C64_EXPANSION_CONFIG_DEFAULT);
            effect_write <= 0;
            effect_commit <= 0;
            effect_address <= 0;
            effect_data <= 0;
            effect_commit_sequence <= 0;
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
            stream_burst_word <= 32'd0;
            stream_burst_count <= 3'd0;
            stream_burst_index <= 2'd0;
            stream_burst_pending <= 1'b0;
            stream_epoch      <= 1'b0;
            stream_aborted    <= 1'b0;
            load_submit     <= 1'b0;
            load_kind       <= 8'd0;
            load_data       <= 8'd0;
            load_offset     <= 32'd0;
            load_total      <= 32'd0;
            load_first      <= 1'b0;
            load_last       <= 1'b0;
            load_epoch      <= 1'b0;
            load_abort      <= 1'b0;
            load_abort_epoch <= 1'b0;
            load_abort_kind  <= 8'd0;
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
            media_data_pop <= 1'b0;
            ui_submit      <= 1'b0;
            ui_visible     <= 1'b0;
            ui_bank        <= 1'b0;
            ui_dense       <= 1'b0;
        end else begin
            // Byte commands pulse load_submit for one cycle. Burst transfers
            // instead hold it as a conventional ready/valid producer until
            // the asynchronous mailbox accepts the staged byte.
            if (!stream_burst_pending)
                load_submit <= 1'b0;
            load_abort <= 1'b0;
            load_error_clear <= 1'b0;
            input_submit <= 1'b0;
            media_mount <= 1'b0;
            media_eject <= 1'b0;
            media_complete <= 1'b0;
            media_data_pop <= 1'b0;
            ui_submit <= 1'b0;
            effect_write <= 1'b0;
            effect_commit <= 1'b0;
            error_flags <= (error_flags & ~error_clear_mask) | error_set_mask;
            irq_status <= (irq_status & ~irq_clear_mask) |
                          ((|error_set_mask) ? 32'd1 : 32'd0);

            // ABI 0.8 accepts four packed bytes per MMIO write and drains
            // them through the existing ordered CDC mailbox. This keeps the
            // core-facing byte framing unchanged while removing a command
            // round trip for every payload byte.
            if (stream_burst_pending) begin
                if (!load_submit) begin
                    // Stage the first byte without consuming it. The mailbox
                    // samples this registered valid/data pair on a later clk
                    // edge, so the offset must not advance here.
                    load_submit <= 1'b1;
                    load_kind <= stream_kind;
                    case (stream_burst_index)
                    2'd0: load_data <= stream_burst_word[7:0];
                    2'd1: load_data <= stream_burst_word[15:8];
                    2'd2: load_data <= stream_burst_word[23:16];
                    default: load_data <= stream_burst_word[31:24];
                    endcase
                    load_offset <= stream_offset;
                    load_total <= stream_total;
                    load_first <= stream_offset == 32'd0;
                    load_last <= stream_offset + 32'd1 == stream_total;
                    load_epoch <= stream_epoch;
                end else if (load_submit_ready) begin
                    // Advance only after the currently registered byte has
                    // actually transferred. Keep valid asserted and stage the
                    // next byte while the mailbox makes its acknowledgement
                    // round trip; the payload remains stable whenever ready
                    // is low.
                    stream_offset <= stream_offset + 32'd1;
                    if ({1'b0, stream_burst_index} + 3'd1 ==
                        stream_burst_count) begin
                        load_submit <= 1'b0;
                        stream_burst_pending <= 1'b0;
                        stream_burst_index <= 2'd0;
                    end else begin
                        load_submit <= 1'b1;
                        stream_burst_index <= stream_burst_index + 2'd1;
                        case (stream_burst_index)
                        2'd0: load_data <= stream_burst_word[15:8];
                        2'd1: load_data <= stream_burst_word[23:16];
                        default: load_data <= stream_burst_word[31:24];
                        endcase
                        load_offset <= stream_offset + 32'd1;
                        load_total <= stream_total;
                        load_first <= 1'b0;
                        load_last <= stream_offset + 32'd2 == stream_total;
                    end
                end
            end

            if (rd_en && !rd_known) begin
                count_bad_read  <= count_bad_read + 32'd1;
            end
            if (rd_en && rd_addr == MT_REG_MEDIA_DATA && media_data_valid)
                media_data_pop <= 1'b1;

            if (wr_en) begin
                unique case (wr_addr)
                    MT_REG_ERROR:
                        if (|(error_clear_mask & MT_ERROR_CORE_LOAD))
                            load_error_clear <= 1'b1;
                    MT_REG_CONTROL:
                        control <= merge_strobes(control, wr_data, wr_strb);
                    MT_REG_BORDER_CONFIG:
                        border_config <= clamp_border(
                            merge_strobes(border_config, wr_data, wr_strb));
                    MT_REG_VECTREX_VIDEO_CONFIG:
                        vectrex_video_config <=
                            merge_strobes(vectrex_video_config, wr_data, wr_strb) &
                            MT_VECTREX_VIDEO_CONFIG_MASK;
                    MT_REG_C64_EXPANSION_CONFIG:
                        c64_expansion_config <= clamp_c64_expansion(
                            merge_strobes(c64_expansion_config, wr_data, wr_strb));
                    MT_REG_VIDEO_EFFECT_ADDRESS:
                        if (wr_strb == 4'hF && wr_data[31:10] == 0)
                            effect_address <= wr_data[9:0];
                    MT_REG_VIDEO_EFFECT_DATA:
                        if (wr_strb == 4'hF && !effect_busy) begin
                            effect_data <= wr_data;
                            effect_write <= 1;
                        end
                    MT_REG_VIDEO_EFFECT_COMMIT:
                        if (wr_strb == 4'hF && !effect_busy) begin
                            effect_commit_sequence <= wr_data;
                            effect_commit <= 1;
                        end
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
                    MT_REG_UI_COMMIT: begin
                        if ((wr_data & ~(MT_UI_COMMIT_VISIBLE |
                                        MT_UI_COMMIT_BANK | MT_UI_COMMIT_DENSE)) == 32'd0 &&
                            ui_submit_ready) begin
                            ui_visible <= |(wr_data & MT_UI_COMMIT_VISIBLE);
                            ui_bank <= |(wr_data & MT_UI_COMMIT_BANK);
                            ui_dense <= |(wr_data & MT_UI_COMMIT_DENSE);
                            ui_submit <= 1'b1;
                        end
                    end
                    MT_REG_STREAM_DATA_BURST: begin
                        if (wr_strb == 4'hF && stream_active &&
                            !stream_burst_pending && !load_submit &&
                            stream_offset < stream_total) begin
                            stream_burst_word <= wr_data;
                            stream_burst_index <= 2'd0;
                            stream_burst_count <=
                                stream_total - stream_offset >= 32'd4 ?
                                3'd4 : stream_total[2:0] -
                                       stream_offset[2:0];
                            stream_burst_pending <= 1'b1;
                        end
                    end
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
                                        stream_burst_pending <= 1'b0;
                                        stream_burst_count <= 3'd0;
                                        stream_burst_index <= 2'd0;
                                        // Repeated aborts retain one epoch;
                                        // advance it exactly once when the
                                        // replacement stream is accepted.
                                        if (stream_aborted)
                                            stream_epoch <= ~stream_epoch;
                                        stream_aborted <= 1'b0;
                                        load_submit <= 1'b0;
                                    end
                                end
                                MT_COMMAND_STREAM_DATA: begin
                                    if (!stream_active ||
                                        stream_offset >= stream_total) begin
                                        response_status <= MT_RESPONSE_INVALID_ARGUMENT;
                                    end else if (stream_burst_pending ||
                                                 load_submit ||
                                                 !load_submit_ready) begin
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
                                        load_epoch <= stream_epoch;
                                        stream_offset <= stream_offset + 32'd1;
                                    end
                                end
                                MT_COMMAND_STREAM_END: begin
                                    if (!stream_active) begin
                                        response_status <= MT_RESPONSE_INVALID_ARGUMENT;
                                    end else if (stream_burst_pending ||
                                                 load_submit ||
                                                 !load_submit_ready) begin
                                        response_status <= MT_RESPONSE_BUSY;
                                    end else if (stream_offset != stream_total) begin
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
                                    stream_burst_pending <= 1'b0;
                                    stream_burst_count <= 3'd0;
                                    stream_burst_index <= 2'd0;
                                    load_submit <= 1'b0;
                                    load_abort <= 1'b1;
                                    load_abort_epoch <= stream_epoch;
                                    // Repeated aborts identify the same stream
                                    // even after stream_kind has been cleared.
                                    if (!stream_aborted)
                                        load_abort_kind <= stream_kind;
                                    stream_aborted <= 1'b1;
                                end
                                MT_COMMAND_MEDIA_MOUNT: begin
                                    if ((command_arg0 &
                                         MT_MEDIA_MOUNT_RESERVED_MASK) !=
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
                                        media_request_block_count == 16'd0 ||
                                        command_arg0 != media_request_id ||
                                        (command_arg1 != MT_RESPONSE_OK &&
                                         command_arg1 !=
                                             MT_RESPONSE_INVALID_ARGUMENT &&
                                         command_arg1 != MT_RESPONSE_IO_ERROR &&
                                         command_arg1 != MT_RESPONSE_TIMEOUT) ||
                                        (command_arg1 == MT_RESPONSE_OK &&
                                         (command_arg2[31:16] != 16'd0 ||
                                          command_arg2[15:0] !=
                                              media_request_block_count)) ||
                                        (command_arg1 != MT_RESPONSE_OK &&
                                         command_arg2 != 32'd0) ||
                                        command_arg3 !=
                                            media_request_generation) begin
                                        response_status <=
                                            MT_RESPONSE_INVALID_ARGUMENT;
                                    end else if (!media_submit_ready) begin
                                        response_status <= MT_RESPONSE_BUSY;
                                    end else begin
                                        response_status <= MT_RESPONSE_OK;
                                        response_arg0 <= media_request_id;
                                        response_arg1 <= command_arg2;
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
