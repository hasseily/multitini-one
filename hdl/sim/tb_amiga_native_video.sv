// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Actual PAL ECS sync/counters drive the production normalizer. The CPU stays
// reset and no private ROM is required. This proves canvas timing/completion,
// not the displayed contents or physical DVI behavior.
module tb_amiga_native_video;
    reg clk=0;always #17.62 clk=~clk;
    reg reset=1,ready=0;
    wire chip_clk,chip_tick,hsync_n,vsync_n,hblank,vblank,ntsc,field_short;
    wire [23:0] rgb;
    multitini_amiga_machine dut(
        .clk_service28(clk),.reset(reset),.guest_reset(1'b1),.run_enable(1'b1),
        .clk_chip(chip_clk),.chip_tick(chip_tick),
        .chip_rdata(16'd0),.chip_rdata2(16'd0),.chip_rdata48(48'd0),
        .cpu_rdata(16'd0),.cpu_rdata2(16'd0),.cpu_ready(1'b0),.cpu_ready_long(1'b0),
        .io_uio(1'b0),.io_fpga(1'b0),.io_strobe(1'b0),.io_din(16'd0),
        .ide_address(5'd0),.ide_read(1'b0),.ide_write(1'b0),.ide_wdata(16'd0),
        .joy1(16'hffff),.joy2(16'hffff),.mouse_buttons(3'd0),.kms_level(1'b0),
        .kbd_mouse_type(2'd0),.kbd_mouse_data(8'd0),.rtc(65'd0),
        .field(field_short),.rgb(rgb),.hsync_n(hsync_n),.vsync_n(vsync_n),.hblank(hblank),.vblank(vblank),.ntsc(ntsc));
    reg consumed_tick=0;
    always @(posedge clk)consumed_tick<=!reset && chip_tick;
    wire [11:0] x,y;
    wire [15:0] pixel;
    wire frame_start,valid;
    wire [31:0] malformed;
    multitini_amiga_video_normalizer normalizer(
        .clk(clk),.reset(reset),.ready(ready),.chip_tick(consumed_tick),
        .capture_mode(2'd0),.native_lace(1'b0),.diw_hstart(9'd129),.diw_vstart(11'd44),
        .hsync_n(hsync_n),.vsync_n(vsync_n),.ntsc(ntsc),.field_short(field_short),.hblank(hblank),.vblank(vblank),.rgb(rgb),
        .scan_x(x),.scan_y(y),.rgb565(pixel),.frame_start(frame_start),.valid(valid),.malformed_frames(malformed));
    integer pixels=0,frames=0,starts=0;
    reg [31:0] startup_malformed=0;
    always @(negedge clk)if(ready)begin
        if(valid)begin
            if(x!==pixels%640 || y!==pixels/640)
                $fatal(1,"Native PAL canvas discontinuity: expected %d,%d got %d,%d",pixels%640,pixels/640,x,y);
            pixels=pixels+1;
            if(pixels==640*512)begin
                frames=frames+1;
                if(frames==2)begin
                    if(malformed!==startup_malformed || ntsc!==0)$fatal(1,"Native PAL raster malformed after sync warmup: %d",malformed-startup_malformed);
                    $display("PASS native PAL ECS timing: two complete 640x512 canvases, zero steady-state malformed fields, startup overruns=%0d",startup_malformed);
                    $finish;
                end
            end
        end
        if(frame_start)begin
            if(starts==0)startup_malformed=malformed;
            if(pixels!=0 && pixels!=640*512)$fatal(1,"Native VSYNC truncated output at %d pixels",pixels);
            pixels=0;starts=starts+1;
        end
    end
    initial begin
        repeat(8)@(negedge clk);reset=0;
        repeat(128)@(negedge chip_clk);ready=1;
        #100000000;
        $fatal(1,"Native PAL video timeout: starts=%d frames=%d pixels=%d malformed=%d",starts,frames,pixels,malformed);
    end
    initial begin
        // Model FPGA configuration INIT=0 on the 19 otherwise unreset beam
        // counter FFs. This has been checked in a synthesized 7-series DCP.
        // Subsequent counter, sync and normalization behavior is untouched.
        dut.chipset.AGNUS1.bc1.hpos[8:1]=8'd0;
        dut.chipset.AGNUS1.bc1.vpos=11'd0;
    end
endmodule
