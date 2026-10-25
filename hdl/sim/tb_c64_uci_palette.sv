// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_uci_palette;
    reg host_clk=0,pixel_clk=0,source_clk=0;
    reg resetn=0,pixel_resetn=0,valid=0,clock_running=1;
    reg [28:0] data=0;
    wire ready;
    reg [3:0] index=0;
    wire [15:0] rgb;
    always #5 host_clk=~host_clk;
    always #7 source_clk=~source_clk;
    always #3 if(clock_running) pixel_clk=~pixel_clk;
    multitini_c64_video_bridge #(.PROGRAMMABLE_PALETTE(1),
        .SOURCE_WIDTH(8),.SOURCE_HEIGHT(4)) dut(
        .source_clk(source_clk),.source_resetn(resetn),.source_ready(1'b0),
        .source_r(8'd0),.source_g(8'd0),.source_b(8'd0),
        .source_de(1'b0),.source_vsync(1'b0),.source_valid(1'b0),
        .pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),
        .palette_clk(host_clk),.palette_resetn(resetn),
        .palette_valid(valid),.palette_data(data),.palette_ready(ready),
        .diagnostic_request(1'b0),.scan_x(12'd0),.scan_y(12'd0),
        .vblank_start(1'b0),.machine_rgb565(rgb));
    task automatic submit(input [28:0] word);
        wait(ready);@(negedge host_clk);data=word;valid=1;
        @(negedge host_clk);valid=0;
        wait(!ready);wait(ready);#1;
    endtask
    task automatic check_color(input [3:0] i,input [15:0] expected);
        index=i;#1;
        if(rgb!==expected)$fatal(1,"palette[%0d]=%04x expected%04x",i,rgb,expected);
    endtask
    initial begin
        force dut.machine_active=1;
        force dut.display_valid=1;
        force dut.output_active=1;
        force dut.selected_palette_index=index;
        #1;
        if(dut.g_palette.palette_resetn_pixel!==0)$fatal(1,"palette reset did not initialize low");
        #199;resetn=1;pixel_resetn=1;
        repeat(100)@(posedge host_clk);
        for(integer i=0;i<16;i++)check_color(i,dut.palette_rgb565(i));
        submit(29'h0fff0080);check_color(15,16'hf810);
        submit(29'h03000000);check_color(3,0);
        // Unchanged capture encoding: duplicate output colors do not make
        // an indexed VIC pixel disappear from the capture frame.
        if(dut.encode_palette(8'h75,8'hce,8'hc8)!==5'h13)$fatal(1,"capture mapping changed");
        pixel_resetn=0;repeat(30)@(posedge host_clk);pixel_resetn=1;
        check_color(15,16'hf810);
        // A stopped video clock cannot acknowledge an unapplied update.
        @(negedge pixel_clk);clock_running=0;
        wait(ready);@(negedge host_clk);data=29'h01123456;valid=1;
        @(negedge host_clk);valid=0;
        repeat(50)@(posedge host_clk);
        if(ready)$fatal(1,"palette acknowledged while pixel clock stopped");
        clock_running=1;wait(ready);check_color(1,16'h11aa);
        submit(29'h10000000);
        for(integer i=0;i<16;i++)check_color(i,dut.palette_rgb565(i));
        // A control-domain reset is sampled only when the video clock runs;
        // after it resumes, startup must restore the default palette. The
        // display-mode-only reset above must still preserve custom colors.
        submit(29'h0fff0080);check_color(15,16'hf810);
        @(negedge pixel_clk);clock_running=0;resetn=0;
        repeat(10)@(posedge host_clk);
        if(dut.g_palette.palette_resetn_pixel!==1)$fatal(1,"stopped palette reset clock advanced");
        clock_running=1;repeat(3)@(posedge pixel_clk);#1;
        if(dut.g_palette.palette_resetn_pixel!==0)$fatal(1,"palette reset failed to cross domains");
        resetn=1;repeat(100)@(posedge host_clk);
        for(integer i=0;i<16;i++)check_color(i,dut.palette_rgb565(i));
        $display("PASS C64 UCI palette: 16 colors, RGB565, duplicate colors, reset, stopped clock, reset CDC");
        $finish;
    end
    initial begin #100000;$fatal(1,"palette test timeout");end
endmodule
