// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Seven two-word buffers. One outstanding read; registered REU snoops and
// all posted/in-flight/recent writes fence a matching physical DDR word.
// Recent writes use two tracked words; a third inside one hold window
// fences every word for a full window instead.
//
// One shared evaluator and one shared extractor serve all seven voices. Each
// voice keeps a registered read status (candidate, class, deferred); issue
// picks the earliest deadline from those statuses and re-evaluates only the
// winner against live state. A fetch freezes which buffer entries were valid
// on its edge, so the later extraction returns the bytes resident at the
// logical fetch, as a same-edge latch would.
module multitini_c64_ua_fetch #(
    parameter integer MIN_ISSUE_GAP=32,
    parameter integer YIELD_TO_REU=0,
    parameter integer POST_B_HOLD=16
) (
    input wire clk, hard_reset, reset, paused,
    input wire invalidate,
    output reg invalidate_done=0,
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
    // REU writes complete one at a time, so two entries cover the window.
    localparam integer HOLD_ENTRIES=2;
    localparam integer HOLD_BITS=POST_B_HOLD>0 ? $clog2(POST_B_HOLD+1) : 1;
    localparam [HOLD_BITS-1:0] HOLD_LOAD=POST_B_HOLD;
    // Entry 2v of voice v lives in data0[v], entry 2v+1 in data1[v].
    (* ram_style="distributed" *) reg [63:0] data0[0:6];
    (* ram_style="distributed" *) reg [63:0] data1[0:6];
    reg [20:0] word_tag[0:13];
    reg [13:0] word_valid, snap_valid;
    reg [6:0] frozen;
    reg [23:0] demand_addr[0:6];
    reg [6:0] demand_sixteen;
    reg [111:0] samples_q;
    reg [6:0] sample_ready_q;
    reg [167:0] prediction_q;
    reg [6:0] prediction_valid_q, prediction_wide_q;
    reg [20:0] hold_tag[0:HOLD_ENTRIES-1];
    reg [HOLD_BITS-1:0] hold_left[0:HOLD_ENTRIES-1];
    reg [HOLD_BITS-1:0] hold_all;
    reg snoop_valid;
    reg [20:0] snoop_tag;
    reg owed, owner_valid, owner_stale;
    reg [3:0] owner_entry;
    reg [20:0] owner_tag;
    reg [23:0] request_addr_q;
    reg request_valid_q;
    reg [6:0] status_candidate, status_deferred, status_dirty;
    reg [1:0] status_class[0:6];
    reg [2:0] scan, extract_scan, landed_voice;
    reg landed;
    localparam integer GAP_BITS=MIN_ISSUE_GAP>1 ? $clog2(MIN_ISSUE_GAP) : 1; reg [GAP_BITS-1:0] gap;
    reg invalidating=0,invalidate_seen=0;
    // Unconnected legacy bench ports are inactive; physical logic is a bit.
    wire invalidate_level=(invalidate===1'b1);
    wire fenced=invalidate_level || invalidating;
    assign accepted=memory_valid && memory_ready;
    assign busy=memory_valid || owed || invalidating;

    function automatic blocked(input [20:0] tag);
        integer n;
        begin
            blocked=(cache_inflight_write_valid && cache_inflight_write_addr==tag) ||
                    (snoop_valid && snoop_tag==tag);
            for(n=0;n<4;n=n+1)
                if(cache_pending_valid[n] && cache_pending_addr[n*21+:21]==tag) blocked=1;
            for(n=0;n<HOLD_ENTRIES;n=n+1)
                if(hold_left[n]!=0 && hold_tag[n]==tag) blocked=1;
            if(hold_all!=0) blocked=1;
            // The first guard cycle is the write response edge itself.
            if(POST_B_HOLD>0 && cache_write_done && cache_write_done_addr==tag) blocked=1;
        end
    endfunction
    // A registered snoop blocks a resident hit on the same clock, before the
    // valid bit clears on that edge.
    function automatic entry_live(input valid, input [20:0] tag);
        begin entry_live=valid && !(snoop_valid && snoop_tag==tag); end
    endfunction
    function automatic hit(input valid0, input [20:0] tag0, input valid1, input [20:0] tag1, input [20:0] tag);
        begin hit=(valid0 && tag0==tag) || (valid1 && tag1==tag); end
    endfunction
    reg [13:0] live_valid;
    always_comb for(integer e=0;e<14;e=e+1) live_valid[e]=entry_live(word_valid[e],word_tag[e]);

    // A response retires an owed read or a queued request accepted on that
    // edge. A request issued this clock only turns while owed, so this equals
    // owed || accepted without the evaluator in front of the extractor.
    wire returning=owed || (!fenced && request_valid_q && memory_ready);
    // An arriving word is installed unless a reset, fence or same-word write
    // makes it stale.
    wire response_write=memory_rvalid && returning && owner_valid && !owner_stale &&
        !(snoop_valid && snoop_tag==owner_tag) && !reset && !fenced && !fault && !memory_fault;

    // Earliest deadline among registered statuses: candidates before none,
    // undeferred before deferred, then class, deadline and voice number.
    reg [24:0] key[0:7], first_pair[0:3], second_pair[0:1], winner_key;
    reg [2:0] winner, first_dirty;
    reg winner_valid;
    always_comb begin
        for(integer v=0;v<7;v=v+1)
            key[v]=status_candidate[v] ? {1'b0,status_deferred[v],status_class[v],deadlines[v*18+:18],v[2:0]} : {25{1'b1}};
        key[7]={25{1'b1}};
        for(integer v=0;v<4;v=v+1) first_pair[v]=key[2*v]<=key[2*v+1] ? key[2*v] : key[2*v+1];
        for(integer v=0;v<2;v=v+1) second_pair[v]=first_pair[2*v]<=first_pair[2*v+1] ? first_pair[2*v] : first_pair[2*v+1];
        winner_key=second_pair[0]<=second_pair[1] ? second_pair[0] : second_pair[1];
        winner_valid=!winner_key[24];winner=winner_key[2:0];
        first_dirty=0;
        for(integer v=6;v>=0;v=v-1) if(status_dirty[v]) first_dirty=v[2:0];
        deferred=|(status_candidate & status_deferred);
    end
    // An issue slot waits until every freshly fetched voice is re-evaluated,
    // so a new demand competes by deadline; seven simultaneous fetches clear
    // within eight clocks, which also bounds the wait. The slot then serves
    // the winner. The DDR path becomes ready on the response edge, so the
    // next word may issue on that edge, retiring the old owner before
    // installing the new one.
    reg [3:0] dirty_wait;
    wire slot_ready=!request_valid_q && !reset && !paused && !fenced && !fault && !memory_fault &&
        (!YIELD_TO_REU || !reu_memory_busy) && winner_valid &&
        ((!owed && gap<=1) || (owed && memory_rvalid && gap==0));
    wire slot_free=slot_ready && (status_dirty==0 || dirty_wait[3]);
    wire [2:0] ev=slot_free ? winner : (status_dirty!=0 ? first_dirty : scan);

    // Shared evaluator. A demand is the current sample, prediction the next
    // logical address, and look-ahead the following word.
    wire [23:0] e_addr=demand_addr[ev];
    wire [20:0] e_tag0=e_addr[23:3], e_tag1=e_tag0+21'd1;
    wire e_straddle=demand_sixteen[ev] && e_addr[2:0]==3'd7;
    wire e_need=playing[ev] && !sample_ready_q[ev];
    wire [23:0] e_pred=prediction_q[ev*24+:24];
    wire [20:0] e_ptag=e_pred[23:3], e_ptag1=e_ptag+21'd1;
    wire e_wv0=word_valid[{ev,1'b0}], e_wv1=word_valid[{ev,1'b1}];
    wire e_lv0=live_valid[{ev,1'b0}], e_lv1=live_valid[{ev,1'b1}];
    wire [20:0] e_t0=word_tag[{ev,1'b0}], e_t1=word_tag[{ev,1'b1}];
    wire e_own=owed && owner_valid && !owner_stale && owner_entry[3:1]==ev;
    wire e_res0=hit(e_lv0,e_t0,e_lv1,e_t1,e_tag0), e_res1=hit(e_lv0,e_t0,e_lv1,e_t1,e_tag1);
    wire e_resp=hit(e_lv0,e_t0,e_lv1,e_t1,e_ptag), e_resp1=hit(e_lv0,e_t0,e_lv1,e_t1,e_ptag1);
    wire e_rsv0=e_own && owner_tag==e_tag0, e_rsv1=e_own && owner_tag==e_tag1;
    wire e_rsvp=e_own && owner_tag==e_ptag, e_rsvp1=e_own && owner_tag==e_ptag1;
    reg candidate, pinned0, pinned1, e_blocked;
    reg [20:0] candidate_tag;
    reg [1:0] candidate_class;
    reg [3:0] target;
    always_comb begin
        candidate=0;candidate_class=0;candidate_tag=e_tag0;target={ev,1'b0};
        if(e_need && !e_res0 && !e_rsv0) candidate=1;
        else if(e_need && e_straddle && !e_res1 && !e_rsv1) begin candidate=1;candidate_tag=e_tag1;end
        else if(playing[ev] && prediction_valid_q[ev]) begin
            candidate_tag=e_ptag; candidate_class=1;
            if(!e_resp && !e_rsvp) candidate=1;
            else if(prediction_wide_q[ev] && e_pred[2:0]==7 && !e_resp1 && !e_rsvp1) begin
                candidate=1;candidate_tag=e_ptag1;
            end else if(!e_resp1 && !e_rsvp1) begin
                candidate=1;candidate_tag=e_ptag1;candidate_class=2;
            end
        end
        // Keep any current-demand word until its sample has been latched.
        pinned0=(e_need && e_wv0 && (e_t0==e_tag0 || (e_straddle && e_t0==e_tag1))) ||
                (owed && owner_valid && owner_entry=={ev,1'b0});
        pinned1=(e_need && e_wv1 && (e_t1==e_tag0 || (e_straddle && e_t1==e_tag1))) ||
                (owed && owner_valid && owner_entry=={ev,1'b1});
        if(candidate_class==2 && prediction_valid_q[ev]) begin
            // Look-ahead must retain the resident predicted word. Without
            // this pin a full buffer could alternate two tags forever.
            pinned0=pinned0 || (e_wv0 && e_t0==e_ptag);
            pinned1=pinned1 || (e_wv1 && e_t1==e_ptag);
        end
        if(!e_wv0 && !pinned0) target={ev,1'b0};
        else if(!e_wv1 && !pinned1) target={ev,1'b1};
        else if(!pinned0) target={ev,1'b0};
        else if(!pinned1) target={ev,1'b1};
        else candidate=0;
        // A frozen voice's entries hold its pending sample.
        if(frozen[ev]) candidate=0;
        // blocked() reads fence state, so it is evaluated here, not in an
        // assign that would only follow its arguments.
        e_blocked=candidate && blocked(candidate_tag);
    end
    wire issue=slot_free && candidate && !e_blocked && !fetch[ev];
    wire turn_request=issue && owed;
    assign memory_valid=!fenced && (request_valid_q || turn_request);
    assign memory_addr=turn_request ? {candidate_tag,3'b0} : request_addr_q;

    // Shared extractor: the voice owning a word on its response edge first,
    // then a just-landed word's voice, then frozen voices, then a rotating
    // check of voices still waiting for their sample.
    reg [2:0] xv, first_frozen;
    wire [2:0] rv=owner_entry[3:1];
    wire [23:0] r_addr=demand_addr[rv];
    wire r_demand=owner_tag==r_addr[23:3] ||
        (demand_sixteen[rv] && r_addr[2:0]==3'd7 && owner_tag==r_addr[23:3]+21'd1);
    wire r_wants=response_write && r_demand && (frozen[rv] || (playing[rv] && !sample_ready_q[rv]));
    wire landed_wants=landed && (frozen[landed_voice] ||
        (playing[landed_voice] && !sample_ready_q[landed_voice]));
    always_comb begin
        first_frozen=0;
        for(integer v=6;v>=0;v=v-1) if(frozen[v]) first_frozen=v[2:0];
        xv=r_wants ? rv : landed_wants ? landed_voice : (frozen!=0 ? first_frozen : extract_scan);
    end
    wire [23:0] x_addr=demand_addr[xv];
    wire [20:0] x_tag0=x_addr[23:3], x_tag1=x_tag0+21'd1;
    wire x_straddle=demand_sixteen[xv] && x_addr[2:0]==3'd7;
    // The owner's entry is written on its response edge; read that word from
    // the response itself, as the entry will hold it.
    wire x_f0=response_write && owner_entry=={xv,1'b0};
    wire x_f1=response_write && owner_entry=={xv,1'b1};
    // Entries valid at the voice's fetch edge stay usable for that sample.
    wire x_v0=live_valid[{xv,1'b0}] || (frozen[xv] && snap_valid[{xv,1'b0}]) || x_f0;
    wire x_v1=live_valid[{xv,1'b1}] || (frozen[xv] && snap_valid[{xv,1'b1}]) || x_f1;
    wire [20:0] x_t0=x_f0 ? owner_tag : word_tag[{xv,1'b0}];
    wire [20:0] x_t1=x_f1 ? owner_tag : word_tag[{xv,1'b1}];
    wire [63:0] x_d0=x_f0 ? memory_rdata : data0[xv];
    wire [63:0] x_d1=x_f1 ? memory_rdata : data1[xv];
    wire x_lo0=x_v0 && x_t0==x_tag0, x_hi0=x_v0 && x_t0==x_tag1;
    wire x_ready=(x_lo0 || (x_v1 && x_t1==x_tag0)) &&
        (!x_straddle || x_hi0 || (x_v1 && x_t1==x_tag1));
    wire [63:0] x_first=x_lo0 ? x_d0 : x_d1;
    wire [63:0] x_second=x_hi0 ? x_d0 : x_d1;
    wire [7:0] x_low=x_first[x_addr[2:0]*8+:8];
    wire [7:0] x_high=x_addr[2:0]==7 ? x_second[7:0] : x_first[(x_addr[2:0]+3'd1)*8+:8];
    wire [15:0] x_sample=demand_sixteen[xv] ? {x_high,x_low} : {x_low,8'b0};
    wire x_go=(frozen[xv] || (playing[xv] && !sample_ready_q[xv])) && !fetch[xv];
    // A fresh response present at the sample boundary can be consumed on
    // that edge. Only the previously latched demand is exposed here: fetch
    // may already describe the following sample at this same boundary.
    wire x_now=r_wants && playing[xv] && !sample_ready_q[xv] && !paused && x_ready;

    always_comb begin
        samples=samples_q;sample_ready=sample_ready_q;
        if(x_now)begin samples[xv*16+:16]=x_sample;sample_ready[xv]=1;end
        if(fenced)begin samples=0;sample_ready=0;end
    end

    always @(posedge clk)
        if(!hard_reset && response_write) begin
            if(owner_entry[0]) data1[owner_entry[3:1]]<=memory_rdata;
            else data0[owner_entry[3:1]]<=memory_rdata;
        end

    integer i;
    always @(posedge clk) begin
        invalidate_done<=0;
        landed<=0;
        if(hard_reset) begin
            invalidating<=0;invalidate_seen<=0;
            word_valid<=0;snap_valid<=0;frozen<=0;sample_ready_q<=0;samples_q<=0;demand_sixteen<=0;
            prediction_q<=0;prediction_valid_q<=0;prediction_wide_q<=0;
            hold_all<=0;snoop_valid<=0;snoop_tag<=0;owed<=0;owner_valid<=0;owner_stale<=0;
            owner_entry<=0;owner_tag<=0;request_addr_q<=0;request_valid_q<=0;gap<=0;fault<=0;
            status_candidate<=0;status_deferred<=0;status_dirty<=0;scan<=0;extract_scan<=0;landed_voice<=0;
            dirty_wait<=0;
            for(i=0;i<7;i=i+1) begin demand_addr[i]<=0;status_class[i]<=0;end
            for(i=0;i<14;i=i+1) word_tag[i]<=0;
            for(i=0;i<HOLD_ENTRIES;i=i+1) begin hold_tag[i]<=0;hold_left[i]<=0;end
        end else begin
            invalidate_seen<=invalidate_level;
            if(invalidate_level && !invalidate_seen) invalidating<=1;
            if(invalidating && (!owed || memory_rvalid))begin
                invalidating<=0;invalidate_done<=1;
            end
            snoop_valid<=cache_write_snoop_valid;snoop_tag<=cache_write_snoop_addr;
            // A word is held for POST_B_HOLD clocks after its B response.
            // An entry in its last held clock is free for the next write.
            for(i=0;i<HOLD_ENTRIES;i=i+1) if(hold_left[i]!=0) hold_left[i]<=hold_left[i]-1'b1;
            if(hold_all!=0) hold_all<=hold_all-1'b1;
            if(POST_B_HOLD>0 && cache_write_done) begin
                if(hold_left[0]<=1) begin hold_tag[0]<=cache_write_done_addr;hold_left[0]<=HOLD_LOAD;end
                else if(hold_left[1]<=1) begin hold_tag[1]<=cache_write_done_addr;hold_left[1]<=HOLD_LOAD;end
                else hold_all<=HOLD_LOAD;
            end
            if(gap>0) gap<=gap-1;
            if(!slot_ready || status_dirty==0) dirty_wait<=0;
            else if(!dirty_wait[3]) dirty_wait<=dirty_wait+4'd1;
            if(memory_fault) fault<=1;
            if(snoop_valid) begin
                for(i=0;i<14;i=i+1) if(word_tag[i]==snoop_tag) word_valid[i]<=0;
                if(owner_tag==snoop_tag && owner_valid) owner_stale<=1;
            end
            if(accepted) begin
                request_valid_q<=0;owed<=1;gap<=MIN_ISSUE_GAP>0 ? MIN_ISSUE_GAP-1 : 0;
            end
            if(memory_rvalid && returning) begin
                owed<=0;
                if(response_write) begin
                    word_tag[owner_entry]<=owner_tag;word_valid[owner_entry]<=1;
                    landed<=1;landed_voice<=owner_entry[3:1];
                end
                owner_valid<=0;
            end
            if(turn_request) begin
                request_addr_q<={candidate_tag,3'b0};request_valid_q<=!memory_ready;owed<=memory_ready;
                owner_valid<=1;owner_stale<=0;owner_tag<=candidate_tag;owner_entry<=target;
                word_valid[target]<=0;
            end
            if(reset || fault || memory_fault || fenced) begin
                word_valid<=0;sample_ready_q<=0;samples_q<=0;owner_valid<=0;request_valid_q<=0;
                prediction_valid_q<=0;frozen<=0;landed<=0;
                status_candidate<=0;status_deferred<=0;status_dirty<=0;
                // An accepted request is still owed across every soft reset.
                // It drains above without repopulating an invalidated buffer.
            end else if(!paused) begin
                prediction_q<=predict_address;prediction_valid_q<=predict_valid;prediction_wide_q<=sixteen_bit;
                // Refresh the evaluated voice's status. An issued voice is
                // re-evaluated before it competes again.
                status_candidate[ev]<=candidate;status_class[ev]<=candidate_class;
                status_deferred[ev]<=e_blocked;status_dirty[ev]<=issue;
                if(!slot_free && status_dirty==0) scan<=scan==6 ? 3'd0 : scan+3'd1;
                if(issue) dirty_wait<=0;
                if(issue && !owed) begin
                    request_addr_q<={candidate_tag,3'b0};request_valid_q<=1;
                    owner_valid<=1;owner_stale<=0;owner_tag<=candidate_tag;owner_entry<=target;
                    word_valid[target]<=0;
                end
                if(x_go) begin
                    frozen[xv]<=0;
                    if(frozen[xv]) status_dirty[xv]<=1;
                    if(x_ready) begin
                        samples_q[xv*16+:16]<=demand_sixteen[xv] ? {x_high,x_low} : {x_low,8'b0};
                        sample_ready_q[xv]<=1;
                    end
                end
                if(!landed_wants && frozen==0) extract_scan<=extract_scan==6 ? 3'd0 : extract_scan+3'd1;
                for(i=0;i<7;i=i+1) if(fetch[i]) begin
                    demand_addr[i]<=fetch_address[i*24+:24];demand_sixteen[i]<=sixteen_bit[i];
                    sample_ready_q[i]<=0;frozen[i]<=1;status_dirty[i]<=1;
                    snap_valid[2*i]<=live_valid[2*i] || (response_write && owner_entry==2*i);
                    snap_valid[2*i+1]<=live_valid[2*i+1] || (response_write && owner_entry==2*i+1);
                end
            end
        end
    end
endmodule
