// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Original cartridge program. It exercises the real 68000, Z80, VDP, PSG,
// bus arbiter and save RAM without any commercial ROM or replacement CPU.
module tb_genesis_machine;
    reg clk=0;
    always #5 clk=~clk;
    reg reset=1,pause_request=0;
    wire machine_clk,rom_stall,machine_ready;
    reg cart_download=0,cart_load_wr=0;
    reg [22:0] cart_load_addr=0;
    reg [15:0] cart_load_data=0;
    wire [23:0] rom_addr;
    wire rom_request;
    reg [15:0] rom[0:511];
    wire [15:0] rom_data=rom_addr<1024 ? rom[rom_addr[9:1]] : 16'hffff;
    reg rom_data_valid=1;
    wire [7:0] save_q,video_r,video_g,video_b;
    wire save_changed,video_ce,video_de,video_vs,video_240;
    wire signed [15:0] audio_l,audio_r;
    wire chip_run=reset || !(pause_request || rom_stall);
    wire source_ce=video_ce && chip_run && machine_ready;
    BUFGCE gate_i(.I(clk),.CE(chip_run),.O(machine_clk));
    multitini_genesis_machine dut(
        .clk(clk),.machine_clk(machine_clk),.reset(reset),.pause_request(pause_request),
        .region_jp(1'b0),.region_pal(1'b0),.six_button(2'b11),.fm_chip(1'b0),.lowpass(2'b11),
        .buttons0(12'd0),.buttons1(12'd0),.cart_download(cart_download),.cart_load_wr(cart_load_wr),
        .cart_load_addr(cart_load_addr),.cart_load_data(cart_load_data),
        .save_addr(16'd0),.save_wr(1'b0),.save_data(8'd0),.save_q(save_q),.save_changed(save_changed),
        .rom_addr(rom_addr),.rom_request(rom_request),.rom_data_valid(rom_data_valid),.rom_data(rom_data),
        .rom_stall(rom_stall),.machine_ready(machine_ready),
        .video_r(video_r),.video_g(video_g),.video_b(video_b),.video_ce(video_ce),.video_de(video_de),
        .video_vs(video_vs),.video_240(video_240),.audio_l(audio_l),.audio_r(audio_r));
    wire [11:0] normalized_x,normalized_y;
    wire [15:0] normalized_rgb;
    wire normalized_start;
    wire [31:0] dropped_fields;
    multitini_genesis_video_normalizer normalizer(
        .clk(clk),.resetn(!reset),.ready(machine_ready),.pixel_ce(source_ce),
        .de(video_de),.vsync(video_vs),.red(video_r),.green(video_g),.blue(video_b),
        .mode_240(video_240),.scan_x(normalized_x),.scan_y(normalized_y),
        .rgb565(normalized_rgb),.frame_start(normalized_start),.dropped_fields(dropped_fields));
    integer cursor=256;
    task automatic emit(input [15:0] word_value);
        rom[cursor]=word_value;cursor=cursor+1;
    endtask
    task automatic movew(input [15:0] value,input [31:0] address);
        emit(16'h33fc);emit(value);emit(address[31:16]);emit(address[15:0]);
    endtask
    task automatic moveb(input [7:0] value,input [31:0] address);
        emit(16'h13fc);emit({8'd0,value});emit(address[31:16]);emit(address[15:0]);
    endtask
    integer reads=0,red_pixels=0,audio_nonzero=0,frames=0;
    integer raw_width=0,raw_rows=0,raw_complete=0;
    integer normalized_count=0,normalized_complete=0,normalized_red=0;
    integer vdp_clear_pulses=0;
    always @(posedge machine_clk)
        if(!reset && dut.board_i.ym.vdp.bus_pair.out12)
            vdp_clear_pulses<=vdp_clear_pulses+1;
    reg raw_started=0,raw_de_previous=0,normalized_started=0;
    reg vs_previous=0;
    always @(posedge clk) if(!reset) begin
        if(rom_request && rom_data_valid) reads<=reads+1;
        if(source_ce && video_de && video_r>64 && video_g<16 && video_b<16) red_pixels<=red_pixels+1;
        if(audio_l!=0 || audio_r!=0) audio_nonzero<=audio_nonzero+1;
        vs_previous<=video_vs;
        if(video_vs && !vs_previous) begin
            frames<=frames+1;
            if(raw_started) begin
                if(raw_rows!=224)$fatal(1,"Real VDP field has %0d active rows, expected 224",raw_rows);
                raw_complete=raw_complete+1;
            end
            raw_started=1;raw_rows=0;raw_width=0;raw_de_previous=0;
        end
        if(source_ce && raw_started) begin
            if(video_de) raw_width=raw_width+1;
            if(!video_de && raw_de_previous) begin
                if(raw_width!=320)$fatal(1,"Real H40 VDP line %0d has %0d pixels, expected 320",raw_rows,raw_width);
                raw_rows=raw_rows+1;raw_width=0;
            end
            raw_de_previous=video_de;
        end
        if(normalized_start) begin
            if(normalized_started && normalized_count!=320*240)
                $fatal(1,"Real VDP normalized field ended after %0d pixels",normalized_count);
            normalized_started=1;normalized_count=0;
        end
        if(normalized_x!=12'hfff) begin
            if(!normalized_started || normalized_x!=normalized_count%320 || normalized_y!=normalized_count/320)
                $fatal(1,"Real VDP normalized coordinate %0d: %0d,%0d",normalized_count,normalized_x,normalized_y);
            if((normalized_y<8 || normalized_y>=232) && normalized_rgb!=0)
                $fatal(1,"V224 padding is not black at %0d,%0d",normalized_x,normalized_y);
            if(normalized_rgb[15:11]>8 && normalized_rgb[10:5]<4 && normalized_rgb[4:0]<2)
                normalized_red=normalized_red+1;
            normalized_count=normalized_count+1;
            if(normalized_count==320*240) normalized_complete=normalized_complete+1;
        end
    end
    integer i;
    initial begin
        for(i=0;i<512;i=i+1)rom[i]=16'hffff;
        rom[0]=16'h00ff;rom[1]=16'hff00; // initial supervisor stack
        rom[2]=16'h0000;rom[3]=16'h0200; // reset vector
        rom[128]=16'h5345;rom[129]=16'h4741;
        emit(16'h46fc);emit(16'h2700); // interrupts off
        movew(16'ha55a,32'h00ff0010);
        movew(16'h00d5,32'h00200000);
        // Take the sound bus, load an original Z80 program, then release it.
        movew(16'h0100,32'h00a11100);
        movew(16'h0100,32'h00a11200);
        for(i=0;i<32;i=i+1)emit(16'h4e71);
        moveb(8'h3e,32'h00a00000);moveb(8'h7b,32'h00a00001);
        moveb(8'h32,32'h00a00002);moveb(8'h00,32'h00a00003);
        moveb(8'h01,32'h00a00004);moveb(8'h18,32'h00a00005);
        moveb(8'hfe,32'h00a00006);
        movew(16'h0000,32'h00a11200);
        movew(16'h0000,32'h00a11100);
        movew(16'h0100,32'h00a11200);
        // Mode 5, H40, display enabled; backdrop palette entry zero is red.
        movew(16'h8004,32'h00c00004);movew(16'h8144,32'h00c00004);
        movew(16'h8c81,32'h00c00004);movew(16'h8f02,32'h00c00004);
        emit(16'h23fc);emit(16'hc000);emit(16'h0000);emit(16'h00c0);emit(16'h0004);
        movew(16'h000e,32'h00c00000);
        moveb(8'h84,32'h00c00011);moveb(8'h10,32'h00c00011);moveb(8'h90,32'h00c00011);
        emit(16'h60fe);
        repeat(8)@(negedge clk);
        cart_download=1;
        @(negedge clk);
        for(i=0;i<512;i=i+1) begin
            cart_load_addr=i;cart_load_data=rom[i];cart_load_wr=1;
            @(negedge clk);
        end
        cart_load_wr=0;cart_download=0;
        repeat(70000)@(negedge clk);
        reset=0;
        // Exercise a physical clock hold during a live ROM fetch.
        wait(rom_request);
        @(negedge clk);rom_data_valid=0;
        repeat(29)@(negedge clk);
        rom_data_valid=1;
        // Pause a live active-video pixel too. A held chip CE must not create
        // duplicate samples in the continuously clocked normalizer.
        wait(video_de && video_ce && rom_request);
        @(negedge clk);rom_data_valid=0;
        repeat(37)@(negedge clk);
        rom_data_valid=1;
        repeat(5000000)@(negedge clk);
        // IA14 interleaves the external 68K RAM rows: $FF0010 is word $2008.
        if(dut.ram68k_i.memory[15'h2008]!==16'ha55a)$fatal(1,"68000 original program did not execute: %h addr %h",dut.ram68k_i.memory[15'h2008],rom_addr);
        if(save_q!==8'hd5)$fatal(1,"68000 cartridge SRAM write: %h",save_q);
        if(dut.ramz80_i.memory[256]!==8'h7b)$fatal(1,"Z80 original program did not execute: %h program %h %h %h %h %h %h %h ZRES %b ZBR %b ZBAK %b ZA %h",dut.ramz80_i.memory[256],dut.ramz80_i.memory[0],dut.ramz80_i.memory[1],dut.ramz80_i.memory[2],dut.ramz80_i.memory[3],dut.ramz80_i.memory[4],dut.ramz80_i.memory[5],dut.ramz80_i.memory[6],dut.board_i.ZRES,dut.board_i.ZBR,dut.board_i.ZBAK,dut.board_i.ZA);
        if(red_pixels<1000 || frames<2)$fatal(1,"VDP output missing: red %0d frames %0d",red_pixels,frames);
        if(raw_complete<1 || normalized_complete<1 || normalized_red<1000 || dropped_fields!=0)
            $fatal(1,"Real VDP normalization failed: raw %0d normalized %0d red %0d dropped %0d",raw_complete,normalized_complete,normalized_red,dropped_fields);
        if(audio_nonzero<1000)$fatal(1,"PSG output missing");
        if(reads<1000 || !machine_ready)$fatal(1,"machine did not run");
        $display("PASS real Nuked 68000/Z80 boot, SRAM, VDP, PSG, active-video ROM wait: %0d red pixels, %0d complete 320x240 normalized fields",red_pixels,normalized_complete);
        $display("PASS VDP self-clear portability monitor: %0d sampled clear-dominant pulses",vdp_clear_pulses);
        $finish;
    end
    initial begin #120000000; $fatal(1,"Real Genesis machine fixture timed out"); end
endmodule
