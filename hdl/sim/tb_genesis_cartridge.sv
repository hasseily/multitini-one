// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_genesis_cartridge;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,pause_request=0,cart_dl=0,cart_dl_wr=0;
    reg [22:0] cart_load_addr=0;
    reg [15:0] cart_load_data=16'hffff;
    reg [23:1] cart_addr=0;
    reg [15:0] cart_data_wr=0;
    reg cart_cs=0,cart_oe=0,cart_lwr=0,cart_uwr=0,cart_time=0,cart_dma=0;
    wire [15:0] cart_data;
    wire cart_data_en,cart_dtack,save_change,ym2612_quirk,rom_request,rom_stall;
    reg [15:0] save_addr=0;
    reg [7:0] save_data=0;
    reg save_wr=0;
    wire [7:0] save_q;
    wire [23:0] rom_address;
    reg [15:0] rom_data=16'habcd;
    reg rom_data_valid=1;
    multitini_genesis_cartridge dut(.*);
    task automatic tick;@(negedge clk);endtask
    task automatic load_word(input [22:0] address,input [15:0] value);
        cart_load_addr=address;cart_load_data=value;cart_dl_wr=1;tick;
        cart_dl_wr=0;tick;
    endtask
    task automatic cart_write(input [23:0] address,input [15:0] value,input bit time_select);
        cart_addr=address[23:1];cart_data_wr=value;cart_cs=!time_select;
        cart_time=time_select;cart_lwr=1;repeat(4)tick;
        cart_lwr=0;cart_time=0;cart_cs=0;repeat(4)tick;
    endtask
    task automatic identified_cart(input [87:0] identity);
        reset=1;cart_oe=0;cart_cs=0;cart_dl=1;tick;
        load_word(0,16'h00ff);
        for(integer part=0;part<5;part=part+1)load_word(23'hc0+part,identity[87-part*16-:16]);
        load_word(23'hc5,{identity[7:0],8'h00});
        load_word(23'hc8,16'h0000);load_word(511,16'hffff);
        cart_dl=0;repeat(65540)tick;reset=0;repeat(4)tick;
    endtask
    task automatic ee_line(input bit scl_value,input bit sda_value);
        cart_addr=24'h200000>>1;cart_data_wr={14'd0,scl_value,sda_value};
        cart_lwr=1;cart_cs=1;repeat(12)tick;
    endtask
    task automatic ee_byte(input [7:0] value);
        for(integer b=7;b>=0;b=b-1)begin ee_line(0,value[b]);ee_line(1,value[b]);ee_line(0,value[b]);end
        ee_line(0,1);ee_line(1,1);ee_line(0,1);
    endtask
    initial begin
        repeat(5)tick;cart_dl=1;tick;
        load_word(0,16'h00ff);load_word(511,16'hffff);
        cart_dl=0;tick;
        // A minimum-size cartridge is followed immediately by its complete
        // ascending save restore, while initialization is still active.
        for(integer i=0;i<65536;i=i+1)begin
            save_addr=i;save_data=i[7:0]^8'h5a;save_wr=1;tick;
        end
        save_wr=0;repeat(4)tick;
        for(integer i=0;i<65536;i=i+1)
            if(dut.save_memory[i] !== (i[7:0]^8'h5a))$fatal(1,"early restore lost byte %0d",i);
        reset=0;repeat(4)tick;
        cart_addr=23'h000080;cart_oe=1;cart_cs=1;repeat(4)tick;
        if(rom_address!==24'h000100 || !rom_request || cart_data!==16'habcd || !cart_data_en)$fatal(1,"ROM mapping");
        rom_data_valid=0;tick;if(!rom_stall)$fatal(1,"ROM miss must stall");
        rom_data_valid=1;cart_oe=0;cart_cs=0;tick;
        cart_write(24'h200000,16'h00cc,0);save_addr=0;repeat(4)tick;
        if(save_q!==8'hcc)$fatal(1,"SRAM write/read");
        pause_request=1;cart_write(24'h200000,16'h00dd,0);repeat(4)tick;
        if(save_q!==8'hcc)$fatal(1,"paused held SRAM bus wrote");
        pause_request=0;reset=1;repeat(20)tick;reset=0;repeat(4)tick;
        if(save_q!==8'hcc)$fatal(1,"soft reset erased save");
        // SSF2 uses eight 512 KiB windows for ROMs larger than 4 MiB.
        reset=1;cart_dl=1;tick;load_word(0,16'h00ff);load_word(23'h27ffff,16'hffff);
        cart_dl=0;repeat(65540)tick;reset=0;repeat(4)tick;
        cart_write(24'ha130f3,16'h0008,1);
        cart_addr=24'h080000>>1;cart_oe=1;cart_cs=1;repeat(4)tick;
        if(rom_address!==24'h400000)$fatal(1,"SSF2 bank mapping %h",rom_address);
        identified_cart("GM  GM 0000");
        if(!dut.sram00_quirk)$fatal(1,"zero-fill header was not detected");
        for(integer i=0;i<65536;i=i+1)
            if(dut.save_memory[i]!==8'h00)$fatal(1,"zero-fill header left stale byte %0d",i);
        identified_cart("GM T-81033 ");
        if(dut.eeprom_quirk!==3)$fatal(1,"24C02 header was not detected");
        ee_line(1,1);ee_line(1,0);ee_line(0,0);
        ee_byte(8'ha0);ee_byte(8'h12);
        // Stop the physical chip immediately after its last SCL edge. The
        // already accepted serial write must drain into save RAM in pause.
        for(integer b=7;b>0;b=b-1)begin ee_line(0,8'h76>>b);ee_line(1,8'h76>>b);ee_line(0,8'h76>>b);end
        ee_line(0,0);
        cart_data_wr=16'h0002; // eighth rising SCL, SDA=0
        wait(dut.eeprom_ram_we);
        pause_request=1;
        repeat(4)tick;
        if(dut.save_memory[16'h12]!==8'h76)$fatal(1,"pause discarded committed EEPROM write");
        $display("PASS Genesis cartridge save restore, SRAM/reset/pause, SSF2, zero-fill and draining EEPROM writes");
        $finish;
    end
endmodule
