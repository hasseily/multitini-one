// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Cartridge mapper regression: machine savestates are disabled in production.
// This implements only the source VHDL register interface, so the actual
// selected mapper modules can be exercised in a portable SV simulation.
module eReg_SavestateV #(parameter Adr=0,parameter [63:0] def=0)(
    input clk,input [63:0] BUS_Din,input [9:0] BUS_Adr,
    input BUS_wren,BUS_rst,output [63:0] BUS_Dout,
    input [63:0] Din,output reg [63:0] Dout=def);
    always @(posedge clk)if(BUS_rst)Dout<=def;
        else if(BUS_wren&&BUS_Adr==Adr)Dout<=BUS_Din;
    assign BUS_Dout=BUS_Adr==Adr?Din:64'd0;
endmodule

module tb_cart_mappers;
    reg clk=0,ce_master=1,ce=0,cpu_ce=0,paused=0,reset=1,vs_mode=0;
    reg [63:0] flags=0;
    reg [15:0] prg_ain=16'h8000;
    wire [24:0] prg_aout;
    reg prg_read=0,prg_write=0;
    reg [7:0] prg_din=0,prg_from_ram=8'hff;
    wire [7:0] prg_dout;
    wire prg_allow,prg_bus_write,prg_conflict,has_savestate,prg_conflict_d0,has_flashsaves;
    reg [9:0] prg_mask=0,chr_mask=0;
    reg chr_ex=0,chr_read=0,chr_write=0;
    reg [7:0] chr_din=0;
    reg [13:0] chr_ain_orig=0,chr_ain_ex=0;
    wire [21:0] chr_aout;
    wire [7:0] chr_dout;
    wire has_chr_dout,chr_allow,vram_a10,vram_ce;
    wire [17:0] mapper_addr;
    reg [7:0] mapper_data_in=0;
    wire [7:0] mapper_data_out;
    wire mapper_prg_write,mapper_ovr,irq;
    reg [15:0] audio_in=16'h2468;
    wire [15:0] audio;
    wire [1:0] diskside;
    reg fds_busy=0,fds_eject=0,fds_auto_eject=0,fds_fast=0;
    reg [1:0] max_diskside=0;
    reg [63:0] SaveStateBus_Din=0;
    reg [9:0] SaveStateBus_Adr=0;
    reg SaveStateBus_wren=0,SaveStateBus_rst=1,SaveStateBus_load=0;
    wire [63:0] SaveStateBus_Dout;
    reg Savestate_MAPRAMactive=0;
    reg [12:0] Savestate_MAPRAMAddr=0;
    reg Savestate_MAPRAMRdEn=0,Savestate_MAPRAMWrEn=0;
    reg [7:0] Savestate_MAPRAMWriteData=0;
    wire [7:0] Savestate_MAPRAMReadData;
    integer assertions=0,mappers=0;
    reg [4:0] held_shift;
    reg [7:0] held_bank;
    always #5 clk=!clk;
    cart_top dut(.*);
    task tick;begin @(posedge clk);#1;end endtask
    task choose(input [7:0] mapper,input integer prg_kib,input integer chr_kib,
                input mirror_vertical,input battery);
        integer n,k;
        begin
            @(negedge clk);reset=1;ce_master=1;ce=0;cpu_ce=0;prg_write=0;prg_read=0;chr_read=0;
            prg_ain=16'h8000;chr_ain_orig=0;flags=0;flags[7:0]=mapper;
            flags[14]=mirror_vertical;flags[15]=chr_kib==0;flags[25]=battery;
            flags[29:26]=battery?0:7;flags[34:31]=battery?7:0;
            n=prg_kib/16;k=0;while(n>1)begin n=n/2;k=k+1;end flags[10:8]=k;
            n=chr_kib/8;k=0;while(n>1)begin n=n/2;k=k+1;end flags[13:11]=k;
            prg_mask=prg_kib/2-1;chr_mask=chr_kib==0?3:chr_kib/2-1;
            repeat(3)tick();@(negedge clk);reset=0;SaveStateBus_rst=0;tick();
            if(audio!==16'h1234)$fatal(1,"disabled mapper outputs contaminated audio mapper%0d",mapper);
            mappers=mappers+1;
        end
    endtask
    task wr(input [15:0] address,input [7:0] data);
        begin
            @(negedge clk);prg_ain=address;prg_din=data;prg_write=1;prg_read=0;ce=1;
            tick();@(negedge clk);prg_write=0;ce=1;tick();@(negedge clk);ce=0;
        end
    endtask
    task serial(input [15:0] address,input [4:0] data);
        integer b;
        begin for(b=0;b<5;b=b+1)wr(address,{7'd0,data[b]});end
    endtask
    task prg(input [15:0] address,input [24:0] expected);
        begin
            @(negedge clk);prg_ain=address;prg_read=1;prg_write=0;ce=0;#1;
            if(prg_aout!==expected||!prg_allow)
                $fatal(1,"mapper%0d PRG %h got%h allow%b expected%h",flags[7:0],address,prg_aout,prg_allow,expected);
            assertions=assertions+1;
        end
    endtask
    task chr(input [13:0] address,input [21:0] expected);
        begin
            @(negedge clk);chr_ain_orig=address;chr_read=0;#1;
            if(chr_aout!==expected)
                $fatal(1,"mapper%0d CHR %h got%h expected%h",flags[7:0],address,chr_aout,expected);
            assertions=assertions+1;
        end
    endtask
    task nt(input [13:0] address,input bank);
        begin
            chr(address,22'h3a0000+bank*1024+(address&1023));
            if(!vram_ce||vram_a10!==bank)$fatal(1,"mapper%0d mirroring",flags[7:0]);
        end
    endtask
    task ppu_read_dot(input [13:0] address);
        begin @(negedge clk);chr_ain_orig=address;chr_read=1;tick();@(negedge clk);chr_read=0;end
    endtask
    task a12_pulse;
        begin
            @(negedge clk);chr_ain_orig=0;cpu_ce=1;repeat(4)tick();
            @(negedge clk);cpu_ce=0;chr_ain_orig=14'h1000;tick();
        end
    endtask
    initial begin
        // NROM and shared CPU RAM override. All seven explicit mapper buses
        // feed this mux, so every address assertion also checks pruning.
        choose(0,32,8,0,0);prg(16'h8003,25'h3);prg(16'hc003,25'h4003);
        prg(16'h1805,25'h380005);prg(16'h6012,25'h3c0012);chr(14'h12,22'h200012);
        nt(14'h2402,0);nt(14'h2802,1);

        choose(2,256,0,1,0);prg(16'h8000,0);prg(16'hc000,25'h3c000);
        wr(16'h8000,5);prg(16'h8002,25'h14002);prg(16'hc002,25'h3c002);
        ce_master=0;wr(16'h8000,7);prg(16'h8002,25'h14002);ce_master=1;
        wr(16'h8000,7);prg(16'h8002,25'h1c002);chr(14'h12,22'h200012);
        if(!chr_allow)$fatal(1,"UxROM CHR RAM permission");nt(14'h2400,1);nt(14'h2800,0);

        choose(3,32,32,1,0);wr(16'h8000,2);chr(14'h12,22'h204012);prg(16'hc000,25'h4000);
        if(!prg_conflict)$fatal(1,"CNROM bus-conflict flag missing");
        ce_master=0;wr(16'h8000,3);chr(14'h12,22'h204012);ce_master=1;
        wr(16'h8000,0);chr(14'h12,22'h200012);

        choose(7,256,0,0,0);wr(16'h8000,8'h13);prg(16'h8002,25'h18002);prg(16'hc002,25'h1c002);
        nt(14'h2002,1);nt(14'h2802,1);wr(16'h8000,2);prg(16'h8002,25'h10002);nt(14'h2c02,0);

        choose(66,128,32,1,0);wr(16'h8000,8'h21);prg(16'h8002,25'h10002);chr(14'h12,22'h202012);nt(14'h2400,1);
        choose(11,128,128,0,0);wr(16'h8000,8'h32);prg(16'h8002,25'h10002);chr(14'h12,22'h206012);nt(14'h2800,1);
        choose(71,256,0,1,0);wr(16'hc000,3);prg(16'h8002,25'hc002);prg(16'hc002,25'h3c002);nt(14'h2400,1);

        // MMC1 five-bit serial bank register, both CHR bank modes, all four
        // mirroring modes and battery-only flags that must not create SOROM.
        choose(1,256,128,0,1);serial(16'h8000,5'h1e);serial(16'he000,3);
        prg(16'h8002,25'hc002);prg(16'hc002,25'h3c002);
        serial(16'ha000,4);serial(16'hc000,7);chr(14'h12,22'h204012);chr(14'h1012,22'h207012);
        prg(16'h6012,25'h3c0012);serial(16'ha000,16);prg(16'h6012,25'h3c0012);
        nt(14'h2400,1);nt(14'h2800,0);
        serial(16'h8000,5'h1f);nt(14'h2400,0);nt(14'h2800,1);
        serial(16'h8000,5'h1d);nt(14'h2000,1);nt(14'h2c00,1);
        serial(16'h8000,5'h1c);nt(14'h2000,0);nt(14'h2c00,0);
        ce_master=0;held_shift=dut.mmc1.shift;serial(16'he000,7);
        if(dut.mmc1.shift!==held_shift)$fatal(1,"MMC1 serial shift advanced without master CE");
        prg(16'h8002,25'hc002);ce_master=1;serial(16'he000,7);prg(16'h8002,25'h1c002);

        // MMC3 PRG inversion, 1/2KiB CHR banking, SRAM enable/protection and
        // qualified A12 edges. Master CE holds the IRQ and bank state too.
        choose(4,512,256,1,1);wr(16'h8000,6);wr(16'h8001,9);prg(16'h8002,25'h12002);prg(16'hc002,25'h7c002);
        wr(16'h8000,7);wr(16'h8001,2);prg(16'ha002,25'h4002);
        wr(16'h8000,8'h46);prg(16'h8002,25'h7c002);prg(16'hc002,25'h12002);
        wr(16'h8000,2);wr(16'h8001,5);chr(14'h1012,22'h201412);
        wr(16'h8000,0);wr(16'h8001,6);chr(14'h12,22'h201812);chr(14'h412,22'h201c12);
        wr(16'h8000,8'h80);chr(14'h1012,22'h201812);chr(14'h12,22'h201412);
        wr(16'ha000,1);nt(14'h2400,0);nt(14'h2800,1);wr(16'ha000,0);nt(14'h2400,1);nt(14'h2800,0);
        wr(16'ha001,8'h80);prg(16'h6012,25'h3c0012);
        @(negedge clk);prg_write=1;prg_read=0;#1;if(!prg_allow)$fatal(1,"MMC3 enabled SRAM write denied");
        wr(16'ha001,8'hc0);prg(16'h6012,25'h3c0012);
        @(negedge clk);prg_write=1;prg_read=0;#1;if(prg_allow)$fatal(1,"MMC3 protected SRAM write allowed");
        wr(16'ha001,0);@(negedge clk);prg_ain=16'h6012;prg_read=1;#1;if(prg_allow)$fatal(1,"MMC3 disabled SRAM read allowed");
        wr(16'hc000,2);wr(16'hc001,0);wr(16'he001,0);a12_pulse();if(irq)$fatal(1,"MMC3 premature IRQ after reload");
        a12_pulse();if(irq)$fatal(1,"MMC3 premature IRQ before zero");a12_pulse();if(!irq)$fatal(1,"MMC3 scanline IRQ missing");
        ce_master=0;wr(16'he000,0);if(!irq)$fatal(1,"MMC3 IRQ clear bypassed master CE");ce_master=1;wr(16'he000,0);if(irq)$fatal(1,"MMC3 IRQ disable failed");

        // MMC2 and MMC4 retain distinct PRG sizes and PPU latch windows.
        choose(9,128,128,0,0);wr(16'ha000,3);prg(16'h8002,25'h6002);prg(16'ha002,25'h1a002);prg(16'he002,25'h1e002);
        wr(16'hb000,2);wr(16'hc000,5);wr(16'hd000,7);wr(16'he000,9);
        chr(14'h12,22'h202012);ppu_read_dot(14'h0fe8);chr(14'h12,22'h205012);
        ce_master=0;ppu_read_dot(14'h0fd8);chr(14'h12,22'h205012);ce_master=1;
        ppu_read_dot(14'h0fd8);chr(14'h12,22'h202012);ppu_read_dot(14'h1fea);chr(14'h1012,22'h209012);
        wr(16'hf000,1);nt(14'h2400,0);nt(14'h2800,1);
        choose(10,256,128,0,1);wr(16'ha000,4);prg(16'h8002,25'h10002);prg(16'hc002,25'h3c002);prg(16'h6012,25'h3c0012);
        wr(16'hb000,3);wr(16'hc000,6);chr(14'h12,22'h203012);ppu_read_dot(14'h0feb);chr(14'h12,22'h206012);
        ppu_read_dot(14'h0fdb);chr(14'h12,22'h203012);wr(16'hf000,0);nt(14'h2400,1);nt(14'h2800,0);
        $display("PASS NES mappers: %0d mapper IDs, %0d mapped-address assertions, master CE, MMC1 serial/RAM flags, MMC3 SRAM protection/IRQ and MMC2/MMC4 latches",mappers,assertions);
        $finish;
    end
    initial begin #100000;$fatal(1,"mapper review timeout");end
endmodule
