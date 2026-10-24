// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// REU streaming store on DDR. Four 32-byte read lines, next-line prefetch,
// and four merging 8-byte posted writes. One physical transaction is issued
// at a time, so an in-flight write completes before the next read starts.
// Queued writes forward into fills without delaying read-ahead.
// Resident lines are updated at write acceptance, including a line being
// filled. Pending writes overlay fill beats, so a fixed REU address always
// reads the newest byte. Only hard PL reset clears this store; guest reset
// and REU disable stop prefetch but continue draining accepted writes.
module multitini_c64_reu_cache #(
    parameter integer ADDRESS_BITS = 24
) (
    input wire clk, reset, enable, flush,
    input wire [23:0] hint_addr,
    input wire hint_read,
    input wire [23:0] request_addr,
    input wire request_write,
    input wire [63:0] request_wdata,
    input wire [7:0] request_wstrb,
    input wire request_valid,
    output wire request_ready,
    output wire [63:0] response_rdata,
    output wire response_valid,
    output reg [23:0] memory_addr,
    output reg memory_write,
    output reg [63:0] memory_wdata,
    output reg [7:0] memory_wstrb,
    output reg memory_burst,
    output wire memory_valid,
    input wire memory_ready,
    input wire [63:0] memory_rdata,
    input wire memory_rvalid, memory_fault,
    output wire busy,
    output reg fault,
    // Host preload invalidation is accepted as a pulse, drains old work, and
    // acknowledges only after all four read lines have become inaccessible.
    input wire invalidate,
    output reg invalidate_done,
    // Read-only coherence view. Addresses are aligned 64-bit DDR word tags.
    output wire [83:0] pending_addr,
    output wire [3:0] pending_valid,
    output wire [20:0] inflight_write_addr,
    output wire inflight_write_valid,
    output wire write_done,
    output wire [20:0] write_done_addr,
    output wire write_snoop_valid,
    output wire [20:0] write_snoop_addr
);
    localparam [1:0] IDLE=0, SEND=1, WAIT=2;
    reg [1:0] state;
    // Small independent word registers allow a returning fill and an unrelated
    // posted write to update different words without a replicated BRAM port.
    (* ram_style="registers" *) reg [63:0] data [0:15];
    reg [16:0] tags [0:3];
    reg [3:0] valid;
    reg [1:0] beat;
    reg [23:3] wb_addr [0:3];
    reg [63:0] wb_data [0:3];
    reg [7:0] wb_strb [0:3];
    reg [1:0] head, tail;
    reg [2:0] count;
    reg invalidate_pending;
    wire [1:0] young = tail - 2'd1;
    wire hit = valid[request_addr[6:5]] && tags[request_addr[6:5]] == request_addr[23:7];
    wire merge = count != 0 && wb_addr[young] == request_addr[23:3];
    wire accept_write = request_valid && request_ready && request_write;
    wire response = memory_rvalid && (state == WAIT || (state == SEND && memory_ready));
    wire filling = state != IDLE && !memory_write;
    wire fill_beat = response && !memory_write;
    wire [23:0] fill_addr = {memory_addr[23:5],beat,3'b000};
    wire update_line = hit || (filling && memory_addr[23:5] == request_addr[23:5]);
    // The modified REU's upper bank latch is not part of the REC counter.
    // Read ahead wraps inside its selected 512 KiB window, even with 16 MiB.
    wire [23:0] next_hint = {hint_addr[23:19], hint_addr[18:5] + 14'd1, 5'b00000};
    reg need_read;
    reg [23:0] read_addr;
    reg [63:0] fill_data;

    function automatic [63:0] merge_bytes(input [63:0] old_data, new_data, input [7:0] mask);
        integer lane;
        begin
            merge_bytes=old_data;
            for (lane=0;lane<8;lane=lane+1)
                if(mask[lane]) merge_bytes[lane*8+:8]=new_data[lane*8+:8];
        end
    endfunction

    always @* begin
        need_read=0; read_addr=0;
        if(request_valid && !request_write && !hit) begin
            need_read=1; read_addr=request_addr;
        end else if(enable && hint_read) begin
            if(!valid[hint_addr[6:5]] || tags[hint_addr[6:5]]!=hint_addr[23:7]) begin
                need_read=1; read_addr=hint_addr;
            end else if(!valid[next_hint[6:5]] || tags[next_hint[6:5]]!=next_hint[23:7]) begin
                need_read=1; read_addr=next_hint;
            end
        end
    end
    // The queue stays ordered. Overlay oldest to newest, then this edge's
    // accepted write. The queue cannot drain during a fill, and an earlier
    // physical write has completed before that fill starts.
    integer q, slot;
    always @* begin
        fill_data=memory_rdata;
        slot=0;
        for(q=0;q<4;q=q+1) begin
            slot=(head+q)&3;
            if(q<count && wb_addr[slot]==fill_addr[23:3])
                fill_data=merge_bytes(fill_data,wb_data[slot],wb_strb[slot]);
        end
        if(accept_write && request_addr[23:3]==fill_addr[23:3])
            fill_data=merge_bytes(fill_data,request_wdata,request_wstrb);
    end

    assign request_ready=!reset && !fault && !memory_fault && !invalidate_pending &&
        (request_write ? (count<4 || merge) : hit);
    // A cache hit is a logical memory completion on the acceptance edge.
    // The REU FSM still compares its ordered logical memory accesses to MAME;
    // burst fills and combined physical stores are independently validated.
    assign response_valid=request_valid && request_ready;
    assign response_rdata=data[request_addr[6:3]];
    assign memory_valid=state==SEND && !reset && !fault && !memory_fault;
    // A failed posted write cannot be declared safely drained, even after
    // its error response retired the only in-flight transaction.
    assign busy=fault || memory_fault || state!=IDLE || count!=0 || request_valid || invalidate_pending;
    genvar observer;
    generate for(observer=0;observer<4;observer=observer+1) begin: write_observers
        assign pending_addr[observer*21+:21]=wb_addr[observer];
        assign pending_valid[observer]=((observer+4-head)%4)<count;
    end endgenerate
    assign inflight_write_valid=state!=IDLE && memory_write;
    assign inflight_write_addr=memory_addr[23:3];
    assign write_done=response && memory_write && !reset && !fault && !memory_fault;
    assign write_done_addr=memory_addr[23:3];
    assign write_snoop_valid=accept_write;
    assign write_snoop_addr=request_addr[23:3];

    integer i;
    always @(posedge clk) begin
        if(reset) begin
            state<=IDLE; valid<=0; head<=0; tail<=0; count<=0; beat<=0; fault<=0;
            invalidate_pending<=0;invalidate_done<=0;
            memory_addr<=0; memory_write<=0; memory_wdata<=0; memory_wstrb<=0; memory_burst<=0;
            for(i=0;i<4;i=i+1) begin tags[i]<=0;wb_addr[i]<=0;wb_data[i]<=0;wb_strb[i]<=0;end
        end else begin
            invalidate_done<=0;
            if(invalidate) invalidate_pending<=1;
            if(memory_fault) fault<=1;
            // Retain dirty data on faults. Quiescence must not report success
            // if accepted guest writes have not reached DDR.
            if(accept_write) begin
                if(request_addr[2:0]!=0 || request_wstrb==0) fault<=1;
                if(merge) begin
                    wb_data[young]<=merge_bytes(wb_data[young],request_wdata,request_wstrb);
                    wb_strb[young]<=wb_strb[young]|request_wstrb;
                end else begin
                    wb_addr[tail]<=request_addr[23:3]; wb_data[tail]<=request_wdata;
                    wb_strb[tail]<=request_wstrb;tail<=tail+1'b1;count<=count+1'b1;
                end
                if(update_line && !(fill_beat && request_addr[23:3]==fill_addr[23:3]))
                    data[request_addr[6:3]]<=merge_bytes(data[request_addr[6:3]],request_wdata,request_wstrb);
            end
            if(state==IDLE && !fault && !memory_fault && !accept_write) begin
                // Give read-ahead priority over queued writes. Waiting for the
                // queue to empty can starve a swap stream when DDR writes take
                // longer than its two native cycles. Queued bytes overlay the
                // fill, while an already-issued write completed before IDLE.
                if(invalidate_pending && count==0 && !request_valid) begin
                    valid<=0;invalidate_pending<=0;invalidate_done<=1;
                end else if(need_read && !invalidate_pending) begin
                    memory_addr<={read_addr[23:5],5'b00000};memory_write<=0;
                    memory_wdata<=0;memory_wstrb<=0;memory_burst<=1;beat<=0;
                    tags[read_addr[6:5]]<=read_addr[23:7];valid[read_addr[6:5]]<=0;
                    state<=SEND;
                end else if(count!=0 && (wb_strb[head]==8'hff || count>1 || flush || !enable || invalidate_pending)) begin
                    // Combine a partial youngest word while streaming. Full
                    // words, older words and command completion drain it.
                    memory_addr<={wb_addr[head],3'b000};memory_write<=1;
                    memory_wdata<=wb_data[head];memory_wstrb<=wb_strb[head];memory_burst<=0;
                    head<=head+1'b1;count<=count-1'b1;state<=SEND;
                end
            end
            if(state==SEND && memory_ready && memory_valid) state<=WAIT;
            if(response) begin
                if(memory_write) state<=IDLE;
                else begin
                    data[fill_addr[6:3]]<=fill_data;
                    if(beat==3) begin
                        // Faulted fills never become readable.
                        valid[memory_addr[6:5]]<=!fault && !memory_fault;
                        state<=IDLE;
                    end else beat<=beat+1'b1;
                end
            end
        end
    end
endmodule
