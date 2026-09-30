// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Strict byte-stream transaction. ROM writes finish in PSRAM before CART is
// committed. Configuration and SRAM staging require the machine held reset.
module multitini_genesis_loader #(parameter bit PAL_MODE=0)(
    input wire clk,reset,core_reset_request,clear_error,
    input wire load_abort,load_abort_epoch,load_epoch,
    input wire [7:0] load_data,load_kind,
    input wire load_valid,load_first,load_last,
    input wire [31:0] load_offset,load_total,
    output wire load_ready,
    output reg error,cart_loaded,config_valid,
    output reg [31:0] cart_size,
    output reg [7:0] region,
    output reg [1:0] six_button,lowpass,
    output reg fm_chip,
    output reg cart_download,cart_word_write,
    output reg [22:0] cart_word_address,output reg [15:0] cart_word_data,
    output reg [15:0] save_address,output reg [7:0] save_data,output reg save_write,
    output reg cache_invalidate,
    output reg [23:0] write_addr,output reg [63:0] write_data,
    output reg [7:0] write_strobe,output reg write_valid,
    input wire write_ready,write_response,memory_fault
);
    import multitini_genesis_defs_pkg::*;
    reg active,waiting,final_write;
    reg active_epoch,epoch_valid,aborted_epoch,aborted_valid;
    reg [7:0] kind,even_byte;
    reg [31:0] expected,total;
    reg [127:0] config_bytes;
    reg [63:0] packed_bytes;
    reg [7:0] packed_strobe;
    wire take=load_valid && load_ready;
    // Abort and payload cross through independent mailboxes. Drain a stale
    // queued byte even while an already accepted memory write is completing.
    wire stale_byte=(aborted_valid && load_epoch==aborted_epoch) ||
        (load_abort && load_epoch==load_abort_epoch);
    wire abort_targets=load_abort && (!epoch_valid || active_epoch==load_abort_epoch);
    assign load_ready=!reset && (stale_byte || (!waiting && !write_valid));
    wire first_ok=load_offset==0 && core_reset_request &&
        ((load_kind==MT_LOAD_GENESIS_CONFIG && load_total==MT_SIZE_GENESIS_CONFIG) ||
         (load_kind==MT_LOAD_GENESIS_CART && config_valid && load_total>=MT_SIZE_GENESIS_CART_MIN &&
          load_total<=MT_SIZE_GENESIS_CART_MAX && !load_total[0]) ||
         (load_kind==MT_LOAD_GENESIS_SAVE && cart_loaded && load_total==MT_SIZE_GENESIS_SAVE));
    wire sequence_ok=load_first ? ((!active || load_epoch!=active_epoch) && first_ok) :
        (active && load_epoch==active_epoch && load_kind==kind && load_total==total && load_offset==expected);
    wire end_ok=load_offset<load_total && load_last==(load_offset+1'b1==load_total);
    reg [127:0] config_next;
    reg [63:0] pack_next;
    reg [7:0] strobe_next;
    always @* begin
        config_next=load_first ? 128'd0 : config_bytes;
        if(load_offset<16)config_next[load_offset*8+:8]=load_data;
        pack_next=load_offset[2:0]==0 ? 64'hffffffffffffffff : packed_bytes;
        strobe_next=load_offset[2:0]==0 ? 8'd0 : packed_strobe;
        pack_next[load_offset[2:0]*8+:8]=load_data;
        strobe_next[load_offset[2:0]]=1;
    end
    wire config_ok=config_next[31:0]==MT_GENESIS_CONFIG_MAGIC &&
        config_next[39:32]>=MT_GENESIS_REGION_US && config_next[39:32]<=MT_GENESIS_REGION_JP &&
        ((config_next[39:32]==MT_GENESIS_REGION_EU)==PAL_MODE) &&
        config_next[47:40]<=3 && config_next[55:48]<=1 && config_next[63:56]<=3 && config_next[127:64]==0;
    always @(posedge clk)begin
        cart_word_write<=0;save_write<=0;cache_invalidate<=0;
        if(reset)begin
            error<=0;cart_loaded<=0;config_valid<=0;cart_size<=0;
            region<=PAL_MODE ? MT_GENESIS_REGION_EU : MT_GENESIS_REGION_US;
            six_button<=3;fm_chip<=0;lowpass<=1;
            cart_download<=0;cart_word_address<=0;cart_word_data<=0;
            save_address<=0;save_data<=0;cache_invalidate<=1;
            write_addr<=0;write_data<=0;write_strobe<=0;write_valid<=0;
            active<=0;waiting<=0;final_write<=0;kind<=0;expected<=0;total<=0;
            active_epoch<=0;epoch_valid<=0;aborted_epoch<=0;aborted_valid<=0;
            config_bytes<=0;packed_bytes<=0;packed_strobe<=0;even_byte<=0;
        end else begin
            if(clear_error)error<=0;
            if(memory_fault)begin error<=1;cart_loaded<=0;end
            if(write_valid && write_ready)begin write_valid<=0;waiting<=1;end
            if(waiting && write_response)begin
                waiting<=0;
                if(final_write && active && !abort_targets)begin
                    active<=0;cart_download<=0;
                    cart_loaded<=!error && !memory_fault;cart_size<=total;
                end
            end
            if(abort_targets)begin
                aborted_epoch<=load_abort_epoch;aborted_valid<=1;
                active<=0;cart_loaded<=0;config_valid<=0;cart_download<=0;
                final_write<=0;cache_invalidate<=1;
                // Only a request not yet accepted by the cache may be
                // canceled. A simultaneous handshake keeps waiting asserted.
                if(write_valid && !write_ready)write_valid<=0;
            end
            if(take && !stale_byte)begin
                if(!sequence_ok || !end_ok || !core_reset_request)begin
                    error<=1;active<=0;cart_loaded<=0;cart_download<=0;config_valid<=0;cache_invalidate<=1;
                end else begin
                    if(load_first)begin
                        active<=1;active_epoch<=load_epoch;epoch_valid<=1;aborted_valid<=0;
                        kind<=load_kind;total<=load_total;expected<=1;
                        if(load_kind==MT_LOAD_GENESIS_CONFIG)begin
                            config_valid<=0;cart_loaded<=0;
                            if(!memory_fault)error<=0;
                        end
                        if(load_kind==MT_LOAD_GENESIS_CART)begin
                            cart_download<=1;cart_loaded<=0;cart_size<=0;cache_invalidate<=1;
                        end
                    end else expected<=expected+1'b1;
                    case(load_kind)
                    MT_LOAD_GENESIS_CONFIG:begin
                        config_bytes<=config_next;
                        if(load_last)begin
                            active<=0;config_valid<=config_ok;
                            if(!config_ok)error<=1;
                            else begin region<=config_next[39:32];six_button<=config_next[41:40];
                                fm_chip<=config_next[48];lowpass<=config_next[57:56];end
                        end
                    end
                    MT_LOAD_GENESIS_CART:begin
                        packed_bytes<=pack_next;packed_strobe<=strobe_next;
                        if(!load_offset[0])even_byte<=load_data;
                        else begin
                            cart_word_write<=1;cart_word_address<=load_offset[23:1];
                            cart_word_data<={even_byte,load_data};
                        end
                        if(load_offset[2:0]==7 || load_last)begin
                            write_addr<={load_offset[23:3],3'd0};write_data<=pack_next;
                            write_strobe<=strobe_next;write_valid<=1;final_write<=load_last;
                        end
                    end
                    MT_LOAD_GENESIS_SAVE:begin
                        save_address<=load_offset[15:0];save_data<=load_data;save_write<=1;
                        if(load_last)active<=0;
                    end
                    default:begin error<=1;active<=0;cart_loaded<=0;end
                    endcase
                end
            end
        end
    end
endmodule
