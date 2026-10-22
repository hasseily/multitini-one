// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_expansion_config;
    import multitini_regs_pkg::*;
    logic control_clk=0,control_resetn=0,core_clk=0,core_resetn=0;
    logic[31:0] requested_config=32'h0000000b;
    wire[3:0] config_value;
    integer checks=0,received=0,transmitted=0;
    bit monitor=0;
    logic[3:0] pending[$];
    always #3.5 control_clk=~control_clk;
    always #5.5 core_clk=~core_clk;
    multitini_c64_expansion_config dut(.*);
    task automatic check(input logic okay,input string why);
        checks++;if(okay !==1'b1)$fatal(1,"expansion CDC: %s",why);
    endtask
    always @(posedge control_clk) if(monitor && control_resetn && dut.changed && dut.ready) begin
        pending.push_back(requested_config[3:0]);transmitted++;
    end
    always @(posedge core_clk) if(monitor && core_resetn && dut.valid)begin : receive
        logic[3:0] expected;
        check(pending.size()>0,"unsolicited destination word");
        expected=pending.pop_front();
        check(dut.destination===expected,"coherent word equals accepted source payload");
        received++;
    end
    task automatic settle(input logic[3:0] expected);
        integer n;
        n=0;while((config_value!==expected || !dut.ready || dut.changed) && n<300)begin @(negedge control_clk);n++;end
        check(n<300 && config_value===expected,"latest configuration eventually delivered");
        repeat(20)@(negedge control_clk);
        check(config_value===expected,"configuration remains stable when idle");
    endtask
    initial begin
        // Let the vendor simulation's 100 ns configuration GSR finish.
        repeat(32)@(negedge control_clk);check(config_value===4'hb,"reset default");
        control_resetn=1;core_resetn=1;monitor=1;settle(4'hb);
        @(negedge control_clk);requested_config=32'hffff0002;
        wait(!dut.ready);
        repeat(12)begin @(negedge control_clk);requested_config=(requested_config^32'h00000009);end
        requested_config=32'h1234000a;settle(4'ha);
        check(transmitted==received && pending.size()==0,"all accepted words consumed once");
        // Destination reset during an in-flight word must recover and resend
        // the latest request. Stale reset-time payloads are not scored here.
        @(negedge control_clk);requested_config=3;wait(!dut.ready);
        monitor=0;pending.delete();core_resetn=0;
        repeat(16)@(negedge core_clk);check(config_value===4'hb,"destination-only reset default");
        requested_config=8;core_resetn=1;settle(8);
        // A control-side reset must also allow a fresh request after the
        // handshake drains, without requiring a destination reset.
        @(negedge control_clk);requested_config=2;wait(!dut.ready);control_resetn=0;
        repeat(12)@(negedge control_clk);requested_config=11;control_resetn=1;settle(11);
        core_resetn=0;control_resetn=0;repeat(12)@(negedge control_clk);
        requested_config=2;core_resetn=1;control_resetn=1;settle(2);
        $display("PASS: C64 expansion config checks=%0d coherent_transfers=%0d reset_cases=3",checks,received);$finish;
    end
    initial begin #100000;$fatal(1,"expansion CDC timeout");end
endmodule
