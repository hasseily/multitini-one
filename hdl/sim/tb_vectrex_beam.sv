// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_vectrex_beam;
    reg clk=0; always #5 clk=~clk;
    reg reset=1,pause=0,sample_tick=0,ca2=0,cb2=0;
    reg [7:0] porta=0,portb=8'h81;
    wire signed [23:0] x,y;
    wire [7:0] intensity;
    wire blank,point_valid;
    wire signed [7:0] dac_audio;
    multitini_vectrex_beam #(.ANALOG_DELAY_CLOCKS(8)) dut(.*);
    task clocks(input integer n);
        repeat(n)begin @(posedge clk);#1;end
    endtask
    task settle(input [7:0] pa,input [7:0] pb);
        @(negedge clk);porta=pa;portb=pb;clocks(12);
    endtask
    initial begin
        clocks(3);@(negedge clk);reset=0;
        clocks(8);
        if(point_valid || x!==0 || y!==0)$fatal(1,"Reset/settling interval leaked a point");
        settle(20,8'h80); // Y sample and hold, integrator held at zero.
        settle(5,8'h82);  // Reference offset.
        settle(127,8'h84);
        @(negedge clk);ca2=1;cb2=1;
        settle(15,8'h81); // DAC X=15; ramp disabled; Y=20, reference=5.
        if(x!==0 || y!==0 || intensity!==255 || blank)$fatal(1,"Sample/hold or zero failed");
        @(negedge clk);portb=8'h01;
        clocks(8);
        if(x!==0 || y!==0)$fatal(1,"Ramp changed before the eight-clock settling delay");
        clocks(4);
        if(x!==40 || y!==60)$fatal(1,"Integrator/reference gain x=%0d y=%0d",x,y);
        @(negedge clk);cb2=0;
        clocks(1);
        if(!blank || !point_valid)$fatal(1,"CB2 blanking must be immediate and emit an edge point");
        @(negedge clk);pause=1;
        clocks(20);
        if(x!==50 || y!==75 || point_valid)$fatal(1,"Pause advanced beam state");
        @(negedge clk);pause=0;ca2=0;
        clocks(8);
        if(x!==130 || y!==195)$fatal(1,"CA2 zero bypassed analogue settling");
        clocks(1);
        if(x!==0 || y!==0)$fatal(1,"CA2 did not reset both integrators");
        settle(8'hff,8'h84);
        if(intensity!==0)$fatal(1,"Negative Z must clamp to black");
        settle(8'hc9,8'h86);
        if(dac_audio!==-55)$fatal(1,"Audio mux sample did not retain signed DAC value");
        @(negedge clk);sample_tick=1;
        clocks(1);if(!point_valid)$fatal(1,"Regular sample tick missing");
        @(negedge clk);sample_tick=0;
        clocks(1);if(point_valid)$fatal(1,"Sample valid must be a pulse");
        $display("PASS: Vectrex beam delay, sample/hold, reference, gain, zero, blank, pause and audio");
        $finish;
    end
    initial begin #100000;$fatal(1,"Beam test timeout");end
endmodule
