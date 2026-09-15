// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_border_config;
    reg control_clk=0,pixel_clk=0,control_resetn=0,pixel_resetn=0,vblank=0;
    reg [31:0] requested=32'h670;
    wire [12:0] config_q;
    reg [12:0] previous=13'h670;
    always #7 control_clk=~control_clk;
    always #3 pixel_clk=~pixel_clk;
    multitini_border_config dut(
        .control_clk(control_clk),.control_resetn(control_resetn),
        .requested_config(requested),
        .pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.vblank_start(vblank),
        .frame_config(config_q)
    );
    always @(posedge pixel_clk) begin
        #1;
        if(pixel_resetn && !vblank && config_q!==previous)
            $fatal(1,"Settings changed outside vblank");
        previous=config_q;
    end
    task automatic apply_check(input [12:0] expected);
        repeat(150) @(negedge pixel_clk);
        vblank=1;
        @(negedge pixel_clk);vblank=0;
        if(config_q!==expected)
            $fatal(1,"Settings got %h expected %h",config_q,expected);
    endtask
    initial begin
        repeat(5) @(negedge control_clk);
        control_resetn=1;pixel_resetn=1;
        apply_check(13'h670);
        requested=32'h1499;
        apply_check(13'h1499);
        // Rapid writes while mailbox is occupied retain a coherent latest word.
        for(integer i=1;i<20;i=i+1) begin
            @(negedge control_clk);requested=32'h101+i*16;
        end
        apply_check(13'h231);
        // Reset only the destination after a completed transaction.
        pixel_resetn=0;repeat(10) @(negedge control_clk);pixel_resetn=1;
        apply_check(13'h231);
        // Source reset must resend even when the new value equals its default.
        control_resetn=0;requested=32'h670;
        repeat(10) @(negedge control_clk);control_resetn=1;
        apply_check(13'h670);
        // Reset either side while a new payload is in flight.
        requested=32'h671;
        repeat(2) @(negedge control_clk);pixel_resetn=0;
        repeat(10) @(negedge control_clk);pixel_resetn=1;
        apply_check(13'h671);
        requested=32'h1419;
        repeat(2) @(negedge control_clk);control_resetn=0;
        requested=32'h670;
        repeat(10) @(negedge control_clk);control_resetn=1;
        apply_check(13'h670);
        $display("PASS coherent border settings, latest write, vblank, independent/in-flight resets");
        $finish;
    end
endmodule
