// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
import P65C816_pkg::*;
module tb_apple_iigs_mcode;
    reg clk=0,reset_n=0,enable=0;
    reg [7:0] opcode=0;
    reg [3:0] state=0;
    MCode_r actual,expected,held;
    integer checks=0;
    always #5 clk=~clk;
    mcode port(.CLK(clk),.RST_N(reset_n),.EN(enable),.IR(opcode),.STATE(state),.M(actual));
    mcode_reference original(.CLK(clk),.RST_N(reset_n),.EN(enable),.IR(opcode),.STATE(state),.M(expected));
    task check;
        begin
            #1;
            if(actual!==expected)$fatal(1,"Microcode mismatch IR=%x STATE=%x EN=%b reset=%b actual=%x expected=%x",opcode,state,enable,reset_n,actual,expected);
            checks=checks+1;
        end
    endtask
    initial begin
        @(negedge clk);check();reset_n=1;
        for(integer op=0;op<256;op=op+1)begin
            for(integer step=1;step<=8;step=step+1)begin
                @(negedge clk);opcode=op;state=step;enable=1;
                @(posedge clk);check();held=actual;
                @(negedge clk);enable=0;opcode=~opcode;state=0;
                @(posedge clk);check();
                if(actual!==held)$fatal(1,"Microcode advanced with EN=0");
                @(negedge clk);enable=1;
                @(posedge clk);check();
            end
        end
        // Assert reset between edges after a non-default lookup.
        @(negedge clk);enable=1;opcode=8'h69;state=1;
        @(posedge clk);check();
        #2;reset_n=0;check();
        #1;reset_n=1;enable=0;
        @(posedge clk);check();
        $display("PASS: %0d microcode equivalence checks cover all2048 entries, EN hold, STATE0 and asynchronous reset",checks);
        $finish;
    end
endmodule
