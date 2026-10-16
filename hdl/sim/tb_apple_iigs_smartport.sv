// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_smartport;
    reg CLK_14M=0;always #5 CLK_14M=~CLK_14M;
    reg RESET=1,phi0=0,IO_SELECT=0,DEVICE_SELECT=0,RD=1;
    reg [15:0] A=0;reg [7:0] cpu_data=0;
    wire [7:0] D_OUT;wire DMA,DMA_WE;wire [23:0] DMA_ADDR;reg [7:0] DMA_BANK=0;
    wire [7:0] D_IN=DMA && !DMA_WE ? ((DMA_ADDR*7+19)&255) : cpu_data;
    wire [31:0] sector;wire [7:0] sd_rd,sd_wr;
    reg [7:0] img_mounted=0;reg img_readonly=0;reg [63:0] img_size=0;
    reg [8:0] hps_ram_addr=0;reg [7:0] ram_di=0;wire [7:0] ram_do;
    reg ram_we=0;reg [7:0] sd_ack=0,sd_error=0;
    reg load_rom_we=0;reg [11:0] load_rom_addr=0;reg [7:0] load_rom_data=0;
    hdd #(.EXTERNAL_BUS(1)) dut(.load_rom_clk(CLK_14M),.*);
    integer dma_bytes=0;reg [23:0] expected_address=0;reg checking_read=0;
    always @(posedge CLK_14M)if(!RESET && DMA && DMA_WE && phi0)begin
        if(!checking_read || DMA_ADDR!==expected_address+dma_bytes || D_OUT!==((dma_bytes*11+3)&255))
            $fatal(1,"DMA read byte%0d addr=%x expected=%x data=%x",dma_bytes,DMA_ADDR,expected_address+dma_bytes,D_OUT);
        dma_bytes=dma_bytes+1;
    end
    task idle(input integer edges);repeat(edges)@(negedge CLK_14M);endtask
    task write_reg(input [3:0] r,input [7:0] v);begin
        @(negedge CLK_14M);A=16'hc0f0+r;cpu_data=v;RD=0;DEVICE_SELECT=1;phi0=1;
        @(negedge CLK_14M);phi0=0;DEVICE_SELECT=0;RD=1;idle(2);
    end endtask
    task read_reg(input [3:0] r,input [7:0] expected);begin
        @(negedge CLK_14M);A=16'hc0f0+r;RD=1;DEVICE_SELECT=1;
        idle(3);if(D_OUT!==expected)$fatal(1,"reg%0x got%02x expected%02x",r,D_OUT,expected);
        DEVICE_SELECT=0;idle(2);
    end endtask
    task mount(input integer unit,input [31:0] blocks,input bit protect);begin
        img_mounted=8'b1<<unit;img_size={23'd0,blocks,9'd0};img_readonly=protect;
        idle(2);img_mounted=0;idle(2);
    end endtask
    task setup(input [7:0] command,input [7:0] unit,input [31:0] block,input [23:0] address);begin
        write_reg(2,command);write_reg(3,unit);write_reg(4,address[7:0]);write_reg(5,address[15:8]);
        write_reg(8,address[23:16]);write_reg(6,block[7:0]);write_reg(7,block[15:8]);
        write_reg(9,block[23:16]);write_reg(10,block[31:24]);
    end endtask
    task execute;begin
        @(negedge CLK_14M);A=16'hc0f0;RD=1;DEVICE_SELECT=1;phi0=1;
        @(negedge CLK_14M);phi0=0;DEVICE_SELECT=0;idle(2);
    end endtask
    task cycle_dma;begin phi0=0;idle(2);phi0=1;idle(1);phi0=0;end endtask
    task complete(input integer unit,input [7:0] error);begin
        sd_ack=8'b1<<unit;idle(3);sd_error=error;idle(3);sd_ack=0;
    end endtask
    task drain;begin
        for(integer i=0;i<520 && DMA;i=i+1)cycle_dma();
        if(DMA)$fatal(1,"DMA never completed");idle(4);
    end endtask
    initial begin
        idle(4);RESET=0;
        for(integer i=0;i<8;i=i+1)begin
            mount(i,32'h80010000+i,0);setup(8'h80,i+1,0,0);
            read_reg(11,i[7:0]);read_reg(12,0);read_reg(13,1);read_reg(14,8'h80);
        end
        read_reg(15,8'h81);
        $display("PASS: eight direct units,32-bit capacity, mounted/protect flags");
        // The complete512-byte DMA crosses a64KiB bank boundary.
        setup(8'hc1,8,32'h8000ffff,24'h02ff80);expected_address=24'h02ff80;dma_bytes=0;checking_read=1;
        execute();if(sd_rd!==8'h80 || sector!==32'h8000ffff)$fatal(1,"wide read request/unit");
        sd_ack=8'h80;idle(3);
        for(integer i=0;i<512;i=i+1)begin hps_ram_addr=i;ram_di=(i*11+3)&255;ram_we=1;idle(1);end
        ram_we=0;sd_ack=0;sd_error=0;drain();checking_read=0;
        if(dma_bytes!=512)$fatal(1,"read count%0d",dma_bytes);read_reg(1,0);
        $display("PASS: extended unit8 LBA8000ffff and24-bit read DMA bank crossing all512bytes");
        setup(8'hc2,7,32'h80000001,24'h04fff0);execute();
        for(integer i=0;i<520 && sd_wr==0;i=i+1)cycle_dma();
        if(sd_wr!==8'h40 || sector!==32'h80000001)$fatal(1,"wide write request/unit");
        for(integer i=0;i<512;i=i+1)if(dut.sector_ram.mem[i]!==(((24'h04fff0+i)*7+19)&255))
            $fatal(1,"write byte%0d=%x",i,dut.sector_ram.mem[i]);
        complete(6,0);drain();read_reg(1,0);
        $display("PASS: extended write full512bytes,24-bit bank carry,32-bit block");
        // Failed disk reads must never start the RAM-write phase.
        setup(8'h81,8,0,24'h2000);execute();complete(7,8'h27);drain();read_reg(1,8'h27);
        setup(8'h82,8,0,24'h2000);execute();
        for(integer i=0;i<520 && sd_wr==0;i=i+1)cycle_dma();
        complete(7,8'h2b);drain();read_reg(1,8'h2b);
        mount(7,32'h10000,1);setup(8'h82,8,0,0);execute();read_reg(1,8'h2b);
        if(DMA || sd_wr || sd_rd)$fatal(1,"protected command issued media request");
        setup(8'h81,8,32'h10000,0);execute();read_reg(1,8'h2d);
        mount(7,0,0);setup(8'h81,8,0,0);execute();read_reg(1,8'h2f);read_reg(11,0);read_reg(15,8'h70);
        setup(8'h81,9,0,0);execute();read_reg(1,8'h28);
        setup(8'h83,1,0,0);execute();read_reg(1,8'h21);
        setup(8'hc1,1,0,24'hfffe01);execute();read_reg(1,8'h22);
        if(DMA || sd_rd || sd_wr)$fatal(1,"24-bit buffer overflow wrapped into RAM");
        $display("PASS: read/write I/O failures, offline, protection,bad block and invalid unit/command");
        setup(1,8'hf0,1,24'h007f80);DMA_BANK=8'h06;execute();
        if(sd_rd!==2 || DMA_ADDR!==24'h067f80)$fatal(1,"legacy unit/bank compatibility");
        RESET=1;idle(2);RESET=0;idle(2);
        if(DMA || DMA_WE || sd_rd || sd_wr)$fatal(1,"reset failed to abort pending request");
        setup(8'h80,1,0,0);read_reg(11,0);read_reg(14,8'h80);
        $display("PASS: legacy ProDOS unit/DMAbank and reset abort retaining media metadata");
        for(integer i=0;i<2304;i=i+1)begin
            load_rom_addr=i;load_rom_data=(i*13+(i>>8))&255;load_rom_we=1;idle(1);
        end
        load_rom_we=0;IO_SELECT=1;
        A=16'hc700;idle(3);if(D_OUT!==0)$fatal(1,"Cn ROM map");
        A=16'hc800;idle(3);if(D_OUT!==1)$fatal(1,"C800 ROM map got%x",D_OUT);
        A=16'hcfff;idle(3);if(D_OUT!==((2303*13+8)&255))$fatal(1,"CFFF ROM map");
        $display("PASS: runtime2304-byte Cn/C800 ROM map");$finish;
    end
    initial begin #2000000;$fatal(1,"SmartPort controller timeout");end
endmodule
