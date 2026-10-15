// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Local native RAM and independent 512-byte CPU/PPU read caches. Only cart ROM
// is in PSRAM. An outstanding read holds all master state, preserving bus and
// mapper ordering. Service requests stay stable until accepted.
module multitini_nes_memory(
    input wire clk,reset,invalidate,master_ce,machine_reset,chr_is_ram,
    input wire [24:0] cpu_addr,input wire cpu_read,cpu_write,input wire [7:0] cpu_wdata,
    output wire [7:0] cpu_rdata,
    input wire [21:0] ppu_addr,input wire ppu_read,ppu_write,input wire [7:0] ppu_wdata,
    output wire [7:0] ppu_rdata,output wire stall,
    input wire [12:0] save_address,input wire [7:0] save_data,input wire save_write,
    output reg runtime_write_committed,output reg [23:0] runtime_write_address,
    input wire save_read,input wire [23:0] save_addr,output wire save_ready,
    output reg [63:0] save_rdata,output reg save_rvalid,
    input wire [23:0] loader_addr,input wire [63:0] loader_wdata,
    input wire [7:0] loader_wstrb,input wire loader_valid,
    output wire loader_ready,output reg loader_response,
    output wire [23:0] memory_addr,output wire memory_write,
    output wire [63:0] memory_wdata,output wire [7:0] memory_wstrb,
    output wire memory_valid,input wire memory_ready,
    input wire [63:0] memory_rdata,input wire memory_rvalid
);
    (* ram_style="distributed" *) reg [7:0] cpu_ram[0:2047];
    (* ram_style="distributed" *) reg [7:0] nt_ram[0:2047];
    (* ram_style="distributed" *) reg [7:0] chr_ram[0:8191];
    // Eight byte lanes give the snapshot a natural64-bit word without an
    // eight-read-port memory. Each lane has exactly one priority write port.
    wire [63:0] save_word;
    wire [9:0] save_read_word=save_read?save_addr[12:3]:cpu_addr[12:3];
    (* ram_style="distributed" *) reg [63:0] cpu_lines[0:63],ppu_lines[0:63];
    reg [14:0] cpu_tags[0:63],ppu_tags[0:63];
    reg [63:0] cpu_valid,ppu_valid;
    reg clearing;
    reg [12:0] clear_address;
    wire cpu_is_ram=cpu_addr[21:17]==5'b11100;
    wire cpu_is_save=cpu_addr[21:18]==4'b1111;
    wire ppu_is_nt=ppu_addr[21:17]==5'b11101;
    wire ppu_is_chr_ram=chr_is_ram&&ppu_addr[21:20]==2'b10;
    wire cpu_ext=cpu_read&&!cpu_is_ram&&!cpu_is_save;
    wire ppu_ext=ppu_read&&!ppu_is_nt&&!ppu_is_chr_ram;
    wire cpu_hit=cpu_valid[cpu_addr[8:3]]&&cpu_tags[cpu_addr[8:3]]==cpu_addr[23:9];
    wire ppu_hit=ppu_valid[ppu_addr[8:3]]&&ppu_tags[ppu_addr[8:3]]=={2'd0,ppu_addr[21:9]};
    assign cpu_rdata=cpu_is_ram?cpu_ram[cpu_addr[10:0]]:cpu_is_save?save_word[cpu_addr[2:0]*8+:8]:
        cpu_hit?cpu_lines[cpu_addr[8:3]][cpu_addr[2:0]*8+:8]:8'hff;
    assign ppu_rdata=ppu_is_nt?nt_ram[ppu_addr[10:0]]:ppu_is_chr_ram?chr_ram[ppu_addr[12:0]]:
        ppu_hit?ppu_lines[ppu_addr[8:3]][ppu_addr[2:0]*8+:8]:8'hff;
    reg outstanding;
    reg discard;
    reg [1:0] owner;
    reg [23:0] pending_address;
    reg choose_ppu;
    wire cpu_miss=cpu_ext&&!cpu_hit;
    wire ppu_miss=ppu_ext&&!ppu_hit;
    wire select_ppu=ppu_miss&&(!cpu_miss||choose_ppu);
    assign stall=clearing||(!machine_reset&&(cpu_miss||ppu_miss));
    assign memory_valid=!outstanding&&!invalidate&&(loader_valid||(!machine_reset&&(cpu_miss||ppu_miss)));
    assign memory_addr=loader_valid?loader_addr:select_ppu?{2'd0,ppu_addr[21:3],3'd0}:{cpu_addr[23:3],3'd0};
    assign memory_write=loader_valid;
    assign memory_wdata=loader_wdata;
    assign memory_wstrb=loader_valid?loader_wstrb:8'd0;
    assign loader_ready=!outstanding&&!invalidate&&memory_ready;
    assign save_ready=!clearing;
    integer i;
    genvar lane;
    generate for(lane=0;lane<8;lane=lane+1)begin:save_lanes
        (* ram_style="distributed" *) reg [7:0] lane_ram[0:1023];
        wire restore_lane=save_write&&save_address[2:0]==lane;
        wire native_lane=master_ce&&!machine_reset&&cpu_write&&cpu_is_save&&cpu_addr[2:0]==lane;
        wire lane_write=clearing||restore_lane||native_lane;
        wire [9:0] lane_address=clearing?clear_address[12:3]:restore_lane?save_address[12:3]:cpu_addr[12:3];
        wire [7:0] lane_data=clearing?8'd0:restore_lane?save_data:cpu_wdata;
        assign save_word[lane*8+:8]=lane_ram[save_read_word];
        always @(posedge clk)begin
            if(lane_write)lane_ram[lane_address]<=lane_data;
        end
    end endgenerate
    always @(posedge clk) begin
        loader_response<=0;save_rvalid<=0;runtime_write_committed<=0;
        if(reset)begin
            cpu_valid<=0;ppu_valid<=0;outstanding<=0;discard<=0;owner<=0;pending_address<=0;choose_ppu<=0;
            clearing<=1;clear_address<=0;save_rdata<=0;runtime_write_address<=0;
        end else begin
            if(invalidate)begin cpu_valid<=0;ppu_valid<=0;clearing<=1;clear_address<=0;discard<=1;end
            if(clearing)begin
                if(clear_address<2048)begin cpu_ram[clear_address[10:0]]<=0;nt_ram[clear_address[10:0]]<=0;end
                chr_ram[clear_address]<=0;
                clear_address<=clear_address+1'b1;
                if(clear_address==8191)clearing<=0;
            end else begin
                if(master_ce&&!machine_reset)begin
                    if(cpu_write&&cpu_is_ram)cpu_ram[cpu_addr[10:0]]<=cpu_wdata;
                    if(cpu_write&&cpu_is_save)begin
                        runtime_write_committed<=1;
                        runtime_write_address<=24'h3c0000+{11'd0,cpu_addr[12:0]};
                    end
                    if(ppu_write&&ppu_is_nt)nt_ram[ppu_addr[10:0]]<=ppu_wdata;
                    if(ppu_write&&ppu_is_chr_ram)chr_ram[ppu_addr[12:0]]<=ppu_wdata;
                end
                if(save_read)begin
                    save_rdata<=save_word;
                    save_rvalid<=1;
                end
            end
            if(memory_valid&&memory_ready)begin
                outstanding<=1;discard<=0;pending_address<=memory_addr;
                owner<=loader_valid?0:select_ppu?2:1;
            end
            if(memory_rvalid&&outstanding)begin
                outstanding<=0;
                case(owner)
                0:loader_response<=1;
                1:begin
                    cpu_valid[pending_address[8:3]]<=!discard&&!invalidate;
                    cpu_tags[pending_address[8:3]]<=pending_address[23:9];
                    cpu_lines[pending_address[8:3]]<=memory_rdata;choose_ppu<=1;
                end
                2:begin
                    ppu_valid[pending_address[8:3]]<=!discard&&!invalidate;
                    ppu_tags[pending_address[8:3]]<=pending_address[23:9];
                    ppu_lines[pending_address[8:3]]<=memory_rdata;choose_ppu<=0;
                end
                default:;
                endcase
            end
        end
    end
endmodule
