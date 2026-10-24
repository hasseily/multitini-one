// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_turbo_vic_fast;
    reg clk=0,reset=1;always #5 clk=!clk;
    integer phase=0,ntsc,checks=0,pixels=0,hold_checks=0,wraps=0,last_y=0;
    reg [7:0] config_bytes[2];
    always @(negedge clk) phase=(phase+1)%32;
    wire phi=phase>=16,ena_pixel=phase%4==3,ena_data=phase==15||phase==31;
    reg cs=0,we=0;reg [5:0] address=0;reg [7:0] data=0;
    reg fast_wr=0,clr_m2m=0,clr_m2d=0;
    reg [5:0] fast_wa=0,fast_ra=0;reg [7:0] fast_wd=0;
    wire [7:0] fast_data;
    wire hold_d011,hold_spr,native_busy,irq_n;
    wire [8:0] raster;wire [9:0] raster_x;
    wire [511:0] observation;
    video_vicii_656x #(.registeredAddress(1),.emulateRefresh(1),.emulateLightpen(1),.emulateGraphics(1),.FAST_PORT(1)) dut(
        .clk(clk),.reset(reset),.phi(phi),.enaData(ena_data),.enaPixel(ena_pixel),.baSync(1'b0),.ba(),.ba_dma(),.ba_reu(),
        .mode6569(!ntsc),.mode6567old(1'b0),.mode6567R8(ntsc!=0),.mode6572(1'b0),.turbo_en(1'b0),.turbo_state(),.variant(2'b00),.char_rom_bank(1'b1),
        .read_early_active(1'b0),.read_early(1'b0),.cs(cs),.we(we),.lp_n(1'b0),.aRegisters(address),.diRegisters(data),.di(8'hff),.diColor(4'hf),.\do (),
        .vicAddr(),.irq_n(irq_n),.hSync(),.vSync(),.colorIndex(),.debugX(raster_x),.debugY(raster),.vicRefresh(),.addrValid(),.observe_state(observation),
        .fast_phase(5'(phase)),.fast_rd_addr(fast_ra),.fast_rd_data(fast_data),.fast_wr(fast_wr),.fast_wa(fast_wa),.fast_wd(fast_wd),
        .fast_clr_m2m(clr_m2m),.fast_clr_m2d(clr_m2d),.fast_hold_d011(hold_d011),.fast_hold_spr(hold_spr),.fast_native_busy(native_busy));
    task write_fast(input [5:0] a,input [7:0] d);
        @(negedge clk);#1;fast_wa=a;fast_wd=d;fast_wr=1;
        @(negedge clk);#1;fast_wr=0;checks=checks+1;
    endtask
    task expect_read(input [5:0] a,input [7:0] d);
        fast_ra=a;#1;
        if(fast_data !== d)$fatal(1,"fast read offset=%h expected=%h actual=%h",a,d,fast_data);
        checks=checks+1;
    endtask
    task clear_collision(input bit background,input [7:0] mask);
        @(negedge clk);#1;fast_wd=mask;clr_m2m=!background;clr_m2d=background;
        @(negedge clk);#1;clr_m2m=0;clr_m2d=0;
    endtask
    task two_writes(input [5:0] a,input [7:0] first,input [7:0] second);
        @(negedge clk);#1;fast_wa=a;fast_wd=first;fast_wr=1;
        @(negedge clk);#1;fast_wd=second;
        @(negedge clk);#1;fast_wr=0;
    endtask
    always @(posedge clk) if(!reset) begin
        if(hold_d011 !== (phase>=10&&phase<=13))$fatal(1,"D011 acceptance hold phase");
        if(hold_spr !== ((observation[3:0]==12&&observation[231:229]==0&&phase>=14&&phase<=29)||
                         (observation[3:0]==8&&phase==13)))$fatal(1,"sprite acceptance hold phase");
        hold_checks=hold_checks+1;
        if(ena_pixel)pixels=pixels+1;
        if(phase==15)begin if(raster<last_y)wraps=wraps+1;last_y=raster;end
    end
    integer a;
    initial begin
        $readmemh("case.hex",config_bytes);ntsc=config_bytes[0];
        repeat(64)@(negedge clk);#1;reset=0;
        clear_collision(0,8'hff);clear_collision(1,8'hff);
        // All immediate stored fields, including MCDelay rather than the
        // pixel-latched MC value. Live raster/IRQ bytes are checked below.
        for(a=0;a<16;a=a+1)begin write_fast(a,8'(a*11));expect_read(a,8'(a*11));end
        write_fast('h10,'ha5);expect_read('h10,'ha5);
        write_fast('h15,'h5a);expect_read('h15,'h5a);
        write_fast('h16,'h2d);expect_read('h16,'hed);
        write_fast('h17,'ha5);expect_read('h17,'ha5);
        write_fast('h18,'h26);expect_read('h18,'h27);
        write_fast('h1a,'h0d);expect_read('h1a,'hfd);
        write_fast('h1b,'h5a);expect_read('h1b,'h5a);
        write_fast('h1c,'ha5);expect_read('h1c,'ha5);
        write_fast('h1d,'h5a);expect_read('h1d,'h5a);
        for(a=32;a<=46;a=a+1)begin write_fast(a,8'(a));expect_read(a,8'hf0|8'(a&15));end
        write_fast('h30,1);expect_read('h30,'hff);
        // A native select sets the full late-write barrier until next phi1.
        while(phase!=16)@(negedge clk);#1;cs=1;we=1;address='h20;data=3;
        @(negedge clk);#1;if(!native_busy)$fatal(1,"native VIC write barrier absent");
        while(phase!=0)@(negedge clk);#1;cs=0;we=0;
        while(phase!=16)@(negedge clk);@(posedge clk);#1;if(native_busy)$fatal(1,"native VIC barrier did not release");
        // Real overlapping sprites and background establish nonzero latches.
        write_fast('h10,0);write_fast('h17,0);write_fast('h1c,0);write_fast('h1d,0);
        write_fast('h11,'h1b);write_fast('h16,'h08);write_fast(0,64);write_fast(2,64);
        write_fast(1,80);write_fast(3,80);write_fast('h15,3);
        wait(raster==120);write_fast('h15,0);
        expect_read('h1e,3);expect_read('h1f,3);
        clear_collision(0,1);expect_read('h1e,2);clear_collision(0,2);expect_read('h1e,0);
        clear_collision(1,1);expect_read('h1f,2);clear_collision(1,2);expect_read('h1f,0);
        // Reset IRQ history before a new frame. One-edge DEN and compare
        // pulses must have persistent effects even though the final byte does not.
        write_fast('h11,'h0b);write_fast('h1a,1);write_fast('h19,'h0f);
        wait(wraps>=1&&raster==48);two_writes('h11,'h1b,'h0b);
        if(observation[47] !== 1)$fatal(1,"one-edge DEN pulse lost at line48");
        wait(raster==49);if(observation[47] !== 1)$fatal(1,"DEN effect not retained");
        wait(raster==60);two_writes('h12,60,61);
        if(observation[182] !== 1||irq_n!==0)$fatal(1,"one-edge compare match lost");
        write_fast('h19,1);@(negedge clk);#1;
        if(observation[182] !== 0||irq_n!==1)$fatal(1,"raster acknowledge did not settle");
        two_writes('h12,60,61);
        if(observation[182] !== 1)$fatal(1,"compare mismatch failed to rearm");
        write_fast('h19,1);@(negedge clk);#1;
        wait(raster==249);if(observation[47] !== 0)$fatal(1,"DEN sticky latch did not clear at frame tail");
        wait(wraps>=2&&raster==120);
        if(pixels<2*(ntsc?65*263:63*312)*8)$fatal(1,"incomplete frame coverage");
        $display("PASS VIC fast mode=%0d checks=%0d hold_edges=%0d pixel_edges=%0d",ntsc,checks,hold_checks,pixels);$finish;
    end
    initial begin repeat(4*312*65*32)@(posedge clk);$fatal(1,"VIC fast timeout");end
endmodule
