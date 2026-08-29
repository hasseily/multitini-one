// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_c64_input;
    import multitini_c64_defs_pkg::*;

    logic clk = 1'b0;
    logic reset = 1'b1;
    logic [15:0] input_code = 16'd0;
    logic [15:0] input_value = 16'd0;
    logic input_valid = 1'b0;
    logic input_ready;
    logic [10:0] ps2_key;
    logic [6:0] joy_a;
    logic [6:0] joy_b;

    always #5 clk = !clk;

    multitini_c64_input dut (.*);

    task automatic check_value(input logic condition, input string message);
        if (!condition) begin
            $display("FAIL: %s", message);
            $fatal(1);
        end
    endtask

    task automatic send_event(input logic [15:0] code,
                              input logic [15:0] value);
        @(negedge clk);
        input_code = code;
        input_value = value;
        input_valid = 1'b1;
        @(negedge clk);
        input_valid = 1'b0;
        #1;
    endtask

    initial begin
        repeat (3) @(negedge clk);
        reset = 1'b0;
        @(negedge clk);
        check_value(input_ready, "input ready");

        send_event({MT_INPUT_PS2_SET2, 8'h1C}, 16'h0001);
        check_value(ps2_key[10:0] == {1'b1, 1'b1, 1'b0, 8'h1C},
                    "PS/2 A press encoded");
        send_event({MT_INPUT_PS2_SET2, 8'h1C}, 16'h0000);
        check_value(ps2_key[10:0] == {1'b0, 1'b0, 1'b0, 8'h1C},
                    "PS/2 A release encoded");
        send_event({MT_INPUT_PS2_SET2, 8'h75}, 16'h0003);
        check_value(ps2_key[10:0] == {1'b1, 1'b1, 1'b1, 8'h75},
                    "extended PS/2 press encoded");

        send_event({MT_INPUT_JOYSTICK1, 8'd0}, 16'h0001);
        send_event({MT_INPUT_JOYSTICK1, 8'd4}, 16'h0001);
        send_event({MT_INPUT_JOYSTICK2, 8'd3}, 16'h0001);
        check_value(joy_a == 7'b0010001, "joystick one press state");
        check_value(joy_b == 7'b0001000, "joystick two press state");
        send_event({MT_INPUT_JOYSTICK1, 8'd0}, 16'h0000);
        check_value(joy_a == 7'b0010000, "joystick release state");

        send_event({8'h7F, 8'h00}, 16'hFFFF);
        check_value(joy_a == 7'b0010000 && joy_b == 7'b0001000,
                    "unknown namespace ignored");

        $display("PASS: C64 input bridge");
        $finish;
    end
endmodule
