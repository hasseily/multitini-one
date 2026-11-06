// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

module multitini_atari_xt_keyboard (
    input  wire        clk, reset,
    input  wire [15:0] input_code, input_value,
    input  wire        input_valid,
    output reg         key_make, key_release, key_break,
    output reg  [7:0]  key_code,
    output wire [7:0]  consol,
    output wire [7:0]  porta,
    output wire [1:0]  trigger_n
);
    reg [2:0] console_down;
    reg [4:0] joystick1, joystick2;
    reg [1:0] shift_down, control_down;
    reg [8:0] active_key;
    reg key_down;

    wire [8:0] key = {input_value[1], input_code[7:0]};
    wire pressed = input_value[0];
    wire shift = |shift_down;
    wire control = |control_down;

    assign consol = {5'b00000, ~console_down};
    assign porta = {~joystick2[3:0], ~joystick1[3:0]};
    assign trigger_n = {~joystick2[4], ~joystick1[4]};

    // POKEY matrix codes are machine-visible values. Bits 6 and 7 carry
    // shift and control. The shifted digit positions follow the Atari layout.
    function automatic [8:0] translate(input [8:0] scan, input bit sh, ct);
        reg [7:0] modifiers;
        begin
            modifiers = {ct, sh, 6'b0};
            translate = 9'h100;
            case (scan)
                9'h01c: translate = {1'b0, modifiers | 8'h3f}; // A
                9'h032: translate = {1'b0, modifiers | 8'h15}; // B
                9'h021: translate = {1'b0, modifiers | 8'h12}; // C
                9'h023: translate = {1'b0, modifiers | 8'h3a}; // D
                9'h024: translate = {1'b0, modifiers | 8'h2a}; // E
                9'h02b: translate = {1'b0, modifiers | 8'h38}; // F
                9'h034: translate = {1'b0, modifiers | 8'h3d}; // G
                9'h033: translate = {1'b0, modifiers | 8'h39}; // H
                9'h043: translate = {1'b0, modifiers | 8'h0d}; // I
                9'h03b: translate = {1'b0, modifiers | 8'h01}; // J
                9'h042: translate = {1'b0, modifiers | 8'h05}; // K
                9'h04b: translate = {1'b0, modifiers | 8'h00}; // L
                9'h03a: translate = {1'b0, modifiers | 8'h25}; // M
                9'h031: translate = {1'b0, modifiers | 8'h23}; // N
                9'h044: translate = {1'b0, modifiers | 8'h08}; // O
                9'h04d: translate = {1'b0, modifiers | 8'h0a}; // P
                9'h015: translate = {1'b0, modifiers | 8'h2f}; // Q
                9'h02d: translate = {1'b0, modifiers | 8'h28}; // R
                9'h01b: translate = {1'b0, modifiers | 8'h3e}; // S
                9'h02c: translate = {1'b0, modifiers | 8'h2d}; // T
                9'h03c: translate = {1'b0, modifiers | 8'h0b}; // U
                9'h02a: translate = {1'b0, modifiers | 8'h10}; // V
                9'h01d: translate = {1'b0, modifiers | 8'h2e}; // W
                9'h022: translate = {1'b0, modifiers | 8'h16}; // X
                9'h035: translate = {1'b0, modifiers | 8'h2b}; // Y
                9'h01a: translate = {1'b0, modifiers | 8'h17}; // Z
                9'h016: translate = {1'b0, modifiers | 8'h1f}; // 1 !
                9'h01e: translate = sh && !ct ? 9'h075 : {1'b0, modifiers | 8'h1e}; // 2 @
                9'h026: translate = {1'b0, modifiers | 8'h1a}; // 3 #
                9'h025: translate = {1'b0, modifiers | 8'h18}; // 4 $
                9'h02e: translate = {1'b0, modifiers | 8'h1d}; // 5 %
                9'h036: translate = sh && !ct ? 9'h047 : {1'b0, modifiers | 8'h1b}; // 6 ^
                9'h03d: translate = sh && !ct ? 9'h05b : {1'b0, modifiers | 8'h33}; // 7 &
                9'h03e: translate = sh && !ct ? 9'h007 : {1'b0, modifiers | 8'h35}; // 8 *
                9'h046: translate = sh && !ct ? 9'h070 : {1'b0, modifiers | 8'h30}; // 9 (
                9'h045: translate = {1'b0, modifiers | 8'h32}; // 0 )
                9'h05a, 9'h15a: translate = {1'b0, modifiers | 8'h0c}; // Return
                9'h076: translate = {1'b0, modifiers | 8'h1c}; // Escape
                9'h066: translate = {1'b0, modifiers | 8'h34}; // Backspace
                9'h00d: translate = {1'b0, modifiers | 8'h2c}; // Tab
                9'h029: translate = {1'b0, modifiers | 8'h21}; // Space
                9'h04e: translate = {1'b0, modifiers | 8'h0e}; // - _
                9'h055: translate = sh && !ct ? 9'h006 : {1'b0, modifiers | 8'h0f}; // = +
                9'h054: translate = 9'h060; // [
                9'h05b: translate = 9'h062; // ]
                9'h05d: translate = sh ? 9'h04f : 9'h046; // Backslash or vertical bar
                9'h04c: translate = {1'b0, modifiers | 8'h02}; // ; :
                9'h052: translate = sh ? 9'h05e : 9'h073; // ' "
                9'h041: translate = sh ? 9'h036 : {1'b0, modifiers | 8'h20}; // , <
                9'h049: translate = sh ? 9'h037 : {1'b0, modifiers | 8'h22}; // . >
                9'h04a, 9'h14a: translate = {1'b0, modifiers | 8'h26}; // / ?
                9'h058: translate = 9'h03c; // Caps toggle
                9'h011, 9'h111: translate = 9'h027; // Atari/inverse video
                9'h00c: translate = {1'b0, modifiers | 8'h11}; // F4 Help
                9'h16b: translate = 9'h086; // Left
                9'h174: translate = 9'h087; // Right
                9'h175: translate = 9'h08e; // Up
                9'h172: translate = 9'h08f; // Down
                9'h16c: translate = 9'h076; // Home / Clear
                9'h170: translate = sh ? 9'h077 : 9'h0b7; // Insert
                9'h171: translate = sh ? 9'h074 : 9'h0b4; // Delete
                default: ;
            endcase
        end
    endfunction

    wire [8:0] translated = translate(key, shift, control);
    always @(posedge clk) begin
        key_make <= 0;
        key_release <= 0;
        key_break <= 0;
        if (reset) begin
            console_down <= 0;
            joystick1 <= 0;
            joystick2 <= 0;
            shift_down <= 0;
            control_down <= 0;
            active_key <= 0;
            key_down <= 0;
            key_code <= 0;
        end else if (input_valid) begin
            case (input_code[15:8])
                8'h00: case (key)
                    9'h012: shift_down[0] <= pressed;
                    9'h059: shift_down[1] <= pressed;
                    9'h014: control_down[0] <= pressed;
                    9'h114: control_down[1] <= pressed;
                    9'h005: console_down[0] <= pressed; // F1 Start
                    9'h006: console_down[1] <= pressed; // F2 Select
                    9'h004: console_down[2] <= pressed; // F3 Option
                    9'h003: key_break <= pressed;     // F5 Break
                    default: begin
                        if (pressed && !translated[8] && (!key_down || key != active_key)) begin
                            key_code <= translated[7:0];
                            active_key <= key;
                            key_down <= 1;
                            key_make <= 1;
                        end else if (!pressed && key_down && key == active_key) begin
                            key_down <= 0;
                            key_release <= 1;
                        end
                    end
                endcase
                8'h01: if (input_code[7:0] < 5) joystick1[input_code[2:0]] <= pressed;
                8'h02: if (input_code[7:0] < 5) joystick2[input_code[2:0]] <= pressed;
                default: ;
            endcase
        end
    end
endmodule
