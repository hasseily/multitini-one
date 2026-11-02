// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Read-only observer. Every time unit is an UNGATED service-clock period.
// Inputs are sampled into one bundle; no path returns to CPU, IDE or memory.
// Native REQ changes become observable one service edge after publication.
// The endpoint observation edge is excluded, correcting that one-tick bias.
module multitini_amiga_diagnostics #(
    parameter integer SERVICE_HZ=28375196
)(
    input wire clk,reset,guest_reset,chip_tick,
    input wire [15:0] native_config,
    input wire [2:0] native_request,
    input wire [5:0] ide_monitor, // {guest IO active,write words[1:0],read words[1:0],READ DRQ publish}
    input wire cpu_wait,cpu_write,
    input wire [7:0] memory_monitor, // {CPU DDR write/read issue,CHIP WAIT/SEND,CPU WAIT/SEND,CPU write/read request}
    input wire control_valid,input wire [7:0] control_action,
    input wire [15:0] control_sequence,
    output reg control_done,control_error,output reg [15:0] result_sequence,
    input wire [6:0] read_index,output wire [15:0] read_word,
    output wire frozen,output reg running
);
    localparam integer COUNTERS=28,WORDS=128;
    reg [63:0] totals[0:COUNTERS-1],scratch[0:COUNTERS-1];
    reg [15:0] bank[0:WORDS-1];
    reg [63:0] timebase,origin,stop_stamp;
    reg [31:0] epoch;
    reg [15:0] sequence_id,config_begin,config_end;
    reg [6:0] reasons;
    reg gap_open,bank_ready,stopping,freeze_pending;
    reg [63:0] stamp_q;
    reg reset_q,tick_q,cpu_wait_q,cpu_write_q;
    reg [15:0] config_q;
    reg [2:0] request_q,previous_request;
    reg [5:0] ide_q;
    reg [7:0] memory_q;
    assign read_word=bank_ready ? bank[read_index] : 16'd0;
    assign frozen=bank_ready;
    wire request_event=(request_q==3'd4 || request_q==3'd5) && request_q!=previous_request;
    wire close_event=gap_open && request_event;
    // Scratch indices5..20 and26..27 describe CLOSED gaps only. They can
    // overlap the exclusive buckets6..10, and must not be added to them.
    task automatic add_total(input integer n,input [63:0] amount);
        begin
            if(totals[n]>64'hffffffffffffffff-amount)begin totals[n]<=64'hffffffffffffffff;reasons[5]<=1;end
            else totals[n]<=totals[n]+amount;
        end
    endtask
    task automatic add_scratch(input integer n,input [63:0] amount);
        begin
            if(scratch[n]>64'hffffffffffffffff-amount)begin scratch[n]<=64'hffffffffffffffff;reasons[5]<=1;end
            else scratch[n]<=scratch[n]+amount;
        end
    endtask
    integer n,k;
    always @(posedge clk)begin
        control_done<=0;control_error<=0;
        timebase<=timebase+1'b1;
        stamp_q<=timebase;reset_q<=guest_reset;tick_q<=chip_tick;
        config_q<=native_config;request_q<=native_request;ide_q<=ide_monitor;
        cpu_wait_q<=cpu_wait;cpu_write_q<=cpu_write;memory_q<=memory_monitor;
        previous_request<=request_q;
        if(reset)begin
            timebase<=0;origin<=0;stop_stamp<=0;epoch<=0;sequence_id<=0;
            config_begin<=0;config_end<=0;reasons<=0;gap_open<=0;bank_ready<=0;
            running<=0;stopping<=0;freeze_pending<=0;result_sequence<=0;
            stamp_q<=0;reset_q<=1;tick_q<=0;config_q<=0;request_q<=0;
            ide_q<=0;cpu_wait_q<=0;cpu_write_q<=0;memory_q<=0;previous_request<=0;
            for(n=0;n<COUNTERS;n=n+1)begin totals[n]<=0;scratch[n]<=0;end
            for(n=0;n<WORDS;n=n+1)bank[n]<=0;
        end else begin
            if(running)begin
                add_total(0,1);add_total(tick_q ? 1:2,1);
                config_end<=config_q;
                if(reset_q || request_q==6)begin reasons[3]<=1;running<=0;gap_open<=0;end
                if(config_q!=config_begin)begin reasons[4]<=1;running<=0;gap_open<=0;end
                if(!(reset_q || request_q==6 || config_q!=config_begin))begin
                if(request_event)begin
                    add_total(request_q==4 ? 24:25,1);
                end
                // Native request assertion occurred on stamp_q-1. Its
                // observation edge is not part of the preceding interval.
                if(close_event)begin
                    for(n=5;n<=20;n=n+1)add_total(n,scratch[n]);
                    add_total(26,scratch[26]);add_total(27,scratch[27]);add_total(4,1);
                    if(scratch[5]>totals[21])totals[21]<=scratch[5];
                    totals[23]<=stamp_q-origin-1'b1;gap_open<=0;
                end else if(gap_open)begin
                    add_scratch(5,1);
                    if(!tick_q)add_scratch(6,1);
                    else if(cpu_wait_q && cpu_write_q)add_scratch(7,1);
                    else if(cpu_wait_q)add_scratch(8,1);
                    else if(ide_q[5])add_scratch(9,1);
                    else add_scratch(10,1);
                    if(tick_q)begin
                        add_scratch(11,{62'd0,ide_q[2:1]});add_scratch(12,{62'd0,ide_q[4:3]});
                    end
                    if(memory_q[0])add_scratch(13,1);
                    if(memory_q[1])add_scratch(14,1);
                    for(k=2;k<6;k=k+1)if(memory_q[k])add_scratch(13+k,1);
                    if(cpu_wait_q)add_scratch(cpu_write_q ? 20:19,1);
                    if(memory_q[6])add_scratch(26,1);
                    if(memory_q[7])add_scratch(27,1);
                end
                if(tick_q && ide_q[0])begin
                    if(gap_open && !close_event)begin reasons[6]<=1;running<=0;end
                    else begin
                        if(totals[3]==0)totals[22]<=stamp_q-origin;
                        add_total(3,1);gap_open<=1;
                        for(n=0;n<COUNTERS;n=n+1)scratch[n]<=0;
                    end
                end
                end
            end
            // Drain the one delayed REQ observation at STOP's boundary.
            // It can close a request published before STOP, without counting
            // any service/guest work at or after the stop edge.
            if(stopping)begin
                stopping<=0;freeze_pending<=1;
                if(close_event && stamp_q-1'b1<stop_stamp)begin
                    for(n=5;n<=20;n=n+1)add_total(n,scratch[n]);
                    add_total(26,scratch[26]);add_total(27,scratch[27]);add_total(4,1);
                    if(scratch[5]>totals[21])totals[21]<=scratch[5];
                    totals[23]<=stamp_q-origin-1'b1;gap_open<=0;
                    add_total(request_q==4 ? 24:25,1);
                end
            end
            if(freeze_pending)begin
                freeze_pending<=0;bank_ready<=1;control_done<=1;result_sequence<=sequence_id;
                bank[0]<=16'h4447;bank[1]<=1;bank[2]<=WORDS;bank[3]<=sequence_id;
                bank[4]<={9'd0,reasons[6:3],gap_open,(reasons[6:3]==0),1'b1};
                bank[5]<=config_begin;bank[6]<=config_end;bank[7]<=0;
                bank[8]<=16'(SERVICE_HZ);bank[9]<=16'(SERVICE_HZ>>16);
                bank[10]<=epoch[15:0];bank[11]<=epoch[31:16];
                bank[12]<=1;bank[13]<=1;bank[14]<=COUNTERS;bank[15]<=0;
                for(n=0;n<COUNTERS;n=n+1)for(k=0;k<4;k=k+1)bank[16+4*n+k]<=totals[n][16*k+:16];
                gap_open<=0;
            end
            if(control_valid)begin
                if(control_sequence==0 || stopping || freeze_pending ||
                   (control_action!=1 && control_action!=2) ||
                   (control_action==1 && running) ||
                   (control_action==2 && control_sequence!=sequence_id))begin
                    control_done<=1;control_error<=1;result_sequence<=control_sequence;
                end else if(control_action==1)begin
                    sequence_id<=control_sequence;epoch<=epoch+1'b1;origin<=timebase;
                    config_begin<=native_config;config_end<=native_config;
                    reasons<=0;gap_open<=0;bank_ready<=0;running<=1;
                    for(n=0;n<COUNTERS;n=n+1)begin totals[n]<=0;scratch[n]<=0;end
                    control_done<=1;result_sequence<=control_sequence;
                end else if(bank_ready)begin
                    control_done<=1;result_sequence<=sequence_id;
                end else begin
                    running<=0;stopping<=1;stop_stamp<=timebase;
                end
            end
        end
    end
endmodule
