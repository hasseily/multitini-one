// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module multitini_amiga_loader(
    input wire clk,reset,clear_error,load_abort,load_abort_epoch,load_epoch,
    input wire [7:0] load_kind,load_data,
    input wire load_valid,load_first,load_last,
    input wire [31:0] load_offset,load_total,
    output wire load_ready,output reg error,rom_loaded,config_valid,
    output reg [127:0] config_bytes,
    output reg write_valid,output reg [23:0] write_addr,
    output reg [7:0] write_data,input wire write_ready,write_done,
    input wire memory_fault
);
    reg active,epoch,waiting,last_q;
    reg [7:0] kind;
    reg [31:0] next_offset,total;
    reg [127:0] staging;
    wire first_ok=load_first && load_offset==0 &&
        ((load_kind==8'h60 && load_total==16) || (load_kind==8'h61 && load_total==32'h100000));
    wire continuation_ok=active && !load_first && load_epoch==epoch && load_kind==kind &&
        load_total==total && load_offset==next_offset;
    wire framing_ok=(first_ok || continuation_ok) && (load_last==(load_offset+1==load_total));
    wire [127:0] config_next=(staging & ~(128'hff << (load_offset[3:0]*8))) |
        (128'(load_data) << (load_offset[3:0]*8));
    assign load_ready=!waiting && !write_valid;
    always @(posedge clk)begin
        if(reset)begin
            active<=0;epoch<=0;waiting<=0;last_q<=0;kind<=0;next_offset<=0;total<=0;
            error<=0;rom_loaded<=0;config_valid<=0;config_bytes<=0;staging<=0;
            write_valid<=0;write_addr<=0;write_data<=0;
        end else begin
            if(clear_error)error<=0;
            if(memory_fault)begin error<=1;rom_loaded<=0;end
            if(write_valid && write_ready)begin write_valid<=0;waiting<=1;end
            if(waiting && write_done)begin
                waiting<=0;
                if(last_q && active && !memory_fault && !(load_abort && load_abort_epoch==epoch))begin rom_loaded<=1;active<=0;end
            end
            if(load_abort && active && load_abort_epoch==epoch)begin
                active<=0;error<=1;if(kind==8'h61)rom_loaded<=0;else config_valid<=0;
            end
            if(load_valid && load_ready)begin
                if(!framing_ok)begin error<=1;active<=0;if(load_kind==8'h61)rom_loaded<=0;end
                else begin
                    if(load_first)begin
                        active<=1;epoch<=load_epoch;kind<=load_kind;total<=load_total;
                        if(load_kind==8'h61)rom_loaded<=0;else begin config_valid<=0;staging<=0;end
                    end
                    next_offset<=load_offset+1;
                    if(load_kind==8'h61)begin
                        write_addr<=load_offset[19] ? 24'h780000+{5'd0,load_offset[18:0]} :
                                                                  24'h600000+{5'd0,load_offset[18:0]};
                        write_data<=load_data;write_valid<=1;last_q<=load_last;
                    end else begin
                        staging<=config_next;
                        if(load_last)begin
                            active<=0;
                            // AM01: CPU, standard, chip acceleration, disk speed,
                            // Fast MiB, Chip MiB. Reject unsupported options.
                            if(config_next[31:0]!=32'h31304d41 ||
                               (config_next[39:32]!=0 && config_next[39:32]!=2 && config_next[39:32]!=3) ||
                               config_next[47:40]!=0 || config_next[55:48]!=0 || config_next[63:56]>1 ||
                               (config_next[71:64]!=4 && config_next[71:64]!=8) ||
                               config_next[79:72]!=2 || config_next[87:80]>1 || config_next[127:88]!=0)begin error<=1;config_valid<=0;end
                            else begin config_bytes<=config_next;config_valid<=1;end
                        end
                    end
                end
            end
        end
    end
endmodule
