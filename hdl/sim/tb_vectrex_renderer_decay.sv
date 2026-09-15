// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_vectrex_renderer_decay;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,pause=0,line_valid=0;
    wire line_ready,initialized;
    wire [31:0] drawn_lines,overflow_count;
    wire [7:0] pixel_intensity;
    multitini_vectrex_renderer #(.WIDTH(4),.HEIGHT(4),.CLOCK_HZ(9600),
        .DECAY_HZ(60),.QUEUE_DEPTH(4)) dut(
        .clk(clk),.reset(reset),.pause(pause),.line_valid(line_valid),
        .line_x0(16'sd0),.line_y0(16'sd0),.line_x1(16'sd0),.line_y1(16'sd0),
        .line_intensity(8'd255),.line_ready(line_ready),.initialized(initialized),
        .drawn_lines(drawn_lines),.overflow_count(overflow_count),
        .pixel_clk(clk),.pixel_address(4'd0),.pixel_intensity(pixel_intensity));
    task check_fade(input [7:0] expected);
        wait(dut.state==5 && dut.fade_address==0);
        repeat(2)@(posedge clk);#1;
        if(pixel_intensity!==expected)$fatal(1,"Phosphor sweep expected %0d got %0d",expected,pixel_intensity);
    endtask
    initial begin
        repeat(3)@(posedge clk);@(negedge clk);reset=0;
        wait(initialized);
        @(negedge clk);line_valid=1;
        @(negedge clk);line_valid=0;
        check_fade(192);
        @(negedge clk);pause=1;
        repeat(1000)@(posedge clk);#1;
        if(pixel_intensity!==192)$fatal(1,"Pause did not freeze phosphor time");
        @(negedge clk);pause=0;
        check_fade(144);check_fade(108);check_fade(81);
        if(drawn_lines!==1 || overflow_count!==0)$fatal(1,"Unexpected line/overflow count");
        $display("PASS: Vectrex phosphor decays by a quarter per sweep and freezes on pause");
        $finish;
    end
    initial begin #100000;$fatal(1,"Phosphor test timeout");end
endmodule
