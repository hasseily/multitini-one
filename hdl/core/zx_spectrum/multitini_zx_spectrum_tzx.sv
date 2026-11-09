// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// The ARM expands TZX control flow and emits bounded little-endian records.
// Decode ahead of the tape timer so consecutive records share a T-state
// boundary. The caller holds the next CPU phase if the event queue empties.
module multitini_zx_spectrum_tzx (
    input  wire        clk, reset, tick, paused, toggle, rewind, loaded, mode_128,
    input  wire [17:0] size,
    output wire [16:0] read_address,
    input  wire [7:0]  read_data,
    output reg         ear, playing,
    output wire        underflow
);
    localparam [7:0] TONE = 1, PULSES = 2, PWM = 3, PAUSE = 4,
        DIRECT = 5, LEVEL = 6, STOP_48 = 7;
    localparam [15:0] TICKS_PER_MS = 3500;

    typedef enum logic [4:0] {
        RECORD, FIRST_LO, FIRST_HI, SECOND_LO, SECOND_HI, TONE_EVENT,
        PULSE_COUNT, PULSE_LO, PULSE_HI, USED_BITS, LENGTH_LO, LENGTH_MID,
        LENGTH_HI, DATA_BYTE, DATA_EVENT, SET_LEVEL, STOP_EVENT, END_EVENT, DONE
    } decode_state_t;
    typedef enum logic [1:0] {WAVE, SILENCE, STOP, END_OF_TAPE} event_kind_t;
    typedef struct packed {
        event_kind_t kind;
        logic level;
        logic [15:0] duration;
    } wave_event_t;

    decode_state_t decode_state;
    reg [7:0] command, word_low, data_shift;
    reg [15:0] period_zero, period_one, repeat_count, length_low;
    reg [17:0] offset, bytes_left;
    reg [3:0] bits_left, last_bits;
    reg read_ready, source_level, second_pulse;
    wire [15:0] word_value = {read_data, word_low};
    wire [23:0] payload_length = {read_data, length_low};
    wire [24:0] payload_end = {7'd0, offset} + 25'd1 + {1'b0, payload_length};
    wire have_byte = read_ready && offset < size;

    wave_event_t events [0:7];
    wave_event_t event_in;
    reg [3:0] events_write, events_read;
    wire events_valid = events_write != events_read;
    wire events_full = events_write == (events_read ^ 4'b1000);
    wire wave_event_t event_out = events[events_read[2:0]];
    reg event_valid, needs_byte;
    wire push_event = event_valid && !events_full;
    wire read_byte = needs_byte && have_byte && (!event_valid || push_event);
    // The BRAM returns the next byte on the same edge that consumes this one.
    wire [17:0] next_offset = offset + 1'b1;
    assign read_address = read_byte ? next_offset[16:0] : offset[16:0];

    always @* begin
        needs_byte = 0;
        event_valid = 0;
        event_in = {WAVE, source_level, period_zero};
        case (decode_state)
            RECORD, FIRST_LO, SECOND_LO, SECOND_HI, PULSE_COUNT, PULSE_LO,
            USED_BITS, LENGTH_LO, LENGTH_MID, LENGTH_HI, DATA_BYTE, SET_LEVEL:
                needs_byte = 1;
            FIRST_HI: begin
                needs_byte = 1;
                if (command == PAUSE) begin
                    event_valid = have_byte;
                    event_in = {word_value == 0 ? STOP : SILENCE, source_level, word_value};
                end
            end
            TONE_EVENT: event_valid = 1;
            PULSE_HI: begin
                needs_byte = 1;
                event_valid = have_byte && word_value != 0;
                event_in.duration = word_value;
            end
            DATA_EVENT: begin
                event_valid = 1;
                if (command == DIRECT) event_in.level = data_shift[7];
                else event_in.duration = data_shift[7] ? period_one : period_zero;
            end
            STOP_EVENT: begin
                event_valid = 1;
                event_in.kind = STOP;
            end
            END_EVENT: begin
                event_valid = 1;
                event_in.kind = END_OF_TAPE;
            end
            default: ;
        endcase
    end

    reg at_end;
    reg [15:0] ticks_left, milliseconds_left;
    wire restart = reset || rewind || !loaded || (toggle && at_end);
    wire boundary = ticks_left <= 1 && milliseconds_left == 0;
    assign underflow = playing && !paused && boundary && !events_valid;
    wire pop_event = tick && playing && !paused && boundary && events_valid;

    always @(posedge clk) begin
        if (restart) begin
            events_write <= 0;
            events_read <= 0;
        end else begin
            if (push_event) begin
                events[events_write[2:0]] <= event_in;
                events_write <= events_write + 1'b1;
            end
            if (pop_event) events_read <= events_read + 1'b1;
        end
    end

    always @(posedge clk) begin
        if (restart) begin
            offset <= 0;
            read_ready <= 0;
            decode_state <= RECORD;
            source_level <= 0;
        end else begin
            read_ready <= 1;
            if (read_byte) offset <= next_offset;
            if (needs_byte && read_ready && !have_byte) decode_state <= END_EVENT;
            else if (read_byte || push_event) begin
                case (decode_state)
                    RECORD: begin
                        command <= read_data;
                        case (read_data)
                            TONE, PWM, PAUSE, DIRECT: decode_state <= FIRST_LO;
                            PULSES: decode_state <= PULSE_COUNT;
                            LEVEL: decode_state <= SET_LEVEL;
                            STOP_48: decode_state <= mode_128 ? RECORD : STOP_EVENT;
                            default: decode_state <= END_EVENT;
                        endcase
                    end
                    FIRST_LO: begin
                        word_low <= read_data;
                        decode_state <= FIRST_HI;
                    end
                    FIRST_HI: begin
                        period_zero <= word_value;
                        if (command == PAUSE) begin
                            if (word_value != 0) source_level <= 0;
                            decode_state <= RECORD;
                        end else if (word_value == 0) decode_state <= END_EVENT;
                        else decode_state <= command == DIRECT ? USED_BITS : SECOND_LO;
                    end
                    SECOND_LO: begin
                        word_low <= read_data;
                        decode_state <= SECOND_HI;
                    end
                    SECOND_HI: begin
                        period_one <= word_value;
                        repeat_count <= word_value;
                        if (word_value == 0) decode_state <= END_EVENT;
                        else decode_state <= command == TONE ? TONE_EVENT : USED_BITS;
                    end
                    TONE_EVENT: begin
                        source_level <= !source_level;
                        repeat_count <= repeat_count - 1'b1;
                        if (repeat_count == 1) decode_state <= RECORD;
                    end
                    PULSE_COUNT: begin
                        repeat_count <= {8'd0, read_data};
                        decode_state <= read_data == 0 ? RECORD : PULSE_LO;
                    end
                    PULSE_LO: begin
                        word_low <= read_data;
                        decode_state <= PULSE_HI;
                    end
                    PULSE_HI: begin
                        if (word_value == 0) decode_state <= END_EVENT;
                        else begin
                            source_level <= !source_level;
                            repeat_count <= repeat_count - 1'b1;
                            decode_state <= repeat_count == 1 ? RECORD : PULSE_LO;
                        end
                    end
                    USED_BITS: begin
                        last_bits <= read_data[3:0];
                        decode_state <= read_data == 0 || read_data > 8 ? END_EVENT : LENGTH_LO;
                    end
                    LENGTH_LO: begin
                        length_low[7:0] <= read_data;
                        decode_state <= LENGTH_MID;
                    end
                    LENGTH_MID: begin
                        length_low[15:8] <= read_data;
                        decode_state <= LENGTH_HI;
                    end
                    LENGTH_HI: begin
                        bytes_left <= payload_length[17:0];
                        decode_state <= payload_length == 0 || payload_end > {7'd0, size} ? END_EVENT : DATA_BYTE;
                    end
                    DATA_BYTE: begin
                        data_shift <= read_data;
                        bits_left <= bytes_left == 1 ? last_bits : 4'd8;
                        bytes_left <= bytes_left - 1'b1;
                        second_pulse <= 0;
                        decode_state <= DATA_EVENT;
                    end
                    DATA_EVENT: begin
                        source_level <= command == DIRECT ? data_shift[7] : !source_level;
                        second_pulse <= !second_pulse;
                        if (command == DIRECT || second_pulse) begin
                            data_shift <= {data_shift[6:0], 1'b0};
                            bits_left <= bits_left - 1'b1;
                            if (bits_left == 1) decode_state <= bytes_left == 0 ? RECORD : DATA_BYTE;
                        end
                    end
                    SET_LEVEL: begin
                        source_level <= read_data[0];
                        decode_state <= read_data > 1 ? END_EVENT : RECORD;
                    end
                    STOP_EVENT: decode_state <= RECORD;
                    END_EVENT: decode_state <= DONE;
                    default: ;
                endcase
            end
        end
    end

    always @(posedge clk) begin
        if (restart) begin
            ear <= 0;
            playing <= loaded && toggle && !reset && !rewind;
            at_end <= 0;
            ticks_left <= 0;
            milliseconds_left <= 0;
        end else if (toggle) begin
            playing <= !playing;
        end else if (tick && playing && !paused) begin
            if (ticks_left > 1) ticks_left <= ticks_left - 1'b1;
            else if (milliseconds_left != 0) begin
                // Finish the previous edge before lowering EAR for silence.
                ear <= 0;
                ticks_left <= TICKS_PER_MS;
                milliseconds_left <= milliseconds_left - 1'b1;
            end else if (events_valid) begin
                ear <= event_out.level;
                case (event_out.kind)
                    WAVE: ticks_left <= event_out.duration;
                    SILENCE: begin
                        ticks_left <= TICKS_PER_MS;
                        milliseconds_left <= event_out.duration - 1'b1;
                    end
                    default: begin
                        ticks_left <= 0;
                        playing <= 0;
                        at_end <= event_out.kind == END_OF_TAPE;
                    end
                endcase
            end
        end
    end
endmodule
