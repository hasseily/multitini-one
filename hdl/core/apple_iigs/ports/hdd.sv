//============================================================================
//  ProDOS HDD interface
//
//  Modelled on the AppleWin hard-disk controller (see AppleWin's
//  source/Harddisk.cpp for the register semantics). The boot ROM in
//  rtl/roms/hdd.a65 carries its own AppleWin attribution.
//
//  Copyright (c) Steven A. Wilson
//  Copyright (c) the AppleWin project -- original controller interface
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 3 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
//  more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program. If not, see <https://www.gnu.org/licenses/>.
//============================================================================
// Multitini port, 2026-10-14: reproduced by scripts/port_apple_iigs_core.py.
// Slot-7 ProDOS / SmartPort disk engine. C0F0 executes, C0F1 is final
// error (80 busy). F2 command bit7 selects direct SmartPort units1..8;
// bit6 records extended calls. Legacy commands0..2 use unit bit7.
// F4/5/F8=24-bit buffer, F6/7/9/A=32-bit LBA. Legacy DMA uses DMA_BANK.
// FB..FE=selected unit32-bit blockcount, FF=count:0000:protect:mounted.
module hdd #(parameter EXTERNAL_BUS=0)(
    input load_rom_clk,load_rom_we,input [11:0] load_rom_addr,input [7:0] load_rom_data,
    input CLK_14M,phi0,IO_SELECT,DEVICE_SELECT,RESET,
    input [15:0] A,input RD,input [7:0] D_IN,output reg [7:0] D_OUT,
    output reg DMA,output wire [23:0] DMA_ADDR,input [7:0] DMA_BANK=8'd0,
    output reg DMA_WE,output wire [31:0] sector,
    output reg [7:0] sd_rd,sd_wr,input [7:0] img_mounted,
    input img_readonly,input [63:0] img_size,
    input [8:0] hps_ram_addr,input [7:0] ram_di,output reg [7:0] ram_do,
    input ram_we,input [7:0] sd_ack,input [7:0] sd_error=8'd0
);
    reg [7:0] reg_status,reg_command,reg_unit,reg_mem_l,reg_mem_h,reg_mem_bank;
    reg [7:0] reg_block_l,reg_block_h,reg_block_u,reg_block_v;
    reg [7:0] hdd_mounted=0,hdd_protect=0;
    reg [31:0] hdd_size[0:7];
    reg [8:0] a2_ram_addr;
    reg [2:0] active_unit;
    reg [23:0] active_buffer;
    reg active_smart;
    reg [31:0] active_sector;
    wire [2:0] hdd_unit=reg_command[7] ? reg_unit[2:0]-3'd1 : {2'd0,reg_unit[7]};
    wire unit_valid=!reg_command[7] || (reg_unit>=1 && reg_unit<=8);
    wire current_unit_mounted=unit_valid && hdd_mounted[hdd_unit];
    wire current_unit_protect=hdd_protect[hdd_unit];
    wire any_unit_mounted=|hdd_mounted;
    wire [31:0] block_number=reg_command[7] ? {reg_block_v,reg_block_u,reg_block_h,reg_block_l} : {16'd0,reg_block_h,reg_block_l};
    wire [31:0] current_size=current_unit_mounted ? hdd_size[hdd_unit] : 32'd0;
    wire [15:0] legacy_size=|current_size[31:16] ? 16'hffff : current_size[15:0];
    reg [3:0] present_count;
    integer n;
    always @*begin
        present_count=0;
        for(integer unit_index=0;unit_index<8;unit_index=unit_index+1)
            present_count=present_count+{3'd0,hdd_mounted[unit_index]};
    end
    initial for(integer i=0;i<8;i=i+1)hdd_size[i]=0;
    always @(posedge CLK_14M)begin
        for(integer i=0;i<8;i=i+1)if(img_mounted[i])begin
            hdd_mounted[i]<=|img_size;
            hdd_protect[i]<=img_readonly;
            hdd_size[i]<=img_size[40:9];
        end
    end
    localparam ST_IDLE=0,ST_RD_ACK=1,ST_RD_HPS=2,ST_RD_A2=3,
        ST_WR_PRE=4,ST_WR_A2=5,ST_WR_ACK=6,ST_WR_HPS=7;
    reg [2:0] dma_state;
    wire dma_req_rd=DEVICE_SELECT && RD && A[3:0]==0 && phi0 && reg_command[5:0]==1;
    wire [7:0] sector_dout,sector_dma_q,rom_dout;
    wire a2_ram_we=(dma_state==ST_WR_PRE || dma_state==ST_WR_A2);
    wire [15:0] legacy_dma_offset=active_buffer[15:0]+{7'd0,a2_ram_addr};
    assign DMA_ADDR=active_smart ? active_buffer+{15'd0,a2_ram_addr} : {active_buffer[23:16],legacy_dma_offset};
    assign sector=active_sector;
    wire [7:0] completion_error=(sd_error==8'h28 || sd_error==8'h2b || sd_error==8'h2d || sd_error==8'h2f) ? sd_error : sd_error==0 ? 8'd0 : 8'h27;
    // A native bus byte retires only on phi0; read/BRAM pipelines remain
    // live between retirements exactly as in the upstream DMA engine.
    always @(posedge CLK_14M)begin
        if(RESET)begin
            dma_state<=ST_IDLE;DMA<=0;DMA_WE<=0;sd_rd<=0;sd_wr<=0;
            a2_ram_addr<=0;active_unit<=0;active_buffer<=0;active_sector<=0;active_smart<=0;
            reg_status<=0;reg_command<=0;reg_unit<=0;reg_mem_l<=0;reg_mem_h<=0;reg_mem_bank<=0;
            reg_block_l<=0;reg_block_h<=0;reg_block_u<=0;reg_block_v<=0;
        end else begin
            case(dma_state)
                ST_RD_ACK:if(sd_ack[active_unit])begin sd_rd[active_unit]<=0;dma_state<=ST_RD_HPS;end
                ST_RD_HPS:if(!sd_ack[active_unit] && phi0)begin
                    if(completion_error!=0)begin reg_status<=completion_error;DMA<=0;dma_state<=ST_IDLE;end
                    else begin a2_ram_addr<=0;DMA_WE<=1;dma_state<=ST_RD_A2;end
                end
                ST_RD_A2:if(phi0)begin
                    a2_ram_addr<=a2_ram_addr+1'b1;
                    if(a2_ram_addr==511)begin DMA_WE<=0;DMA<=0;reg_status<=0;dma_state<=ST_IDLE;end
                end
                ST_WR_PRE:if(phi0)dma_state<=ST_WR_A2;
                ST_WR_A2:if(phi0)begin
                    a2_ram_addr<=a2_ram_addr+1'b1;
                    if(a2_ram_addr==511)begin sd_wr[active_unit]<=1;dma_state<=ST_WR_ACK;end
                end
                ST_WR_ACK:if(sd_ack[active_unit])begin sd_wr[active_unit]<=0;dma_state<=ST_WR_HPS;end
                ST_WR_HPS:if(!sd_ack[active_unit] && phi0)begin
                    DMA<=0;reg_status<=completion_error;dma_state<=ST_IDLE;
                end
                default:;
            endcase
            if(phi0 && dma_state==ST_IDLE && DEVICE_SELECT)begin
                if(!RD)case(A[3:0])
                    2:begin reg_command<=D_IN;reg_status<=0;end
                    3:reg_unit<=D_IN;
                    4:reg_mem_l<=D_IN;
                    5:reg_mem_h<=D_IN;
                    6:reg_block_l<=D_IN;
                    7:reg_block_h<=D_IN;
                    8:reg_mem_bank<=D_IN;
                    9:reg_block_u<=D_IN;
                    10:reg_block_v<=D_IN;
                    default:;
                endcase
                else if(A[3:0]==0)begin
                    if(!unit_valid)reg_status<=8'h28;
                    else if(!current_unit_mounted)reg_status<=8'h2f;
                    else if(reg_command[5:0]==0)reg_status<=0;
                    else if(reg_command[5:0]!=1 && reg_command[5:0]!=2)reg_status<=8'h21;
                    else if(block_number>=current_size)reg_status<=8'h2d;
                    else if(reg_command[7] && {reg_mem_bank,reg_mem_h,reg_mem_l}>24'hfffe00)reg_status<=8'h22;
                    else if(reg_command[5:0]==2 && current_unit_protect)reg_status<=8'h2b;
                    else begin
                        reg_status<=8'h80;active_unit<=hdd_unit;active_sector<=block_number;active_smart<=reg_command[7];
                        active_buffer<={reg_command[7] ? reg_mem_bank : DMA_BANK,reg_mem_h,reg_mem_l};
                        a2_ram_addr<=0;DMA<=1;DMA_WE<=0;
                        if(reg_command[5:0]==1)begin sd_rd[hdd_unit]<=1;dma_state<=ST_RD_ACK;end
                        else dma_state<=ST_WR_PRE;
                    end
                end
            end
        end
    end
    // Registered native read mirror. C0F0's final result is sampled only
    // after DMA releases CPU, so errors reach legacy callers as well.
    always @(posedge CLK_14M)begin
        D_OUT<=8'hff;
        if(DMA && DMA_WE)D_OUT<=sector_dout;
        else if(DEVICE_SELECT && RD)case(A[3:0])
            0:begin
                if(DMA)D_OUT<=reg_status;
                else if(!unit_valid)D_OUT<=8'h28;
                else if(!current_unit_mounted)D_OUT<=8'h2f;
                else if(reg_command[5:0]==0)D_OUT<=0;
                else if(reg_command[5:0]!=1 && reg_command[5:0]!=2)D_OUT<=8'h21;
                else if(block_number>=current_size)D_OUT<=8'h2d;
                else if(reg_command[7] && {reg_mem_bank,reg_mem_h,reg_mem_l}>24'hfffe00)D_OUT<=8'h22;
                else if(reg_command[5:0]==2 && current_unit_protect)D_OUT<=8'h2b;
                else D_OUT<=reg_status;
            end
            1:D_OUT<=reg_status;
            2:D_OUT<=reg_command;
            3:D_OUT<=reg_unit;
            4:D_OUT<=reg_mem_l;
            5:D_OUT<=reg_mem_h;
            6:D_OUT<=reg_block_l;
            7:D_OUT<=reg_block_h;
            8:D_OUT<=reg_mem_bank;
            9:D_OUT<=legacy_size[7:0];
            10:D_OUT<=legacy_size[15:8];
            11:D_OUT<=current_size[7:0];
            12:D_OUT<=current_size[15:8];
            13:D_OUT<=current_size[23:16];
            14:D_OUT<=current_size[31:24];
            15:D_OUT<={present_count,2'b00,current_unit_mounted && current_unit_protect,current_unit_mounted};
        endcase
        else if(IO_SELECT && RD)D_OUT<=any_unit_mounted ? rom_dout : 8'hff;
    end
    bram #(.widthad_a(9)) sector_ram(
        .clock_a(CLK_14M),.wren_a(ram_we),.address_a(hps_ram_addr),.data_a(ram_di),.q_a(sector_dma_q),
        .clock_b(CLK_14M),.wren_b(a2_ram_we && phi0),.address_b(a2_ram_addr),.data_b(D_IN),.q_b(sector_dout),
        .enable_a(1'b1),.enable_b(1'b1));
    always @(posedge CLK_14M)ram_do<=sector_dma_q;
    wire [11:0] rom_address=A[11:8]==4'h7 ? {4'd0,A[7:0]} : 12'h100+{1'd0,A[10:0]};
    apple_iigs_runtime_rom #(.AW(12)) hddrom(
        .load_clk(load_rom_clk),.load_we(load_rom_we),.load_addr(load_rom_addr),.load_data(load_rom_data),
        .clock(CLK_14M),.ce(1'b1),.address(rom_address),.q(rom_dout));
endmodule
