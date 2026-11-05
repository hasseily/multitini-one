// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module multitini_zx_spectrum_tape (
    input  wire        clk, reset, tick, paused,
    input  wire        toggle, rewind,
    input  wire        loaded,
    input  wire [17:0] size,
    input  wire        write_enable,
    input  wire [16:0] write_address,
    input  wire [7:0]  write_data,
    output reg         ear
);
    (* ram_style = "block" *) reg [7:0] data [0:131071];
    reg [17:0] address;
    reg [7:0] read_data;
    always @(posedge clk) begin
        if (write_enable) data[write_address] <= write_data;
        read_data <= data[address[16:0]];
    end

    typedef enum logic [3:0] {
        LENGTH_LO, LENGTH_HI, FIRST_BYTE, PILOT, SYNC_1, SYNC_2,
        DATA_FIRST, DATA_SECOND, NEXT_BYTE, GAP, END_OF_TAPE
    } tape_state_t;
    tape_state_t state;
    reg playing;
    reg [21:0] remaining_ticks;
    reg [15:0] remaining_bytes;
    reg [12:0] remaining_pulses;
    reg [7:0] shift_data;
    reg [2:0] bit_index;

    function automatic [21:0] pulse_length(input bit value);
        pulse_length = value ? 22'd1710 : 22'd855;
    endfunction

    always @(posedge clk) begin
        if (reset || rewind || !loaded || (toggle && state == END_OF_TAPE)) begin
            state <= LENGTH_LO;
            address <= 0;
            ear <= 0;
            playing <= loaded && toggle && !reset && !rewind;
            remaining_ticks <= 0;
        end else if (toggle) begin
            playing <= !playing;
        end else if (tick && playing && !paused) begin
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
                        ear <= !ear;
                        remaining_pulses <= remaining_pulses - 1'b1;
                        if (remaining_pulses == 1) begin
                            remaining_ticks <= 667;
                            state <= SYNC_1;
                        end else remaining_ticks <= 2168;
                    end
                    SYNC_1: begin
                        ear <= !ear;
                        remaining_ticks <= 735;
                        state <= SYNC_2;
                    end
                    SYNC_2: begin
                        ear <= !ear;
                        remaining_ticks <= pulse_length(shift_data[7]);
                        state <= DATA_FIRST;
                    end
                    DATA_FIRST: begin
                        ear <= !ear;
                        remaining_ticks <= pulse_length(shift_data[7]);
                        state <= DATA_SECOND;
                    end
                    DATA_SECOND: begin
                        ear <= !ear;
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
                        ear <= 0;
                        remaining_ticks <= 0;
                        if (address == size) begin
                            playing <= 0;
                            state <= END_OF_TAPE;
                        end else state <= LENGTH_LO;
                    end
                    default: playing <= 0;
                endcase
            end
        end
    end
endmodule
