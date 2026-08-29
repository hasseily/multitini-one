// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module multitini_c64_loader (
    input  logic        clk,
    input  logic        reset,
    input  logic        clear_error,

    input  logic [7:0]  load_data,
    input  logic        load_valid,
    output logic        load_ready,
    input  logic        load_first,
    input  logic        load_last,
    input  logic [7:0]  load_kind,
    input  logic [31:0] load_offset,
    input  logic [31:0] load_total,

    output logic        rom_wr,
    output logic        rom_bank,
    output logic [13:0] rom_addr,
    output logic [7:0]  rom_data,
    output logic [2:0]  rom_loaded,
    output logic        roms_ready,
    output logic        active,
    output logic        error
);
    import multitini_c64_defs_pkg::*;

    logic [7:0]  active_kind;
    logic [31:0] active_total;
    logic [31:0] expected_offset;
    logic        object_error;

    function automatic logic [31:0] expected_size(input logic [7:0] kind);
        case (kind)
            MT_LOAD_C64_BASIC:     expected_size = MT_SIZE_C64_BASIC;
            MT_LOAD_C64_KERNAL:    expected_size = MT_SIZE_C64_KERNAL;
            MT_LOAD_C64_CHARACTER: expected_size = MT_SIZE_C64_CHARACTER;
            default:               expected_size = 32'd0;
        endcase
    endfunction

    function automatic logic supported_kind(input logic [7:0] kind);
        supported_kind = expected_size(kind) != 32'd0;
    endfunction

    function automatic logic [13:0] mapped_address(
        input logic [7:0] kind,
        input logic [31:0] offset
    );
        case (kind)
            MT_LOAD_C64_KERNAL: mapped_address = 14'h2000 + offset[13:0];
            default:            mapped_address = offset[13:0];
        endcase
    endfunction

    function automatic logic mapped_bank(input logic [7:0] kind);
        mapped_bank = kind == MT_LOAD_C64_CHARACTER;
    endfunction

    assign load_ready = !reset;
    assign roms_ready = &rom_loaded;

    always_ff @(posedge clk) begin : receive_stream
        logic start_good;
        logic continuation_good;
        logic finish_good;

        rom_wr <= 1'b0;

        if (reset) begin
            rom_bank       <= 1'b0;
            rom_addr       <= 14'd0;
            rom_data       <= 8'd0;
            rom_loaded     <= 3'b000;
            active         <= 1'b0;
            active_kind    <= 8'd0;
            active_total   <= 32'd0;
            expected_offset <= 32'd0;
            object_error   <= 1'b0;
            error          <= 1'b0;
        end else begin
            if (clear_error) error <= 1'b0;

            if (load_valid && load_ready) begin
                if (load_first) begin
                    start_good = supported_kind(load_kind) &&
                                 load_offset == 32'd0 &&
                                 load_total == expected_size(load_kind) &&
                                 !load_last;
                    if (active || !start_good) error <= 1'b1;

                    active          <= start_good;
                    active_kind     <= load_kind;
                    active_total    <= load_total;
                    expected_offset <= 32'd1;
                    object_error    <= !start_good;

                    if (start_good) begin
                        rom_wr   <= 1'b1;
                        rom_bank <= mapped_bank(load_kind);
                        rom_addr <= mapped_address(load_kind, load_offset);
                        rom_data <= load_data;
                    end
                end else if (active) begin
                    continuation_good = !object_error &&
                                        load_kind == active_kind &&
                                        load_total == active_total &&
                                        load_offset == expected_offset;
                    finish_good = continuation_good && load_last &&
                                  load_offset + 32'd1 == active_total;

                    if (continuation_good) begin
                        rom_wr   <= 1'b1;
                        rom_bank <= mapped_bank(active_kind);
                        rom_addr <= mapped_address(active_kind, load_offset);
                        rom_data <= load_data;
                    end else begin
                        error        <= 1'b1;
                        object_error <= 1'b1;
                    end

                    if (load_last) begin
                        if (finish_good) begin
                            case (active_kind)
                                MT_LOAD_C64_BASIC:     rom_loaded[0] <= 1'b1;
                                MT_LOAD_C64_KERNAL:    rom_loaded[1] <= 1'b1;
                                MT_LOAD_C64_CHARACTER: rom_loaded[2] <= 1'b1;
                                default:               error <= 1'b1;
                            endcase
                        end else begin
                            error <= 1'b1;
                        end
                        active       <= 1'b0;
                        object_error <= 1'b0;
                    end else begin
                        expected_offset <= expected_offset + 32'd1;
                    end
                end else begin
                    error <= 1'b1;
                end
            end
        end
    end
endmodule
