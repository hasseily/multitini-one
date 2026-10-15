// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// A complete, validated iNES1 stream commits only after the last PSRAM reply.
// Epoch cancellation drains stale bytes and any already accepted memory write.
module multitini_nes_loader(
    input wire clk,reset,core_reset_request,clear_error,
    input wire load_abort,load_abort_epoch,load_epoch,
    input wire [7:0] load_data,load_kind,
    input wire load_valid,load_first,load_last,
    input wire [31:0] load_offset,load_total,
    output wire load_ready,
    output reg error,cart_loaded,active,
    output reg [31:0] cart_size,
    output reg [63:0] mapper_flags,
    output reg [9:0] prg_mask,chr_mask,
    output reg invalidate,
    output reg [12:0] save_address,
    output reg [7:0] save_data,
    output reg save_write,
    output reg [23:0] write_addr,
    output reg [63:0] write_data,
    output reg [7:0] write_strobe,
    output reg write_valid,
    input wire write_ready,write_response,memory_fault
);
    import multitini_nes_defs_pkg::*;
    reg waiting,final_write,epoch_valid,active_epoch,aborted_valid,aborted_epoch;
    reg [7:0] kind;
    reg [31:0] expected,total,prg_bytes;
    reg [127:0] header;
    reg [63:0] packed_bytes;
    reg [127:0] next_header;
    reg [63:0] next_pack;
    wire stale=(aborted_valid && load_epoch==aborted_epoch)||
               (load_abort && load_epoch==load_abort_epoch);
    wire abort_target=load_abort&&(!epoch_valid||active_epoch==load_abort_epoch);
    wire take=load_valid&&load_ready;
    assign load_ready=!reset&&(stale||(!waiting&&!write_valid));
    wire first_ok=core_reset_request&&load_offset==0&&
        ((load_kind==MT_LOAD_NES_CART&&load_total>=16400&&load_total<=786448)||
         (load_kind==MT_LOAD_NES_SAVE&&cart_loaded&&mapper_flags[25]&&load_total==8192));
    wire sequence_ok=load_first ? ((!active||load_epoch!=active_epoch)&&first_ok) :
        (active&&load_epoch==active_epoch&&kind==load_kind&&load_total==total&&load_offset==expected);
    wire end_ok=load_offset<load_total&&load_last==(load_offset+1'b1==load_total);
    wire [31:0] payload_offset=load_offset-16;
    wire [23:0] payload_address=payload_offset<prg_bytes ? payload_offset[23:0] :
        MT_NES_CHR_BASE+payload_offset[23:0]-prg_bytes[23:0];
    function automatic [2:0] size_flag(input [7:0] banks);
        case(banks) 0,1:size_flag=0;2:size_flag=1;4:size_flag=2;8:size_flag=3;
            16:size_flag=4;32:size_flag=5;64:size_flag=6;default:size_flag=7;endcase
    endfunction
    always @* begin
        next_header=load_first?128'd0:header;
        if(load_offset<16)next_header[load_offset*8+:8]=load_data;
        next_pack=payload_address[2:0]==0?64'd0:packed_bytes;
        next_pack[payload_address[2:0]*8+:8]=load_data;
    end
    always @(posedge clk) begin
        save_write<=0;invalidate<=0;
        if(reset)begin
            error<=0;cart_loaded<=0;cart_size<=0;active<=0;mapper_flags<=0;prg_mask<=0;chr_mask<=0;
            waiting<=0;final_write<=0;epoch_valid<=0;active_epoch<=0;aborted_valid<=0;aborted_epoch<=0;
            kind<=0;expected<=0;total<=0;prg_bytes<=0;header<=0;packed_bytes<=0;
            write_addr<=0;write_data<=0;write_strobe<=0;write_valid<=0;save_address<=0;save_data<=0;
        end else begin
            if(clear_error)error<=0;
            if(memory_fault)begin error<=1;cart_loaded<=0;end
            if(write_valid&&write_ready)begin write_valid<=0;waiting<=1;end
            if(waiting&&write_response)begin
                waiting<=0;
                if(final_write&&active&&!abort_target)begin
                    active<=0;cart_loaded<=!error&&!memory_fault;cart_size<=total;
                end
            end
            if(abort_target)begin
                aborted_valid<=1;aborted_epoch<=load_abort_epoch;active<=0;cart_loaded<=0;invalidate<=1;
                final_write<=0;
                if(write_valid&&!write_ready)write_valid<=0;
            end
            if(take&&!stale)begin
                if(!sequence_ok||!end_ok||!core_reset_request)begin
                    error<=1;active<=0;cart_loaded<=0;invalidate<=1;
                end else begin
                    if(load_first)begin
                        active<=1;active_epoch<=load_epoch;epoch_valid<=1;aborted_valid<=0;
                        kind<=load_kind;total<=load_total;expected<=1;
                        if(load_kind==MT_LOAD_NES_CART)begin
                            cart_loaded<=0;cart_size<=0;invalidate<=1;mapper_flags<=0;
                            if(!memory_fault)error<=0;
                        end
                    end else expected<=expected+1'b1;
                    if(load_kind==MT_LOAD_NES_SAVE)begin
                        save_address<=load_offset[12:0];save_data<=load_data;save_write<=1;
                        if(load_last)active<=0;
                    end else if(load_offset<16)begin
                        header<=next_header;
                        if(load_offset==15)begin
                            if(!nes_header_ok(next_header,load_total))begin error<=1;active<=0;end
                            else begin
                                prg_bytes<={10'd0,next_header[39:32],14'd0};
                                prg_mask<=({2'd0,next_header[39:32]}<<3)-1'b1;
                                chr_mask<=next_header[47:40]==0 ? 10'd3 :
                                    ({2'd0,next_header[47:40]}<<2)-1'b1;
                                mapper_flags<=0;
                                mapper_flags[7:0]<={next_header[63:60],next_header[55:52]};
                                mapper_flags[10:8]<=size_flag(next_header[39:32]);
                                mapper_flags[13:11]<=size_flag(next_header[47:40]);
                                mapper_flags[14]<=next_header[48];
                                mapper_flags[15]<=next_header[47:40]==0;
                                // Canonical iNES byte10 bit5 preserves the
                                // NES2 mapper2/submapper2 AND bus conflicts.
                                mapper_flags[24:21]<=next_header[85]?4'd2:4'd0;
                                mapper_flags[25]<=next_header[49];
                                mapper_flags[29:26]<=next_header[49]?4'd0:4'd7;
                                mapper_flags[34:31]<=next_header[49]?4'd7:4'd0;
                            end
                        end
                    end else begin
                        packed_bytes<=next_pack;
                        if(payload_address[2:0]==7||load_last)begin
                            write_addr<={payload_address[23:3],3'd0};write_data<=next_pack;
                            write_strobe<=8'hff;write_valid<=1;final_write<=load_last;
                        end
                    end
                end
            end
        end
    end
endmodule
