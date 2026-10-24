// SPDX-License-Identifier: GPL-3.0-only
// Native-domain U64-compatible CPU policy. All write strobes are accepted
// CPU transactions; the adapter excludes DMA and unmapped VIC accesses.
module multitini_c64_turbo_regs #(
    parameter integer CORE_CLOCK_HZ=31527954,
    parameter integer RESET_WINDOW_CYCLES=(2*CORE_CLOCK_HZ)/32,
    parameter integer ISLAND_CYCLE=1
) (
    input wire clk_native,reset,config_valid,
    input wire [31:0] config_word,
    input wire legacy_turbo_request,native_tick,
    input wire cart_present,loader_active,pause_request,guard_active,
    input wire [1:0] guard_cause,
    input wire native_wr,
    input wire [9:0] native_addr,
    input wire [7:0] native_data,
    input wire fio_wr,
    input wire [9:0] fio_addr,
    input wire [7:0] fio_data,
    input wire [9:0] native_rd_addr,fast_rd_addr,
    input wire active_turbo,
    input wire [3:0] active_index,
    input wire active_c64u_top,
    input wire diag_deadline_miss,diag_ioonly_done,diag_nmi_used,
    output wire native_rd_override,fast_rd_override,
    output wire [7:0] native_rd_data,fast_rd_data,
    output wire policy_turbo,policy_blocked,policy_badline,policy_c64u_top,
    output wire [3:0] policy_index,
    output wire guard_request,guard_static_release,
    output wire [1:0] guard_holdoff,
    output wire [31:0] status,
    output reg [7:0] live_d031=0
);
    localparam integer WINDOW_W=$clog2(RESET_WINDOW_CYCLES+1);
    reg [WINDOW_W-1:0] reset_remaining=RESET_WINDOW_CYCLES;
    reg config_seen=0,window_written=0;
    reg [3:0] last_apply=0;
    reg [1:0] previous_mode=0;
    reg changed_hint=0,register_hint=0,c128_hint=0;
    reg [2:0] diagnostic=0;
    reg armed_previous=0;
    wire [1:0] mode=(config_word[10] || legacy_turbo_request) ? config_word[5:4] : 2'b00;
    wire [7:0] preference={!config_word[6],3'b000,config_word[3:0]};
    wire registers_live=mode[1];
    wire c128_live=mode==3;
    wire applying=config_valid && config_seen && config_word[15:12]!=last_apply;
    wire window_open=reset_remaining!=0;
    wire writing=fio_wr || native_wr;
    wire [9:0] write_addr=fio_wr ? fio_addr : native_addr;
    wire [7:0] write_data=fio_wr ? fio_data : native_data;
    reg software_update;
    reg [7:0] software_value;
    always @* begin
        software_update=0; software_value=live_d031;
        case(write_addr)
            10'h031: if(registers_live) begin software_update=writing;software_value=write_data&8'h8f;end
            10'h030: if(c128_live) begin software_update=writing;software_value=write_data[0]?preference:8'h00;end
            10'h07a: if(registers_live) begin software_update=writing;software_value=0;end
            10'h079,10'h07b: if(registers_live) begin software_update=writing;software_value=preference;end
            default: begin end
        endcase
    end
    function automatic [7:0] read_value(input [9:0] address);
        case(address)
            10'h031: read_value=registers_live ? live_d031 : 8'hff;
            10'h030: read_value=c128_live ? {7'h7f,(live_d031!=0)} : 8'hff;
            default: read_value=8'hff;
        endcase
    endfunction
    assign native_rd_override=native_rd_addr==10'h030 || native_rd_addr==10'h031;
    assign fast_rd_override=fast_rd_addr==10'h030 || fast_rd_addr==10'h031;
    assign native_rd_data=read_value(native_rd_addr);
    assign fast_rd_data=read_value(fast_rd_addr);
    wire [3:0] requested_index=mode==1 ? config_word[3:0] : live_d031[3:0];
    assign policy_index=(ISLAND_CYCLE==2 && requested_index>12) ? 4'd12 : requested_index;
    assign policy_badline=mode==1 ? config_word[6] : !live_d031[7];
    assign policy_c64u_top=config_word[7];
    assign guard_request=config_word[8];
    assign guard_holdoff=config_word[17:16];
    assign guard_static_release=config_word[11];
    wire disk_blocked=config_word[8] && !guard_active;
    assign policy_blocked=cart_present || loader_active || window_open || disk_blocked;
    assign policy_turbo=mode!=0 && (policy_index!=0 || !policy_badline) && !policy_blocked;
    assign status={last_apply,1'b0,(config_word[19]?diagnostic:3'b000),
        c128_hint,register_hint,changed_hint,pause_request,window_open,
        loader_active,disk_blocked,cart_present,live_d031,
        (disk_blocked?guard_cause:2'b00),active_c64u_top,active_turbo,
        (active_turbo?active_index:4'b0000)};
    always @(posedge clk_native) begin
        if(reset) begin
            reset_remaining<=RESET_WINDOW_CYCLES;
            live_d031<=0;window_written<=0;config_seen<=0;
            last_apply<=0;previous_mode<=0;
            changed_hint<=0;register_hint<=0;c128_hint<=0;
            diagnostic<=0;armed_previous<=0;
        end else begin
            armed_previous<=config_word[19];
            if(!config_word[19] || !armed_previous) diagnostic<=0;
            else diagnostic<=diagnostic | {diag_nmi_used,diag_ioonly_done,diag_deadline_miss};
            if(config_valid) begin
                config_seen<=1;last_apply<=config_word[15:12];previous_mode<=mode;
            end
            if(native_tick && !pause_request && window_open) begin
                reset_remaining<=reset_remaining-1'b1;
                if(reset_remaining==1 && !window_written && mode==2)
                    live_d031<=preference;
            end
            if(applying) begin
                window_written<=1;
                changed_hint<=0;register_hint<=0;c128_hint<=0;
                if(mode==2) live_d031<=preference;
                else if(mode==3) begin
                    if(previous_mode!=3) live_d031<=0;
                    else if(live_d031!=0) live_d031<=preference;
                end
            end else if(writing) begin
                if(!registers_live && ((write_addr==10'h031 && write_data[3:0]!=0) ||
                                      write_addr==10'h079 || write_addr==10'h07b)) register_hint<=1;
                if(!c128_live && write_addr==10'h030 && write_data[0]) c128_hint<=1;
                if(software_update) begin
                    live_d031<=software_value;window_written<=1;
                    if(software_value!=live_d031) changed_hint<=1;
                end
            end
        end
    end
endmodule
