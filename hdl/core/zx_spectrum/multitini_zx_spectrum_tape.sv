// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module multitini_zx_spectrum_tape (
    input  wire        clk, reset, tick, instant_tick, paused,
    input  wire        toggle, rewind,
    input  wire        loaded, tzx, mode_128,
    input  wire [17:0] size,
    input  wire        write_enable,
    input  wire [16:0] write_address,
    input  wire [7:0]  write_data,
    input  wire        instant_start, instant_next, instant_finish,
    output wire        instant_available, instant_byte_valid,
    output wire [7:0]  instant_byte,
    output wire        ear, playing, underflow
);
    (* ram_style = "block" *) reg [7:0] data [0:131071];
    reg [17:0] address;
    reg [7:0] read_data;
    reg tap_ear, tap_playing;
    wire [16:0] tzx_read_address;
    wire tzx_ear, tzx_playing, tzx_underflow;
    always @(posedge clk) begin
        if (write_enable) data[write_address] <= write_data;
        read_data <= data[tzx ? tzx_read_address : address[16:0]];
    end
    assign ear = tzx ? tzx_ear : tap_ear;
    assign playing = tzx ? tzx_playing : tap_playing;
    assign underflow = tzx && tzx_underflow;

    multitini_zx_spectrum_tzx tzx_player (
        .clk(clk), .reset(reset), .tick(tick), .paused(paused), .toggle(toggle), .rewind(rewind),
        .loaded(loaded && tzx), .mode_128(mode_128), .size(size),
        .read_address(tzx_read_address), .read_data(read_data),
        .ear(tzx_ear), .playing(tzx_playing), .underflow(tzx_underflow)
    );

    typedef enum logic [3:0] {
        LENGTH_LO, LENGTH_HI, FIRST_BYTE, PILOT, SYNC_1, SYNC_2,
        DATA_FIRST, DATA_SECOND, NEXT_BYTE, GAP, END_OF_TAPE
    } tape_state_t;
    tape_state_t state;
    reg [21:0] remaining_ticks;
    reg [15:0] remaining_bytes;
    reg [12:0] remaining_pulses;
    reg [7:0] shift_data;
    reg [2:0] bit_index;
    reg [17:0] block_start, instant_block_end;
    typedef enum logic [2:0] {
        DIRECT_IDLE, DIRECT_START, DIRECT_LEN_LO, DIRECT_LEN_HI,
        DIRECT_WAIT_BYTE, DIRECT_BYTE, DIRECT_END
    } direct_state_t;
    direct_state_t direct_state;

    // The cursor denotes the first untouched block. Pilot and sync have
    // sent no bytes yet, and the gap follows a fully consumed block.
    assign instant_available = loaded && !tzx && block_start < size && direct_state == DIRECT_IDLE &&
        (state <= SYNC_2 || state == GAP);
    assign instant_byte_valid = direct_state == DIRECT_BYTE;
    assign instant_byte = read_data;

    function automatic [21:0] pulse_length(input bit value);
        pulse_length = value ? 22'd1710 : 22'd855;
    endfunction

    always @(posedge clk) begin
        if (reset || rewind || !loaded || tzx || (toggle && state == END_OF_TAPE)) begin
            state <= LENGTH_LO;
            address <= 0;
            block_start <= 0;
            direct_state <= DIRECT_IDLE;
            tap_ear <= 0;
            tap_playing <= loaded && !tzx && toggle && !reset && !rewind;
            remaining_ticks <= 0;
        end else if (instant_start) begin
            state <= LENGTH_LO;
            direct_state <= DIRECT_START;
            address <= block_start;
            instant_block_end <= block_start;
            tap_ear <= 0;
            tap_playing <= 0;
            remaining_ticks <= 0;
        end else if (direct_state != DIRECT_IDLE) begin
            if (instant_finish) begin
                state <= instant_block_end == size ? END_OF_TAPE : LENGTH_LO;
                direct_state <= DIRECT_IDLE;
                address <= instant_block_end;
                block_start <= instant_block_end;
            end else if (instant_next && direct_state == DIRECT_BYTE) begin
                address <= address + 1'b1;
                direct_state <= address + 1'b1 == instant_block_end ? DIRECT_END : DIRECT_WAIT_BYTE;
            end else if (instant_tick && !paused) begin
                case (direct_state)
                    DIRECT_START: direct_state <= DIRECT_LEN_LO;
                    DIRECT_LEN_LO: begin
                        remaining_bytes[7:0] <= read_data;
                        address <= address + 1'b1;
                        direct_state <= DIRECT_LEN_HI;
                    end
                    DIRECT_LEN_HI: begin
                        // The image loader has already validated this length.
                        instant_block_end <= address + 1'b1 + {2'b0, read_data, remaining_bytes[7:0]};
                        address <= address + 1'b1;
                        direct_state <= DIRECT_WAIT_BYTE;
                    end
                    DIRECT_WAIT_BYTE: direct_state <= DIRECT_BYTE;
                    default: ;
                endcase
            end
        end else if (toggle) begin
            tap_playing <= !tap_playing;
        end else if (tick && tap_playing && !paused) begin
            if (remaining_ticks > 1) begin
                remaining_ticks <= remaining_ticks - 1'b1;
            end else begin
                case (state)
                    LENGTH_LO: begin
                        remaining_bytes[7:0] <= read_data;
                        address <= address + 1'b1;
                        state <= LENGTH_HI;
                    end
                    LENGTH_HI: begin
                        remaining_bytes[15:8] <= read_data;
                        address <= address + 1'b1;
                        state <= FIRST_BYTE;
                    end
                    FIRST_BYTE: begin
                        shift_data <= read_data;
                        remaining_pulses <= read_data[7] ? 13'd3223 : 13'd8063;
                        remaining_ticks <= 2168;
                        bit_index <= 7;
                        state <= PILOT;
                    end
                    PILOT: begin
                        tap_ear <= !tap_ear;
                        remaining_pulses <= remaining_pulses - 1'b1;
                        if (remaining_pulses == 1) begin
                            remaining_ticks <= 667;
                            state <= SYNC_1;
                        end else remaining_ticks <= 2168;
                    end
                    SYNC_1: begin
                        tap_ear <= !tap_ear;
                        remaining_ticks <= 735;
                        state <= SYNC_2;
                    end
                    SYNC_2: begin
                        tap_ear <= !tap_ear;
                        remaining_ticks <= pulse_length(shift_data[7]);
                        state <= DATA_FIRST;
                    end
                    DATA_FIRST: begin
                        tap_ear <= !tap_ear;
                        remaining_ticks <= pulse_length(shift_data[7]);
                        state <= DATA_SECOND;
                    end
                    DATA_SECOND: begin
                        tap_ear <= !tap_ear;
                        if (bit_index != 0) begin
                            bit_index <= bit_index - 1'b1;
                            shift_data <= {shift_data[6:0], 1'b0};
                            remaining_ticks <= pulse_length(shift_data[6]);
                            state <= DATA_FIRST;
                        end else begin
                            address <= address + 1'b1;
                            remaining_bytes <= remaining_bytes - 1'b1;
                            if (remaining_bytes == 1) begin
                                remaining_ticks <= 3500000;
                                block_start <= address + 1'b1;
                                state <= GAP;
                            end else begin
                                remaining_ticks <= 0;
                                state <= NEXT_BYTE;
                            end
                        end
                    end
                    NEXT_BYTE: begin
                        shift_data <= read_data;
                        bit_index <= 7;
                        // The byte fetch consumed one T-state of this pulse.
                        remaining_ticks <= pulse_length(read_data[7]) - 1'b1;
                        state <= DATA_FIRST;
                    end
                    GAP: begin
                        tap_ear <= 0;
                        remaining_ticks <= 0;
                        if (address == size) begin
                            tap_playing <= 0;
                            state <= END_OF_TAPE;
                        end else state <= LENGTH_LO;
                    end
                    default: tap_playing <= 0;
                endcase
            end
        end
    end
endmodule
