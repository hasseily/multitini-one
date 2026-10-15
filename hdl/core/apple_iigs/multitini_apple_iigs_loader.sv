// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Bounded, generation-tagged runtime ROM/config/sector loader.
module multitini_apple_iigs_loader(
    input wire clk,reset,core_reset_request,clear_error,
    input wire byte_tick,
    input wire load_abort,load_abort_epoch,load_epoch,
    input wire [7:0] load_kind,load_data,
    input wire load_valid,load_first,load_last,
    input wire [31:0] load_offset,load_total,
    output wire load_ready,output reg error,
    output wire rom_loaded,output reg config_valid,
    output reg [127:0] config_bytes,output reg config_commit,
    output reg aux_write,output reg [15:0] aux_addr,output reg [7:0] aux_data,
    output reg sector_write,output reg [8:0] sector_addr,output reg [7:0] sector_data,
    output reg nv_write,output reg [8:0] nv_addr,output reg [7:0] nv_data,
    output reg invalidate,
    output reg write_valid,output wire write_enable,
    output reg [23:0] write_addr,output reg [63:0] write_data,
    output reg [7:0] write_strobe,input wire write_ready,
    input wire write_response,memory_fault
);
    reg [5:0] loaded;
    reg active,epoch,aborted,abort_epoch,waiting,final_write;
    reg [7:0] kind;
    reg [31:0] expected,total;
    reg [63:0] pack;
    reg [127:0] staging_config;
    wire stale=(aborted && load_epoch==abort_epoch) || (load_abort && load_epoch==load_abort_epoch);
    wire is_rom=load_kind==8'h40 || load_kind==8'h41;
    wire aux=load_kind==8'h42 || load_kind==8'h44 || load_kind==8'h45 || load_kind==8'h46;
    wire protected_kind=is_rom || aux;
    wire first_ok=load_offset==0 && (!protected_kind || core_reset_request) &&
        ((load_kind==8'h40 && load_total==262144) || (load_kind==8'h41 && load_total==131072) ||
         (load_kind==8'h42 && load_total==4096) || (load_kind==8'h43 && load_total==16) ||
         ((load_kind==8'h44 || load_kind==8'h46) && load_total==4096) ||
         (load_kind==8'h45 && load_total==256) ||
         ((load_kind==8'h47 || load_kind==8'h48) && load_total==512));
    wire sequence_ok=load_first ? ((!active || load_epoch!=epoch) && first_ok) :
        (active && load_epoch==epoch && load_kind==kind && load_total==total && load_offset==expected);
    wire end_ok=load_offset<load_total && load_last==(load_offset+1'b1==load_total);
    assign load_ready=byte_tick && !write_valid && !waiting;
    assign write_enable=1;
    assign rom_loaded=config_valid && loaded[2] && loaded[4] &&
        (config_bytes[0] ? (loaded[0] && loaded[3]) : (loaded[1] && loaded[5]));
    reg [63:0] pack_next;
    reg [127:0] config_next;
    always @* begin
        pack_next=load_offset[2:0]==0 ? 64'd0 : pack;
        pack_next[load_offset[2:0]*8+:8]=load_data;
        config_next=load_first ? 128'd0 : staging_config;
        if(load_offset<16)config_next[load_offset[3:0]*8+:8]=load_data;
    end
    always @(posedge clk)begin
        invalidate<=0;config_commit<=0;
        // Pulses are held for a complete CLK_14M period. Both fast ROM load
        // ports and the native sector/PRAM ports observe stable addresses.
        if(byte_tick)begin aux_write<=0;sector_write<=0;nv_write<=0;end
        if(reset)begin
            loaded<=0;config_valid<=0;config_commit<=0;config_bytes<=128'h00000000000000000000000000000001;
            staging_config<=0;error<=0;active<=0;epoch<=0;aborted<=0;abort_epoch<=0;
            waiting<=0;final_write<=0;kind<=0;expected<=0;total<=0;pack<=0;
            write_valid<=0;write_addr<=0;write_data<=0;write_strobe<=0;
            aux_write<=0;aux_addr<=0;aux_data<=0;sector_write<=0;sector_addr<=0;sector_data<=0;
            nv_write<=0;nv_addr<=0;nv_data<=0;invalidate<=1;
        end else begin
            if(clear_error)error<=0;
            if(memory_fault)begin error<=1;loaded<=0;end
            if(write_valid && write_ready)begin write_valid<=0;waiting<=1;end
            if(waiting && write_response)begin
                waiting<=0;
                if(final_write && active && !(load_abort && load_abort_epoch==epoch))begin
                    active<=0;
                    if(!error && !memory_fault)loaded[kind==8'h40 ? 0 : 1]<=1;
                end
            end
            if(load_abort && active && epoch==load_abort_epoch)begin
                active<=0;aborted<=1;abort_epoch<=load_abort_epoch;loaded<=0;invalidate<=1;
                if(write_valid && !write_ready)write_valid<=0;
            end
            if(load_valid && load_ready && !stale)begin
                if(!sequence_ok || !end_ok)begin error<=1;active<=0;end
                else begin
                    if(load_first)begin active<=1;epoch<=load_epoch;aborted<=0;kind<=load_kind;total<=load_total;expected<=1;
                        if(is_rom)begin loaded[load_kind==8'h40 ? 0 : 1]<=0;invalidate<=1;end
                    end else expected<=expected+1'b1;
                    if(is_rom)begin
                        pack<=pack_next;
                        if(load_offset[2:0]==7)begin
                            write_valid<=1;write_addr<=(load_kind==8'h40 ? 24'hfc0000 : 24'hf80000)|load_offset[23:0]&24'hfffff8;
                            write_data<=pack_next;write_strobe<=8'hff;final_write<=load_last;
                        end
                    end else if(aux)begin
                        aux_write<=1;aux_data<=load_data;
                        case(load_kind)
                            8'h42:aux_addr<={4'h0,load_offset[11:0]};
                            8'h44:aux_addr<={4'h1,load_offset[11:0]};
                            8'h45:aux_addr<={8'h20,load_offset[7:0]};
                            default:aux_addr<={4'h3,load_offset[11:0]};
                        endcase
                        if(load_last)begin
                            active<=0;
                            case(load_kind)
                                8'h42:loaded[2]<=1;8'h44:loaded[3]<=1;8'h45:loaded[4]<=1;8'h46:loaded[5]<=1;
                            endcase
                        end
                    end else if(load_kind==8'h43)begin
                        staging_config<=config_next;
                        if(load_last)begin
                            active<=0;
                            if(config_next[7:0]>1 || config_next[15:8]>6 ||
                                (config_next[23:16]!=0 && config_next[23:16]!=2) ||
                                config_next[127:72]!=0 || config_next[71:69]!=0)error<=1;
                            else begin config_bytes<=config_next;config_valid<=1;config_commit<=1;end
                        end
                    end else if(load_kind==8'h47)begin
                        nv_write<=1;nv_addr<=load_offset[8:0];nv_data<=load_data;
                        if(load_last)active<=0;
                    end else begin
                        sector_write<=1;sector_addr<=load_offset[8:0];sector_data<=load_data;
                        if(load_last)active<=0;
                    end
                end
            end
        end
    end
endmodule
