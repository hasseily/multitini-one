// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Seven two-word buffers. One outstanding read; registered REU snoops and
// all posted/in-flight/recent writes fence a matching physical DDR word.
module multitini_c64_ua_fetch #(
    parameter integer MIN_ISSUE_GAP=32,
    parameter integer YIELD_TO_REU=0,
    parameter integer POST_B_HOLD=16
) (
    input wire clk, hard_reset, reset, paused,
    input wire [6:0] fetch, playing, sixteen_bit, predict_valid,
    input wire [167:0] fetch_address, predict_address,
    input wire [125:0] deadlines,
    output reg [111:0] samples,
    output reg [6:0] sample_ready,
    input wire [83:0] cache_pending_addr,
    input wire [3:0] cache_pending_valid,
    input wire [20:0] cache_inflight_write_addr,
    input wire cache_inflight_write_valid,
    input wire cache_write_done,
    input wire [20:0] cache_write_done_addr,
    input wire cache_write_snoop_valid,
    input wire [20:0] cache_write_snoop_addr,
    input wire reu_memory_busy,
    output wire [23:0] memory_addr,
    output wire memory_valid,
    input wire memory_ready,
    input wire [63:0] memory_rdata,
    input wire memory_rvalid, memory_fault,
    output wire busy,
    output reg fault,
    output wire accepted,
    output reg deferred
);
    localparam integer HOLD_SLOTS=POST_B_HOLD>0 ? POST_B_HOLD : 1;
    (* ram_style="distributed" *) reg [63:0] word_data[0:13];
    reg [20:0] word_tag[0:13];
    reg [13:0] word_valid;
    reg [23:0] demand_addr[0:6];
    reg [6:0] demand_sixteen;
    reg [111:0] samples_q;
    reg [6:0] sample_ready_q;
    reg [167:0] prediction_q;
    reg [6:0] prediction_valid_q, prediction_wide_q;
    reg [20:0] recent_tag[0:HOLD_SLOTS-1];
    reg [HOLD_SLOTS-1:0] recent_valid;
    reg snoop_valid;
    reg [20:0] snoop_tag;
    reg owed, owner_valid, owner_stale;
    reg [3:0] owner_entry;
    reg [20:0] owner_tag;
    reg [23:0] request_addr_q;
    reg request_valid_q;
    reg chosen;
    reg [20:0] chosen_tag;
    reg [3:0] chosen_entry;
    reg [2:0] chosen_voice;
    integer gap;
    assign accepted=memory_valid && memory_ready;
    assign busy=memory_valid || owed;
    // The DDR path becomes ready on the response edge. Issue the next selected
    // word on that edge, retiring the old owner before installing the new one.
    // If transport is backpressured, retain the offered payload as usual.
    wire turn_request=owed && memory_rvalid && !request_valid_q && !reset && !paused &&
        !fault && !memory_fault && gap==0 && chosen && !fetch[chosen_voice] &&
        (!YIELD_TO_REU || !reu_memory_busy);
    assign memory_valid=request_valid_q || turn_request;
    assign memory_addr=turn_request ? {chosen_tag,3'b0} : request_addr_q;

    function automatic blocked(input [20:0] tag);
        integer n;
        begin
            blocked=(cache_inflight_write_valid && cache_inflight_write_addr==tag) ||
                    (snoop_valid && snoop_tag==tag);
            for(n=0;n<4;n=n+1)
                if(cache_pending_valid[n] && cache_pending_addr[n*21+:21]==tag) blocked=1;
            for(n=0;n<POST_B_HOLD;n=n+1)
                if(recent_valid[n] && recent_tag[n]==tag) blocked=1;
            // The first guard cycle is the write response edge itself.
            if(POST_B_HOLD>0 && cache_write_done && cache_write_done_addr==tag) blocked=1;
        end
    endfunction
    function automatic resident(input integer voice, input [20:0] tag);
        begin
            resident=((word_valid[2*voice] && word_tag[2*voice]==tag) ||
                      (word_valid[2*voice+1] && word_tag[2*voice+1]==tag)) &&
                     !(snoop_valid && snoop_tag==tag);
        end
    endfunction
    function automatic [63:0] word_value(input integer voice, input [20:0] tag);
        begin
            word_value=(word_valid[2*voice] && word_tag[2*voice]==tag) ? word_data[2*voice] : word_data[2*voice+1];
        end
    endfunction
    function automatic reserved(input integer voice, input [20:0] tag);
        begin reserved=owed && owner_valid && !owner_stale && owner_entry[3:1]==voice && owner_tag==tag;end
    endfunction
    wire return_fresh=memory_rvalid && owed && owner_valid && !owner_stale &&
        !(snoop_valid && snoop_tag==owner_tag) && !reset && !fault && !memory_fault;
    function automatic available(input integer voice, input [20:0] tag);
        begin available=resident(voice,tag) ||
            (return_fresh && owner_entry[3:1]==voice && owner_tag==tag);end
    endfunction
    function automatic [63:0] available_word(input integer voice, input [20:0] tag);
        begin available_word=(return_fresh && owner_entry[3:1]==voice && owner_tag==tag) ?
            memory_rdata : word_value(voice,tag);end
    endfunction

    integer r;
    reg [23:0] return_addr;
    reg [20:0] return_tag;
    reg [63:0] return_first,return_second;
    reg [7:0] return_low,return_high;
    // A fresh response present at the sample boundary can be consumed on
    // that edge. Only the previously latched demand is exposed here: fetch
    // may already describe the following sample at this same boundary.
    always_comb begin
        samples=samples_q;sample_ready=sample_ready_q;
        return_addr=0;return_tag=0;return_first=0;return_second=0;return_low=0;return_high=0;
        for(r=0;r<7;r=r+1)begin
            return_addr=demand_addr[r];return_tag=return_addr[23:3];
            if(return_fresh && playing[r] && !sample_ready_q[r] && !paused &&
               available(r,return_tag) && (!demand_sixteen[r] || return_addr[2:0]!=7 || available(r,return_tag+21'd1)))begin
                return_first=available_word(r,return_tag);return_second=available_word(r,return_tag+21'd1);
                return_low=return_first[return_addr[2:0]*8+:8];
                return_high=return_addr[2:0]==7 ? return_second[7:0] : return_first[(return_addr[2:0]+1)*8+:8];
                samples[r*16+:16]=demand_sixteen[r] ? {return_high,return_low} : {return_low,8'b0};
                sample_ready[r]=1;
            end
        end
    end

    reg [48:0] candidates[0:7], first_pair[0:3], second_pair[0:1], winner;
    reg [23:0] address, prediction;
    reg wide, straddle, need, candidate, pinned0, pinned1;
    reg [20:0] tag0, tag1, candidate_tag;
    reg [1:0] candidate_class;
    reg [17:0] remaining;
    integer v, target;
    // A demand is the current sample, prediction the next logical address,
    // and look-ahead the following word. Class precedes earliest deadline.
    always_comb begin
        chosen=0;chosen_tag=0;chosen_entry=0;chosen_voice=0;deferred=0;
        address=0;prediction=0;wide=0;straddle=0;need=0;candidate=0;
        pinned0=0;pinned1=0;tag0=0;tag1=0;candidate_tag=0;candidate_class=0;remaining=0;target=0;
        for(v=0;v<7;v=v+1) begin
            // Issue selection uses registered logical demand/prediction. The
            // resident-hit path below still observes the exact fetch edge.
            address=demand_addr[v];wide=demand_sixteen[v];
            tag0=address[23:3];tag1=tag0+21'd1;
            straddle=wide && address[2:0]==7;
            need=playing[v] && !sample_ready_q[v];
            candidate=0;candidate_class=0;candidate_tag=tag0;
            pinned0=need && word_valid[2*v] && (word_tag[2*v]==tag0 || (straddle && word_tag[2*v]==tag1));
            pinned1=need && word_valid[2*v+1] && (word_tag[2*v+1]==tag0 || (straddle && word_tag[2*v+1]==tag1));
            if(need && !resident(v,tag0) && !reserved(v,tag0)) candidate=1;
            else if(need && straddle && !resident(v,tag1) && !reserved(v,tag1)) begin candidate=1;candidate_tag=tag1;end
            else if(playing[v] && prediction_valid_q[v]) begin
                prediction=prediction_q[v*24+:24];
                candidate_tag=prediction[23:3]; candidate_class=1;
                if(!resident(v,candidate_tag) && !reserved(v,candidate_tag)) candidate=1;
                else if(prediction_wide_q[v] && prediction[2:0]==7 && !resident(v,candidate_tag+21'd1) && !reserved(v,candidate_tag+21'd1)) begin
                    candidate=1;candidate_tag=candidate_tag+21'd1;
                end else if(!resident(v,candidate_tag+21'd1) && !reserved(v,candidate_tag+21'd1)) begin
                    candidate=1;candidate_tag=candidate_tag+21'd1;candidate_class=2;
                end
            end
            pinned0=pinned0 || (owed && owner_valid && owner_entry==2*v);
            pinned1=pinned1 || (owed && owner_valid && owner_entry==2*v+1);
            if(candidate_class==2 && prediction_valid_q[v]) begin
                // Look-ahead must retain the resident predicted word. Without
                // this pin a full buffer could alternate two tags forever.
                prediction=prediction_q[v*24+:24];
                pinned0=pinned0 || (word_valid[2*v] && word_tag[2*v]==prediction[23:3]);
                pinned1=pinned1 || (word_valid[2*v+1] && word_tag[2*v+1]==prediction[23:3]);
            end
            // Keep any current-demand word until its sample has been latched.
            if(!word_valid[2*v] && !pinned0) target=2*v;
            else if(!word_valid[2*v+1] && !pinned1) target=2*v+1;
            else if(!pinned0) target=2*v;
            else if(!pinned1) target=2*v+1;
            else candidate=0;
            remaining=deadlines[v*18+:18];
            candidates[v]={49{1'b1}};
            if(candidate) begin
                if(blocked(candidate_tag)) deferred=1;
                else candidates[v]={1'b0,candidate_class,remaining,v[2:0],candidate_tag,target[3:0]};
            end
        end
        // A balanced tournament bounds deadline arbitration to three
        // comparisons rather than a seven-voice priority carry chain.
        candidates[7]={49{1'b1}};
        for(v=0;v<4;v=v+1)
            first_pair[v]=candidates[2*v][48:25]<=candidates[2*v+1][48:25] ? candidates[2*v] : candidates[2*v+1];
        for(v=0;v<2;v=v+1)
            second_pair[v]=first_pair[2*v][48:25]<=first_pair[2*v+1][48:25] ? first_pair[2*v] : first_pair[2*v+1];
        winner=second_pair[0][48:25]<=second_pair[1][48:25] ? second_pair[0] : second_pair[1];
        chosen=!winner[48];chosen_voice=winner[27:25];chosen_tag=winner[24:4];chosen_entry=winner[3:0];
    end

    integer i;
    reg [23:0] sample_addr;
    reg sample_wide;
    reg [20:0] sample_tag;
    reg [63:0] first_word, second_word;
    reg [7:0] low_byte, high_byte;
    always @(posedge clk) begin
        if(hard_reset) begin
            word_valid<=0;sample_ready_q<=0;samples_q<=0;demand_sixteen<=0;
            prediction_q<=0;prediction_valid_q<=0;prediction_wide_q<=0;
            recent_valid<=0;snoop_valid<=0;snoop_tag<=0;owed<=0;owner_valid<=0;owner_stale<=0;
            owner_entry<=0;owner_tag<=0;request_addr_q<=0;request_valid_q<=0;gap<=0;fault<=0;
            for(i=0;i<7;i=i+1) demand_addr[i]<=0;
            for(i=0;i<14;i=i+1) begin word_tag[i]<=0;word_data[i]<=0;end
            for(i=0;i<HOLD_SLOTS;i=i+1) recent_tag[i]<=0;
        end else begin
            snoop_valid<=cache_write_snoop_valid;snoop_tag<=cache_write_snoop_addr;
            recent_valid[0]<=POST_B_HOLD>0 && cache_write_done;
            recent_tag[0]<=cache_write_done_addr;
            for(i=1;i<HOLD_SLOTS;i=i+1) begin
                recent_valid[i]<=recent_valid[i-1];recent_tag[i]<=recent_tag[i-1];
            end
            if(gap>0) gap<=gap-1;
            if(memory_fault) fault<=1;
            if(snoop_valid) begin
                for(i=0;i<14;i=i+1) if(word_tag[i]==snoop_tag) word_valid[i]<=0;
                if(owner_tag==snoop_tag && owner_valid) owner_stale<=1;
            end
            if(accepted) begin
                request_valid_q<=0;owed<=1;gap<=MIN_ISSUE_GAP>0 ? MIN_ISSUE_GAP-1 : 0;
            end
            if(memory_rvalid && (owed || accepted)) begin
                owed<=0;
                if(owner_valid && !owner_stale && !(snoop_valid && snoop_tag==owner_tag) &&
                   !reset && !fault && !memory_fault) begin
                    word_data[owner_entry]<=memory_rdata;word_tag[owner_entry]<=owner_tag;word_valid[owner_entry]<=1;
                end
                owner_valid<=0;
            end
            if(turn_request) begin
                request_addr_q<={chosen_tag,3'b0};request_valid_q<=!memory_ready;owed<=memory_ready;
                owner_valid<=1;owner_stale<=0;owner_tag<=chosen_tag;owner_entry<=chosen_entry;
                word_valid[chosen_entry]<=0;
            end
            if(reset || fault || memory_fault) begin
                word_valid<=0;sample_ready_q<=0;samples_q<=0;owner_valid<=0;request_valid_q<=0;
                prediction_valid_q<=0;
                // An accepted request is still owed across every soft reset.
                // It drains above without repopulating an invalidated buffer.
            end else if(!paused) begin
                prediction_q<=predict_address;prediction_valid_q<=predict_valid;prediction_wide_q<=sixteen_bit;
                for(i=0;i<7;i=i+1) begin
                    sample_addr=fetch[i] ? fetch_address[i*24+:24] : demand_addr[i];
                    sample_wide=fetch[i] ? sixteen_bit[i] : demand_sixteen[i];
                    sample_tag=sample_addr[23:3];
                    if(fetch[i]) begin
                        demand_addr[i]<=sample_addr;demand_sixteen[i]<=sample_wide;sample_ready_q[i]<=0;
                    end
                    if(fetch[i] || (playing[i] && !sample_ready_q[i])) begin
                        if(available(i,sample_tag) && (!sample_wide || sample_addr[2:0]!=7 || available(i,sample_tag+21'd1))) begin
                            first_word=available_word(i,sample_tag);second_word=available_word(i,sample_tag+21'd1);
                            low_byte=first_word[sample_addr[2:0]*8+:8];
                            high_byte=sample_addr[2:0]==7 ? second_word[7:0] : first_word[(sample_addr[2:0]+1)*8+:8];
                            samples_q[i*16+:16]<=sample_wide ? {high_byte,low_byte} : {low_byte,8'b0};
                            sample_ready_q[i]<=1;
                        end
                    end
                end
                if(!request_valid_q && !owed && gap<=1 && chosen && !fetch[chosen_voice] && (!YIELD_TO_REU || !reu_memory_busy)) begin
                    request_addr_q<={chosen_tag,3'b0};request_valid_q<=1;
                    owner_valid<=1;owner_stale<=0;owner_tag<=chosen_tag;owner_entry<=chosen_entry;
                    word_valid[chosen_entry]<=0;
                end
            end
        end
    end
endmodule
