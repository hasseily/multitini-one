// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module multitini_zx_spectrum_keyboard (
    input  wire        clk, reset,
    input  wire [15:0] input_code, input_value,
    input  wire        input_valid,
    input  wire [7:0]  row_select,
    output reg  [4:0]  columns,
    output wire [4:0]  kempston,
    output reg         tape_toggle, tape_rewind
);
    reg [255:0] key_down;
    reg [3:0] cursor;
    reg [4:0] joystick [2];
    wire [4:0] pad = joystick[0] | joystick[1];

    // USB button order is up, down, left, right, fire. Kempston exposes
    // right, left, down, up, fire on the CPU's five low data pins.
    assign kempston = {pad[4], pad[0], pad[1], pad[2], pad[3]};

    always @(posedge clk) begin
        tape_toggle <= 0;
        tape_rewind <= 0;
        if (reset) begin
            key_down <= 0;
            cursor <= 0;
            joystick[0] <= 0;
            joystick[1] <= 0;
        end else if (input_valid) begin
            case (input_code[15:8])
                8'h00: begin
                    if (input_value[1]) begin
                        case (input_code[7:0])
                            8'h6b: cursor[0] <= input_value[0]; // Left
                            8'h72: cursor[1] <= input_value[0]; // Down
                            8'h75: cursor[2] <= input_value[0]; // Up
                            8'h74: cursor[3] <= input_value[0]; // Right
                            default: ;
                        endcase
                    end else begin
                        key_down[input_code[7:0]] <= input_value[0];
                        tape_toggle <= input_code[7:0] == 8'h05 && input_value[0] && !key_down[8'h05];
                        tape_rewind <= input_code[7:0] == 8'h06 && input_value[0] && !key_down[8'h06];
                    end
                end
                8'h01, 8'h02: if (input_code[7:0] < 5)
                    joystick[input_code[8] ? 0 : 1][input_code[2:0]] <= input_value[0];
                default: ;
            endcase
        end
    end

    // A15..A8 select any combination of active-low rows. Keeping each host
    // key's state separate preserves a chord when an overlapping key is released.
    wire [4:0] rows [8];
    wire caps = key_down[8'h12] | key_down[8'h59] | key_down[8'h66] | key_down[8'h76] | (|cursor);
    wire symbol_shift = key_down[8'h14] | key_down[8'h11] | key_down[8'h41] | key_down[8'h49] |
                        key_down[8'h4a] | key_down[8'h4c] | key_down[8'h4e] | key_down[8'h52] | key_down[8'h55];

    assign rows[0] = {key_down[8'h2a], key_down[8'h21], key_down[8'h22], key_down[8'h1a], caps}; // V C X Z CS
    assign rows[1] = {key_down[8'h34], key_down[8'h2b], key_down[8'h23], key_down[8'h1b], key_down[8'h1c]}; // G F D S A
    assign rows[2] = {key_down[8'h2c], key_down[8'h2d], key_down[8'h24], key_down[8'h1d], key_down[8'h15]}; // T R E W Q
    assign rows[3] = {key_down[8'h2e] | cursor[0], key_down[8'h25], key_down[8'h26], key_down[8'h1e], key_down[8'h16]};
    assign rows[4] = {key_down[8'h36] | cursor[1], key_down[8'h3d] | cursor[2], key_down[8'h3e] | cursor[3],
                      key_down[8'h46], key_down[8'h45] | key_down[8'h66]};
    assign rows[5] = {key_down[8'h35], key_down[8'h3c], key_down[8'h43], key_down[8'h44] | key_down[8'h4c],
                      key_down[8'h4d] | key_down[8'h52]}; // Y U I O P
    assign rows[6] = {key_down[8'h33], key_down[8'h3b] | key_down[8'h4e], key_down[8'h42],
                      key_down[8'h4b] | key_down[8'h55], key_down[8'h5a]}; // H J K L Enter
    assign rows[7] = {key_down[8'h32], key_down[8'h31] | key_down[8'h41], key_down[8'h3a] | key_down[8'h49],
                      symbol_shift, key_down[8'h29] | key_down[8'h76]}; // B N M SS Space

    // PC slash is Spectrum SYMBOL SHIFT+V.
    always @* begin
        columns = 5'b11111;
        for (integer row = 0; row < 8; row = row + 1)
            if (!row_select[row]) columns = columns & ~rows[row];
        if (!row_select[0] && key_down[8'h4a]) columns[4] = 0;
    end
endmodule
