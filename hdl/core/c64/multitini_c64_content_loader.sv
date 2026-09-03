// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Strict PRG/CRT stream receiver. PRGs are written directly into C64 RAM and
// get the same BASIC pointer repair used by the pinned MiSTer core. CRT ROM
// bytes are normalized into the common external-memory cartridge window.
module multitini_c64_content_loader (
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
    input  logic [15:0] core_addr,

    output logic        prg_ram_wr,
    output logic [15:0] prg_ram_addr,
    output logic [7:0]  prg_ram_data,
    output logic        prg_loaded,
    output logic        prg_run_request,

    output logic [23:0] loader_memory_addr,
    output logic        loader_memory_write,
    output logic [63:0] loader_memory_wdata,
    output logic [7:0]  loader_memory_wstrb,
    output logic        loader_memory_valid,
    input  logic        loader_memory_ready,
    input  logic        memory_fault,

    output logic        cart_loading,
    output logic        cart_loaded,
    output logic [7:0]  cart_id,
    output logic        cart_exrom,
    output logic        cart_game,
    output logic        cart_bank_hi,
    output logic        cart_bank_16k,
    output logic [7:0]  cart_bank_num,
    output logic [7:0]  cart_bank_addr,
    output logic        cart_bank_wr,
    output logic        core_reset_hold,
    output logic        prg_install_pause,

    output logic        active,
    output logic        error
);
    import multitini_c64_defs_pkg::*;

    typedef enum logic [2:0] {
        STATE_IDLE,
        STATE_STREAM,
        STATE_PRG_RESET,
        STATE_PRG_WAIT_BASIC,
        STATE_PRG_POINTERS,
        STATE_PRG_FINISH,
        STATE_CRT_FINISH
    } state_t;

    state_t state;
    logic [7:0] active_kind;
    logic [31:0] active_total;
    logic [31:0] expected_offset;
    logic object_error;

    logic [15:0] prg_base;
    logic [15:0] prg_end;
    logic [3:0] pointer_index;
    logic [7:0] prg_reset_count;

    logic [3:0] chip_header_index;
    logic [31:0] chip_packet_size;
    logic [7:0] chip_bank;
    logic [15:0] chip_address;
    logic [15:0] chip_image_size;
    logic [15:0] chip_bytes_remaining;
    logic [8:0] chip_count;
    logic first_chip_16k;
    logic [21:0] crt_store_offset;
    logic [255:0] low_bank_seen;
    logic [255:0] high_bank_seen;

    localparam logic [8*16-1:0] CRT_MAGIC = "C64 CARTRIDGE   ";
    localparam logic [8*4-1:0] CHIP_MAGIC = "CHIP";

    function automatic logic crt_type_supported(input logic [7:0] value);
        case (value)
            MT_CRT_NORMAL,
            MT_CRT_SIMONS_BASIC,
            MT_CRT_OCEAN_TYPE_1,
            MT_CRT_FUN_PLAY,
            MT_CRT_SUPER_GAMES,
            MT_CRT_EPYX_FASTLOAD,
            MT_CRT_FINAL_CARTRIDGE_1_2,
            MT_CRT_C64_GAME_SYSTEM,
            MT_CRT_DINAMIC,
            MT_CRT_ZAXXON,
            MT_CRT_MAGIC_DESK,
            MT_CRT_COMAL_80,
            MT_CRT_WATERLOO_BASIC,
            MT_CRT_MIKRO_ASSEMBLER,
            MT_CRT_PROPHET64,
            MT_CRT_RGCD,
            MT_CRT_MAGIC_DESK_2: crt_type_supported = 1'b1;
            default: crt_type_supported = 1'b0;
        endcase
    endfunction

    function automatic logic indexed_8k_layout(
        input logic [8:0] index,
        input logic [7:0] bank,
        input logic [15:0] address,
        input logic [15:0] image_size,
        input logic [8:0] bank_limit
    );
        indexed_8k_layout = index < bank_limit && bank == index[7:0] &&
            address == 16'h8000 && image_size == 16'h2000;
    endfunction

    function automatic logic indexed_16k_layout(
        input logic [8:0] index,
        input logic [7:0] bank,
        input logic [15:0] address,
        input logic [15:0] image_size,
        input logic [8:0] bank_limit
    );
        indexed_16k_layout = index < bank_limit && bank == index[7:0] &&
            address == 16'h8000 && image_size == 16'h4000;
    endfunction

    function automatic logic crt_profile_lines_supported(
        input logic [7:0] hardware_type,
        input logic exrom,
        input logic game
    );
        case (hardware_type)
            MT_CRT_NORMAL:
                crt_profile_lines_supported = !(exrom && game);
            MT_CRT_OCEAN_TYPE_1:
                crt_profile_lines_supported = !exrom;
            MT_CRT_SIMONS_BASIC,
            MT_CRT_FINAL_CARTRIDGE_1_2,
            MT_CRT_SUPER_GAMES,
            MT_CRT_ZAXXON,
            MT_CRT_COMAL_80,
            MT_CRT_MAGIC_DESK_2:
                crt_profile_lines_supported = !exrom && !game;
            MT_CRT_FUN_PLAY,
            MT_CRT_EPYX_FASTLOAD,
            MT_CRT_C64_GAME_SYSTEM,
            MT_CRT_DINAMIC,
            MT_CRT_MAGIC_DESK,
            MT_CRT_WATERLOO_BASIC,
            MT_CRT_MIKRO_ASSEMBLER,
            MT_CRT_PROPHET64,
            MT_CRT_RGCD:
                crt_profile_lines_supported = !exrom && game;
            default:
                crt_profile_lines_supported = 1'b0;
        endcase
    endfunction

    function automatic logic crt_chip_layout_supported(
        input logic [7:0] hardware_type,
        input logic [8:0] index,
        input logic [7:0] bank,
        input logic [15:0] address,
        input logic [15:0] image_size,
        input logic exrom,
        input logic game
    );
        begin
            crt_chip_layout_supported = 1'b0;
            case (hardware_type)
                MT_CRT_NORMAL: begin
                    if (bank != 0 || index > 1)
                        crt_chip_layout_supported = 1'b0;
                    else if (!exrom && game)
                        crt_chip_layout_supported = index == 0 &&
                            address == 16'h8000 && image_size == 16'h2000;
                    else if (exrom && !game)
                        crt_chip_layout_supported = index == 0 &&
                            address == 16'he000 && image_size == 16'h2000;
                    else if (index == 0)
                        crt_chip_layout_supported =
                            (image_size == 16'h4000 &&
                             address == 16'h8000) ||
                            (image_size == 16'h2000 &&
                             address == 16'h8000);
                    else
                        crt_chip_layout_supported =
                            image_size == 16'h2000 &&
                            address == 16'ha000;
                end
                MT_CRT_SIMONS_BASIC: begin
                    if (bank == 0 && index == 0)
                        crt_chip_layout_supported = address == 16'h8000 &&
                            (image_size == 16'h2000 ||
                             image_size == 16'h4000);
                    else if (bank == 0 && index == 1)
                        crt_chip_layout_supported = address == 16'ha000 &&
                            image_size == 16'h2000;
                end
                MT_CRT_FINAL_CARTRIDGE_1_2: begin
                    if (bank == 0 && index == 0)
                        crt_chip_layout_supported = address == 16'h8000 &&
                            (image_size == 16'h2000 ||
                             image_size == 16'h4000);
                    else if (bank == 0 && index == 1)
                        crt_chip_layout_supported = address == 16'ha000 &&
                            image_size == 16'h2000;
                end
                MT_CRT_OCEAN_TYPE_1:
                    crt_chip_layout_supported = indexed_8k_layout(
                        index, bank, address, image_size, 9'd64);
                MT_CRT_FUN_PLAY:
                    crt_chip_layout_supported = indexed_8k_layout(
                        index, bank, address, image_size, 9'd16);
                MT_CRT_SUPER_GAMES:
                    crt_chip_layout_supported = indexed_16k_layout(
                        index, bank, address, image_size, 9'd4);
                MT_CRT_EPYX_FASTLOAD,
                MT_CRT_MIKRO_ASSEMBLER:
                    crt_chip_layout_supported = indexed_8k_layout(
                        index, bank, address, image_size, 9'd1);
                MT_CRT_C64_GAME_SYSTEM:
                    crt_chip_layout_supported = indexed_8k_layout(
                        index, bank, address, image_size, 9'd64);
                MT_CRT_DINAMIC:
                    crt_chip_layout_supported = indexed_8k_layout(
                        index, bank, address, image_size, 9'd16);
                MT_CRT_ZAXXON: begin
                    if (index == 0)
                        crt_chip_layout_supported = bank == 0 &&
                            address == 16'h8000 && image_size == 16'h1000;
                    else if (index == 1)
                        crt_chip_layout_supported = bank == 0 &&
                            address == 16'ha000 && image_size == 16'h2000;
                    else if (index == 2)
                        crt_chip_layout_supported = bank == 1 &&
                            address == 16'ha000 && image_size == 16'h2000;
                end
                MT_CRT_MAGIC_DESK:
                    crt_chip_layout_supported = indexed_8k_layout(
                        index, bank, address, image_size, 9'd128);
                MT_CRT_COMAL_80:
                    crt_chip_layout_supported = indexed_16k_layout(
                        index, bank, address, image_size, 9'd4);
                MT_CRT_WATERLOO_BASIC:
                    crt_chip_layout_supported = indexed_8k_layout(
                        index, bank, address, image_size, 9'd2);
                MT_CRT_PROPHET64:
                    crt_chip_layout_supported = indexed_8k_layout(
                        index, bank, address, image_size, 9'd32);
                MT_CRT_RGCD:
                    crt_chip_layout_supported = indexed_8k_layout(
                        index, bank, address, image_size, 9'd8);
                MT_CRT_MAGIC_DESK_2:
                    crt_chip_layout_supported = indexed_16k_layout(
                        index, bank, address, image_size, 9'd128);
                default: ;
            endcase
        end
    endfunction

    function automatic logic crt_layout_complete(
        input logic [7:0] hardware_type,
        input logic [8:0] count,
        input logic first_16k
    );
        case (hardware_type)
            MT_CRT_NORMAL:
                crt_layout_complete = count == 1 || count == 2;
            MT_CRT_SIMONS_BASIC:
                crt_layout_complete =
                    (count == 1 && first_16k) ||
                    (count == 2 && !first_16k);
            MT_CRT_FINAL_CARTRIDGE_1_2:
                crt_layout_complete =
                    (count == 1 && first_16k) ||
                    (count == 2 && !first_16k);
            MT_CRT_OCEAN_TYPE_1:
                crt_layout_complete =
                    count == 16 || count == 32 || count == 64;
            MT_CRT_FUN_PLAY,
            MT_CRT_DINAMIC:
                crt_layout_complete = count == 16;
            MT_CRT_SUPER_GAMES,
            MT_CRT_COMAL_80:
                crt_layout_complete = count == 4;
            MT_CRT_EPYX_FASTLOAD,
            MT_CRT_MIKRO_ASSEMBLER:
                crt_layout_complete = count == 1;
            MT_CRT_C64_GAME_SYSTEM:
                crt_layout_complete = count == 64;
            MT_CRT_ZAXXON:
                crt_layout_complete = count == 3;
            MT_CRT_WATERLOO_BASIC:
                crt_layout_complete = count == 2;
            MT_CRT_PROPHET64:
                crt_layout_complete = count == 32;
            MT_CRT_RGCD:
                crt_layout_complete = count == 8;
            MT_CRT_MAGIC_DESK,
            MT_CRT_MAGIC_DESK_2:
                crt_layout_complete = count != 0 && count <= 128 &&
                    (count & (count - 1'b1)) == 0;
            default:
                crt_layout_complete = 1'b0;
        endcase
    endfunction

    function automatic logic global_header_byte_good(
        input logic [5:0] offset,
        input logic [7:0] value
    );
        begin
            global_header_byte_good = 1'b1;
            if (offset < 16)
                global_header_byte_good =
                    value == CRT_MAGIC[(15-offset)*8 +: 8];
            else begin
                case (offset)
                    16, 17, 18: global_header_byte_good = value == 8'h00;
                    19: global_header_byte_good = value == 8'h40;
                    20: global_header_byte_good = value == 8'h01;
                    21: global_header_byte_good = value <= 8'h01;
                    22: global_header_byte_good = value == 8'h00;
                    23: global_header_byte_good = crt_type_supported(value);
                    24, 25: global_header_byte_good = value <= 8'h01;
                    default: global_header_byte_good = 1'b1;
                endcase
            end
        end
    endfunction

    function automatic logic chip_magic_byte_good(
        input logic [1:0] offset,
        input logic [7:0] value
    );
        chip_magic_byte_good = value == CHIP_MAGIC[(3-offset)*8 +: 8];
    endfunction

    assign load_ready = !reset && !loader_memory_valid &&
                        (state == STATE_IDLE || state == STATE_STREAM);
    assign core_reset_hold = state == STATE_PRG_RESET ||
        ((state == STATE_STREAM || state == STATE_CRT_FINISH) &&
         active_kind == MT_LOAD_C64_CRT);
    assign prg_install_pause =
        (state == STATE_STREAM && active_kind == MT_LOAD_C64_PRG) ||
        state == STATE_PRG_POINTERS || state == STATE_PRG_FINISH;

    always_ff @(posedge clk) begin : receive_content
        logic framing_good;
        logic final_beat;
        logic byte_good;
        logic [16:0] prg_limit;
        logic [15:0] final_image_size;
        logic [31:0] final_packet_size;
        logic [15:0] final_chip_address;
        logic [21:0] aligned_store_offset;
        logic chip_header_good;

        prg_ram_wr      <= 1'b0;
        prg_run_request <= 1'b0;
        cart_bank_wr    <= 1'b0;

        if (reset) begin
            state                 <= STATE_IDLE;
            active_kind           <= 8'd0;
            active_total          <= 32'd0;
            expected_offset       <= 32'd0;
            object_error          <= 1'b0;
            prg_base              <= 16'd0;
            prg_end               <= 16'd0;
            pointer_index         <= 4'd0;
            prg_reset_count       <= 8'd0;
            prg_ram_addr          <= 16'd0;
            prg_ram_data          <= 8'd0;
            prg_loaded            <= 1'b0;
            loader_memory_addr    <= 24'd0;
            loader_memory_write   <= 1'b1;
            loader_memory_wdata   <= 64'd0;
            loader_memory_wstrb   <= 8'd0;
            loader_memory_valid   <= 1'b0;
            cart_loading          <= 1'b0;
            cart_loaded           <= 1'b0;
            cart_id               <= 8'hff;
            cart_exrom            <= 1'b1;
            cart_game             <= 1'b1;
            cart_bank_hi          <= 1'b0;
            cart_bank_16k         <= 1'b0;
            cart_bank_num         <= 8'd0;
            cart_bank_addr        <= 8'd0;
            chip_header_index     <= 4'd0;
            chip_packet_size      <= 32'd0;
            chip_bank             <= 8'd0;
            chip_address          <= 16'd0;
            chip_image_size       <= 16'd0;
            chip_bytes_remaining  <= 16'd0;
            chip_count            <= 9'd0;
            first_chip_16k        <= 1'b0;
            crt_store_offset      <= 22'd0;
            low_bank_seen         <= 256'd0;
            high_bank_seen        <= 256'd0;
            active                <= 1'b0;
            error                 <= 1'b0;
        end else begin
            if (clear_error) error <= 1'b0;

            if (loader_memory_valid && loader_memory_ready)
                loader_memory_valid <= 1'b0;

            if (memory_fault && !clear_error) begin
                loader_memory_valid <= 1'b0;
                cart_loading <= 1'b0;
                cart_loaded <= 1'b0;
                cart_id <= 8'hff;
                active <= 1'b0;
                state <= STATE_IDLE;
                error <= 1'b1;
            end else if (state == STATE_PRG_RESET) begin
                if (prg_reset_count == 0)
                    state <= STATE_PRG_WAIT_BASIC;
                else
                    prg_reset_count <= prg_reset_count - 1'd1;
            end else if (state == STATE_PRG_WAIT_BASIC) begin
                // Match the pinned MiSTer integration: resume the PRG stream
                // only once the freshly booted KERNAL reaches $FFCF.
                if (core_addr == 16'hffcf) state <= STATE_STREAM;
            end else if (state == STATE_PRG_POINTERS) begin
                prg_ram_wr <= 1'b1;
                case (pointer_index)
                    0:  begin prg_ram_addr <= 16'h002b; prg_ram_data <= 8'h01; end
                    1:  begin prg_ram_addr <= 16'h002c; prg_ram_data <= 8'h08; end
                    2:  begin prg_ram_addr <= 16'h00ac; prg_ram_data <= 8'h00; end
                    3:  begin prg_ram_addr <= 16'h00ad; prg_ram_data <= 8'h00; end
                    4:  begin prg_ram_addr <= 16'h002d; prg_ram_data <= prg_end[7:0]; end
                    5:  begin prg_ram_addr <= 16'h002e; prg_ram_data <= prg_end[15:8]; end
                    6:  begin prg_ram_addr <= 16'h002f; prg_ram_data <= prg_end[7:0]; end
                    7:  begin prg_ram_addr <= 16'h0030; prg_ram_data <= prg_end[15:8]; end
                    8:  begin prg_ram_addr <= 16'h0031; prg_ram_data <= prg_end[7:0]; end
                    9:  begin prg_ram_addr <= 16'h0032; prg_ram_data <= prg_end[15:8]; end
                    10: begin prg_ram_addr <= 16'h00ae; prg_ram_data <= prg_end[7:0]; end
                    default: begin prg_ram_addr <= 16'h00af; prg_ram_data <= prg_end[15:8]; end
                endcase
                if (pointer_index == 4'd11) begin
                    state <= STATE_PRG_FINISH;
                end else begin
                    pointer_index <= pointer_index + 4'd1;
                end
            end else if (state == STATE_PRG_FINISH) begin
                // prg_ram_wr is registered. Keep the core paused for this
                // drain cycle so the final $00AF write reaches main RAM.
                state <= STATE_IDLE;
                active <= 1'b0;
                prg_loaded <= 1'b1;
                prg_run_request <= 1'b1;
            end else if (state == STATE_CRT_FINISH) begin
                if (!loader_memory_valid) begin
                    state <= STATE_IDLE;
                    active <= 1'b0;
                    cart_loading <= 1'b0;
                    cart_loaded <= 1'b1;
                end
            end else if (load_valid && load_ready) begin
                final_beat = load_last && load_offset + 32'd1 == load_total;

                if (load_first) begin
                    byte_good = load_offset == 0;
                    if (active) begin
                        error <= 1'b1;
                    end else if (load_kind == MT_LOAD_C64_CRT_DETACH) begin
                        byte_good = byte_good && load_total == 1 &&
                                    load_last && load_data == 0;
                        cart_loading <= 1'b0;
                        cart_loaded <= 1'b0;
                        cart_id <= 8'hff;
                        if (!byte_good) error <= 1'b1;
                    end else if (load_kind == MT_LOAD_C64_PRG) begin
                        byte_good = byte_good && !load_last &&
                                    load_total >= MT_SIZE_C64_PRG_MIN &&
                                    load_total <= MT_SIZE_C64_PRG_MAX;
                        prg_loaded <= 1'b0;
                        if (byte_good) begin
                            state <= STATE_PRG_RESET;
                            active <= 1'b1;
                            active_kind <= load_kind;
                            active_total <= load_total;
                            expected_offset <= 32'd1;
                            object_error <= 1'b0;
                            prg_base[7:0] <= load_data;
                            prg_reset_count <= 8'hff;
                        end else begin
                            state <= STATE_IDLE;
                            active <= 1'b0;
                            error <= 1'b1;
                        end
                    end else if (load_kind == MT_LOAD_C64_CRT) begin
                        byte_good = byte_good && !load_last &&
                                    load_total >= MT_SIZE_C64_CRT_MIN &&
                                    load_total <= MT_SIZE_C64_CRT_MAX &&
                                    global_header_byte_good(0, load_data);
                        cart_loading <= 1'b0;
                        cart_loaded <= 1'b0;
                        cart_id <= 8'hff;
                        if (byte_good) begin
                            state <= STATE_STREAM;
                            active <= 1'b1;
                            active_kind <= load_kind;
                            active_total <= load_total;
                            expected_offset <= 32'd1;
                            object_error <= 1'b0;
                            cart_loading <= 1'b1;
                            chip_header_index <= 4'd0;
                            chip_bytes_remaining <= 16'd0;
                            chip_count <= 9'd0;
                            first_chip_16k <= 1'b0;
                            crt_store_offset <= 22'd0;
                            low_bank_seen <= 256'd0;
                            high_bank_seen <= 256'd0;
                        end else begin
                            state <= STATE_IDLE;
                            active <= 1'b0;
                            error <= 1'b1;
                        end
                    end else begin
                        error <= 1'b1;
                    end
                end else if (state == STATE_STREAM && active) begin
                    framing_good = !object_error &&
                                   load_kind == active_kind &&
                                   load_total == active_total &&
                                   load_offset == expected_offset &&
                                   (load_last ==
                                    (load_offset + 32'd1 == active_total));
                    byte_good = framing_good;

                    if (active_kind == MT_LOAD_C64_PRG) begin
                        if (load_offset == 1) begin
                            prg_limit = {1'b0, load_data, prg_base[7:0]} +
                                        active_total - 32'd2;
                            byte_good = byte_good && prg_limit <= 17'h10000;
                            prg_base[15:8] <= load_data;
                            prg_end <= prg_limit[15:0];
                        end else if (byte_good) begin
                            prg_ram_wr <= 1'b1;
                            prg_ram_addr <= prg_base + load_offset - 32'd2;
                            prg_ram_data <= load_data;
                        end

                        if (!byte_good) begin
                            error <= 1'b1;
                            object_error <= 1'b1;
                        end
                        if (load_last) begin
                            object_error <= 1'b0;
                            if (byte_good && final_beat) begin
                                state <= STATE_PRG_POINTERS;
                                pointer_index <= 4'd0;
                            end else begin
                                state <= STATE_IDLE;
                                active <= 1'b0;
                            end
                        end else begin
                            expected_offset <= expected_offset + 32'd1;
                        end
                    end else if (active_kind == MT_LOAD_C64_CRT) begin
                        if (load_offset < 32'd64) begin
                            byte_good = byte_good &&
                                global_header_byte_good(load_offset[5:0],
                                                        load_data);
                            if (load_offset == 23) cart_id <= load_data;
                            if (load_offset == 24) cart_exrom <= load_data[0];
                            if (load_offset == 25) begin
                                byte_good = byte_good &&
                                    crt_profile_lines_supported(
                                        cart_id, cart_exrom, load_data[0]);
                                cart_game <= load_data[0];
                            end
                        end else if (chip_bytes_remaining == 0) begin
                            chip_header_good = 1'b1;
                            if (chip_header_index < 4)
                                chip_header_good = chip_magic_byte_good(
                                    chip_header_index[1:0], load_data);
                            case (chip_header_index)
                                4: chip_packet_size[31:24] <= load_data;
                                5: chip_packet_size[23:16] <= load_data;
                                6: chip_packet_size[15:8] <= load_data;
                                7: chip_packet_size[7:0] <= load_data;
                                8, 9: chip_header_good = load_data == 0;
                                10: chip_header_good = load_data == 0;
                                11: chip_bank <= load_data;
                                12: chip_address[15:8] <= load_data;
                                13: chip_address[7:0] <= load_data;
                                14: chip_image_size[15:8] <= load_data;
                                15: begin
                                    final_image_size =
                                        {chip_image_size[15:8], load_data};
                                    final_packet_size = chip_packet_size;
                                    final_chip_address = chip_address;
                                    aligned_store_offset =
                                        (crt_store_offset + 22'd8191) &
                                        22'h3fe000;
                                    chip_header_good =
                                        final_packet_size ==
                                            final_image_size + 32'd16 &&
                                        (final_image_size == 16'h1000 ||
                                         final_image_size == 16'h2000 ||
                                         final_image_size == 16'h4000) &&
                                        (final_chip_address == 16'h8000 ||
                                         final_chip_address == 16'ha000 ||
                                         final_chip_address == 16'he000) &&
                                        (final_image_size != 16'h4000 ||
                                         final_chip_address == 16'h8000) &&
                                        (final_image_size != 16'h1000 ||
                                         cart_id == MT_CRT_ZAXXON) &&
                                        {1'b0, final_chip_address} +
                                            final_image_size <= 17'h10000 &&
                                        {1'b0, aligned_store_offset} +
                                            final_image_size <= 23'h200000 &&
                                        crt_chip_layout_supported(
                                            cart_id, chip_count, chip_bank,
                                            final_chip_address,
                                            final_image_size, cart_exrom,
                                            cart_game) &&
                                        !(final_chip_address == 16'h8000 ?
                                          low_bank_seen[chip_bank] :
                                          high_bank_seen[chip_bank]) &&
                                        !(final_image_size == 16'h4000 &&
                                          high_bank_seen[chip_bank]);
                                    if (byte_good && chip_header_good) begin
                                        cart_bank_hi <=
                                            final_chip_address != 16'h8000;
                                        cart_bank_16k <=
                                            final_image_size == 16'h4000;
                                        cart_bank_num <= chip_bank;
                                        cart_bank_addr <=
                                            aligned_store_offset[20:13];
                                        cart_bank_wr <= 1'b1;
                                        crt_store_offset <=
                                            aligned_store_offset;
                                        if (final_chip_address == 16'h8000)
                                            low_bank_seen[chip_bank] <= 1'b1;
                                        else
                                            high_bank_seen[chip_bank] <= 1'b1;
                                        if (final_image_size == 16'h4000)
                                            high_bank_seen[chip_bank] <= 1'b1;
                                        chip_bytes_remaining <=
                                            final_image_size;
                                        if (chip_count == 0)
                                            first_chip_16k <=
                                                final_image_size == 16'h4000;
                                        chip_count <= chip_count + 1'd1;
                                    end
                                end
                                default: ;
                            endcase
                            byte_good = byte_good && chip_header_good;
                            if (chip_header_index == 15)
                                chip_header_index <= 0;
                            else
                                chip_header_index <= chip_header_index + 1'd1;
                        end else if (byte_good) begin
                            loader_memory_addr <=
                                {3'b001, crt_store_offset[20:3], 3'b000};
                            loader_memory_write <= 1'b1;
                            loader_memory_wdata <=
                                {{56{1'b0}}, load_data} <<
                                (crt_store_offset[2:0] * 8);
                            loader_memory_wstrb <=
                                8'b00000001 << crt_store_offset[2:0];
                            loader_memory_valid <= 1'b1;
                            crt_store_offset <= crt_store_offset + 1'd1;
                            chip_bytes_remaining <=
                                chip_bytes_remaining - 1'd1;
                        end

                        if (!byte_good) begin
                            error <= 1'b1;
                            object_error <= 1'b1;
                            cart_loading <= 1'b0;
                            cart_loaded <= 1'b0;
                            cart_id <= 8'hff;
                        end
                        if (load_last) begin
                            object_error <= 1'b0;
                            if (byte_good && final_beat &&
                                chip_bytes_remaining == 1 &&
                                crt_layout_complete(
                                    cart_id, chip_count,
                                    first_chip_16k)) begin
                                state <= STATE_CRT_FINISH;
                            end else begin
                                state <= STATE_IDLE;
                                active <= 1'b0;
                                cart_loading <= 1'b0;
                                cart_loaded <= 1'b0;
                                cart_id <= 8'hff;
                                error <= 1'b1;
                            end
                        end else begin
                            expected_offset <= expected_offset + 32'd1;
                        end
                    end
                end else begin
                    error <= 1'b1;
                end
            end
        end
    end
endmodule
