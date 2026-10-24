// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Ultimate Audio extension. IO2 decode and native CPU transaction acceptance
// belong to the machine adapter. DDR transport remains a separate HP3 path.
module multitini_c64_ua #(
    parameter integer CORE_CLOCK_HZ=31527954,
    parameter integer UA_MIN_ISSUE_GAP=32,
    parameter integer UA_YIELD_TO_REU=0,
    parameter integer UA_POST_B_HOLD=16,
    parameter integer UA_PERIOD_OFFSET_HALF_TICKS=3
) (
    input wire clk, hard_reset, guest_reset, enable, paused, hold,
    input wire reg_write,
    input wire [7:0] reg_addr, reg_wdata,
    output wire [7:0] reg_rdata,
    input wire diagnostics_armed, audio_tick, board_clip,
    input wire [83:0] cache_pending_addr,
    input wire [3:0] cache_pending_valid,
    input wire [20:0] cache_inflight_write_addr,
    input wire cache_inflight_write_valid,
    input wire cache_write_done,
    input wire [20:0] cache_write_done_addr,
    input wire cache_write_snoop_valid,
    input wire [20:0] cache_write_snoop_addr,
    input wire reu_memory_busy,
    output wire irq,
    output reg held,
    output wire [31:0] status,
    output reg [31:0] fetch_count,
    output wire signed [17:0] audio_l, audio_r,
    output wire [23:0] memory_addr,
    output wire memory_valid, memory_busy,
    input wire memory_ready,
    input wire [63:0] memory_rdata,
    input wire memory_rvalid, memory_fault
);
    reg [7:0] control[0:6];
    reg [5:0] volume[0:6];
    reg [3:0] pan[0:6];
    reg [23:0] start_address[0:6], sample_length[0:6], repeat_a[0:6], repeat_b[0:6];
    reg [15:0] rate[0:6];
    reg [6:0] irq_flags;
    wire [6:0] irq_set, underrun, playing, fetch, sixteen_bit, predict_valid, sample_ready;
    wire [167:0] fetch_address, predict_address;
    wire [125:0] deadlines;
    wire [111:0] next_samples, samples;
    wire [41:0] volumes;
    wire [27:0] pans;
    wire accepted, deferred, fault, mix_clip;
    wire reset=hard_reset || guest_reset || !enable || hold;
    reg [25:0] half_accumulator;
    wire [26:0] half_sum={1'b0,half_accumulator}+27'd12500000;
    wire half_tick=half_sum>=CORE_CLOCK_HZ;
    reg was_armed, sticky_underrun, sticky_clip, sticky_defer;
    reg [15:0] underrun_count;
    reg [3:0] underruns_now;
    wire signed [17:0] mixed_l, mixed_r;
    assign reg_rdata=reg_addr[0] ? 8'h10 : {1'b0,irq_flags};
    assign irq=|irq_flags;
    assign status={diagnostics_armed ? underrun_count : 16'b0,1'b0,diagnostics_armed ? playing : 7'b0,
                   4'b0,diagnostics_armed && sticky_defer,diagnostics_armed && sticky_clip,
                   diagnostics_armed && sticky_underrun,fault};
    assign audio_l=(reset || fault) ? 18'sd0 : mixed_l;
    assign audio_r=(reset || fault) ? 18'sd0 : mixed_r;
    genvar g;
    generate for(g=0;g<7;g=g+1) begin: voices
        assign volumes[g*6+:6]=volume[g];
        assign pans[g*4+:4]=pan[g];
        multitini_c64_ua_voice #(.PERIOD_OFFSET_HALF_TICKS(UA_PERIOD_OFFSET_HALF_TICKS)) voice (
            .clk(clk),.reset(reset || fault),.paused(paused),.half_tick(half_tick),
            .control(control[g]),.start_address(start_address[g]),.sample_length(sample_length[g]),
            .repeat_a(repeat_a[g]),.repeat_b(repeat_b[g]),.rate(rate[g]),
            .next_sample(next_samples[g*16+:16]),.sample_ready(sample_ready[g]),
            .fetch(fetch[g]),.fetch_address(fetch_address[g*24+:24]),.predict_address(predict_address[g*24+:24]),
            .sixteen_bit(sixteen_bit[g]),.predict_valid(predict_valid[g]),.deadline(deadlines[g*18+:18]),
            .playing(playing[g]),.sample(samples[g*16+:16]),.irq_set(irq_set[g]),.underrun(underrun[g])
        );
    end endgenerate
    multitini_c64_ua_fetch #(.MIN_ISSUE_GAP(UA_MIN_ISSUE_GAP),.YIELD_TO_REU(UA_YIELD_TO_REU),.POST_B_HOLD(UA_POST_B_HOLD)) fetcher (
        .clk(clk),.hard_reset(hard_reset),.reset(reset),.paused(paused),
        .fetch(fetch),.playing(playing),.sixteen_bit(sixteen_bit),.predict_valid(predict_valid),
        .fetch_address(fetch_address),.predict_address(predict_address),.deadlines(deadlines),
        .samples(next_samples),.sample_ready(sample_ready),
        .cache_pending_addr(cache_pending_addr),.cache_pending_valid(cache_pending_valid),
        .cache_inflight_write_addr(cache_inflight_write_addr),.cache_inflight_write_valid(cache_inflight_write_valid),
        .cache_write_done(cache_write_done),.cache_write_done_addr(cache_write_done_addr),
        .cache_write_snoop_valid(cache_write_snoop_valid),.cache_write_snoop_addr(cache_write_snoop_addr),.reu_memory_busy(reu_memory_busy),
        .memory_addr(memory_addr),.memory_valid(memory_valid),.memory_ready(memory_ready),
        .memory_rdata(memory_rdata),.memory_rvalid(memory_rvalid),.memory_fault(memory_fault),
        .busy(memory_busy),.fault(fault),.accepted(accepted),.deferred(deferred)
    );
    multitini_c64_ua_mix mixer (
        .clk(clk),.reset(reset || fault),.audio_tick(audio_tick),.samples(samples),.volumes(volumes),.pans(pans),
        .audio_l(mixed_l),.audio_r(mixed_r),.clipped(mix_clip)
    );
    integer i, selected;
    reg [6:0] clear_flags;
    always @* begin
        clear_flags=0;selected=reg_addr[7:5]-1;
        if(reg_write && reg_addr[7:5]!=0 && reg_addr[4:0]==5'h1f) begin
            if(reg_wdata==8'hff) clear_flags=7'h7f;
            else if(reg_wdata[0]) clear_flags[selected]=1;
        end
        underruns_now=0;
        for(integer n=0;n<7;n=n+1) underruns_now=underruns_now+underrun[n];
    end
    always @(posedge clk) begin
        held<=hold;
        if(reset) begin
            half_accumulator<=0;irq_flags<=0;
            for(i=0;i<7;i=i+1) begin
                control[i]<=0;volume[i]<=0;pan[i]<=8;start_address[i]<=0;sample_length[i]<=0;
                repeat_a[i]<=0;repeat_b[i]<=0;rate[i]<=0;
            end
        end else begin
            if(!paused) half_accumulator<=half_tick ? half_sum-CORE_CLOCK_HZ : half_sum;
            irq_flags<=(irq_flags & ~clear_flags) | irq_set;
            if(reg_write && reg_addr[7:5]!=0) begin
                case(reg_addr[4:0])
                    5'h00:control[selected]<=reg_wdata & 8'h77;
                    5'h01:volume[selected]<=reg_wdata[5:0];
                    5'h02:pan[selected]<=reg_wdata[3:0];
                    5'h05:start_address[selected][23:16]<=reg_wdata;
                    5'h06:start_address[selected][15:8]<=reg_wdata;
                    5'h07:start_address[selected][7:0]<=reg_wdata;
                    5'h09:sample_length[selected][23:16]<=reg_wdata;
                    5'h0a:sample_length[selected][15:8]<=reg_wdata;
                    5'h0b:sample_length[selected][7:0]<=reg_wdata;
                    5'h0e:rate[selected][15:8]<=reg_wdata;
                    5'h0f:rate[selected][7:0]<=reg_wdata;
                    5'h11:repeat_a[selected][23:16]<=reg_wdata;
                    5'h12:repeat_a[selected][15:8]<=reg_wdata;
                    5'h13:repeat_a[selected][7:0]<=reg_wdata;
                    5'h15:repeat_b[selected][23:16]<=reg_wdata;
                    5'h16:repeat_b[selected][15:8]<=reg_wdata;
                    5'h17:repeat_b[selected][7:0]<=reg_wdata;
                    default:begin end
                endcase
            end
        end
        // Diagnostic history survives guest reset/disable/hold, but not a new
        // arm interval. Fault lifetime belongs to the transport and fetcher.
        if(hard_reset) begin
            was_armed<=0;sticky_underrun<=0;sticky_clip<=0;sticky_defer<=0;underrun_count<=0;fetch_count<=0;
        end else begin
            was_armed<=diagnostics_armed;
            if(!diagnostics_armed || !was_armed) begin
                sticky_underrun<=0;sticky_clip<=0;sticky_defer<=0;underrun_count<=0;fetch_count<=0;
            end else begin
                if(accepted) fetch_count<=fetch_count+1'b1;
                if(|underrun) begin
                    sticky_underrun<=1;
                    if({1'b0,underrun_count}+underruns_now>17'hffff) underrun_count<=16'hffff;
                    else underrun_count<=underrun_count+underruns_now;
                end
                if(mix_clip || board_clip) sticky_clip<=1;
                if(deferred) sticky_defer<=1;
            end
        end
    end
endmodule
