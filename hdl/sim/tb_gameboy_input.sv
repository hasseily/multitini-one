// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_gameboy_input;
    import multitini_gameboy_defs_pkg::*;

    logic clk = 1'b0;
    logic reset = 1'b1;
    logic [15:0] input_code = 16'd0;
    logic [15:0] input_value = 16'd0;
    logic input_valid = 1'b0;
    logic input_ready;
    logic p14 = 1'b1;
    logic p15 = 1'b1;
    logic [3:0] joy_din;
    logic [7:0] buttons;

    always #5 clk = !clk;
    multitini_gameboy_input dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic send_event(input logic [15:0] code, input logic value);
        @(negedge clk);
        input_code = code;
        input_value = {15'd0, value};
        input_valid = 1'b1;
        @(negedge clk);
        input_valid = 1'b0;
    endtask

    initial begin
        repeat (3) @(negedge clk);
        reset = 1'b0;
        send_event({MT_INPUT_GB_GAMEPAD, 8'(MT_GB_BUTTON_RIGHT)}, 1'b1);
        send_event({MT_INPUT_GB_GAMEPAD, 8'(MT_GB_BUTTON_A)}, 1'b1);
        check_value(buttons == 8'b00010001, "button state tracks presses");

        p14 = 1'b0;
        p15 = 1'b1;
        #1;
        check_value(joy_din == 4'b1110, "direction row is active low");
        p14 = 1'b1;
        p15 = 1'b0;
        #1;
        check_value(joy_din == 4'b1110, "button row is active low");
        p14 = 1'b0;
        #1;
        check_value(joy_din == 4'b1110, "selected rows are wired-AND");

        send_event({8'hff, 8'h00}, 1'b1);
        check_value(buttons == 8'b00010001, "unknown namespace ignored");
        send_event({MT_INPUT_GB_GAMEPAD, 8'(MT_GB_BUTTON_RIGHT)}, 1'b0);
        check_value(buttons == 8'b00010000, "release clears button");

        $display("PASS: Game Boy input bridge");
        $finish;
    end
endmodule
