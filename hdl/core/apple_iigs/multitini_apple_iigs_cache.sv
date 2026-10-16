// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// 32 KiB direct-mapped cache with 32-byte lines and a synchronous BRAM
// lookup. CPU and HDD DMA share this coherent path. Writes are posted: a bus
// write retires as soon as the merging write buffer can take it, updates a
// resident line (write-update, no write-allocate) and drains to DDR in order.
// Every read miss, bypass read and loader request waits until the buffer and
// any DDR write are fully acknowledged, because AXI does not order reads
// against writes. Read misses fill a whole line with one four-beat burst.
//
// Retirement is pipelined for timing: the retire edge (CPU bus_commit, or the
// staged HDD DMA commit) only loads a one-entry retire slot. On the next
// cycle the slot merges into or appends to the write buffer and updates a
// resident line, using the asynchronous tag RAM at the slot's own registered
// address. Write acceptance therefore depends only on registered buffer
// state, never on the live lookup compare. A read hit is blocked while the
// slot is valid, and the slot's RAM write clears the lookup trust flags.
//
// Memory interface v2 (requester side): one transaction outstanding,
// accepted on memory_valid && memory_ready. memory_burst=1 is a 4-beat read
// of the 32-byte-aligned line; otherwise one 64-bit beat. A read returns one
// memory_rvalid per beat in ascending order; a write returns one memory_rvalid
// after DDR acknowledged it. memory_busy is registered and is 0 only when no
// posted write and no memory transaction remains.
module multitini_apple_iigs_cache #(
    parameter integer INDEX_BITS=10,
    parameter integer WB_BITS=2
)(
    input wire clk,reset,invalidate,lookup_tick,request_tick,
    input wire [23:0] bus_addr,
    input wire [7:0] bus_wdata,
    // bus_commit retires the bus cycle. commit_staged marks the part of
    // bus_commit that retires the bus cycle staged at the last request_tick
    // (HDD DMA: its address and data move on the native edge before commit).
    input wire bus_valid,bus_write,bus_commit,commit_staged,write_launch,
    input wire bypass,
    output wire [7:0] bus_rdata,
    output wire [63:0] bus_rword,
    output wire stall,
    output reg fault,
    output reg [31:0] hits,misses,wait_cycles,
    input wire loader_valid,loader_write,
    input wire [23:0] loader_addr,
    input wire [63:0] loader_wdata,
    input wire [7:0] loader_wstrb,
    output wire loader_ready,
    output wire loader_response,
    output wire [63:0] loader_rdata,
    output reg [23:0] memory_addr,
    output reg memory_write,
    output reg [63:0] memory_wdata,
    output reg [7:0] memory_wstrb,
    output reg memory_burst,
    output wire memory_valid,
    input wire memory_ready,
    input wire [63:0] memory_rdata,
    input wire memory_rvalid,memory_fault,
    output reg memory_busy
);
    localparam integer LINES=1<<INDEX_BITS;
    localparam integer WORDS=LINES*4;
    localparam integer WORD_BITS=INDEX_BITS+2;
    localparam integer TAG_BITS=19-INDEX_BITS;
    localparam integer WB_DEPTH=1<<WB_BITS;
    (* ram_style="block" *) reg [63:0] data[0:WORDS-1];
    // Tags are narrow; distributed RAM preserves the full data cache
    // while leaving scarce BRAM available to the IIgs video/audio/floppies.
    (* ram_style="distributed" *) reg [TAG_BITS:0] tags[0:LINES-1];
    reg [63:0] lookup_data;
    reg [TAG_BITS:0] lookup_tag;
    reg [23:3] lookup_addr;
    // A lookup register is trusted only when no tag (or data) write landed
    // on or after the edge that captured it.
    reg lookup_tag_ok,lookup_data_ok;
    reg [INDEX_BITS:0] sweep;
    reg [2:0] state;
    reg owner_loader,txn_write,txn_fill,fill_ok;
    reg [1:0] beat;
    reg [23:0] pending_addr;
    reg bypass_valid;
    reg [23:3] bypass_addr;
    reg [63:0] bypass_data;
    reg bypass_valid_q;
    reg [23:3] bypass_addr_q;
    reg [63:0] bypass_data_q;
    // Bus write staged at request_tick, retired by commit_staged.
    reg stage_valid;
    reg [23:0] stage_addr;
    reg [7:0] stage_byte;
    // Retire slot: one retired write, processed on the following cycle.
    reg rs_valid;
    reg [23:0] rs_addr;
    reg [7:0] rs_byte;
    // Posted write buffer: unlaunched entries only, oldest at wb_head.
    // Four entries with a parallel youngest-entry compare: plain registers.
    (* ram_style="registers" *) reg [23:3] wb_addr[0:WB_DEPTH-1];
    (* ram_style="registers" *) reg [63:0] wb_data[0:WB_DEPTH-1];
    (* ram_style="registers" *) reg [7:0] wb_strb[0:WB_DEPTH-1];
    reg [WB_BITS-1:0] wb_head,wb_tail;
    reg [WB_BITS:0] wb_count;
    localparam IDLE=0,SEND=1,WAIT=2,SETTLE=3;
    wire initialized=sweep[INDEX_BITS];
    wire lookup_match=lookup_addr==bus_addr[23:3];
    wire tag_hit=lookup_tag[TAG_BITS] && lookup_tag[TAG_BITS-1:0]==bus_addr[23:INDEX_BITS+5];
    // A CPU write and the lookup both capture the same bus address on the
    // retire edge, so while the slot is valid a matching lookup can only be
    // the just-written word: block every hit for that one cycle. A staged
    // DMA retirement loads the slot only at the end of its cycle, after a
    // lookup that may hold the same word: block hits on that cycle too.
    wire hit=initialized && lookup_match && lookup_tag_ok && lookup_data_ok && tag_hit && !rs_valid &&
        !commit_staged;
    // A primary-clock CPU may consume adjacent bytes on consecutive edges.
    // Retirement invalidates the live token immediately; the staged token
    // alone would survive one more lookup edge and authorize a second read.
    // A staged DMA retirement blocks it like an L1 hit.
    wire bypass_hit=bypass_valid && bypass_valid_q && bypass_addr_q==bus_addr[23:3] && !commit_staged;
    wire [WB_BITS-1:0] wb_young=wb_tail-1'b1;
    wire wb_empty=wb_count==0;
    wire wb_full=wb_count==WB_DEPTH;
    wire txn_read=state!=IDLE && !owner_loader && !txn_write;
    // A write is accepted from registered state only: room in the buffer
    // with the retire slot counted as an entry, no read transaction in
    // flight, and no staged DMA retirement owning this cycle.
    wire wb_space=rs_valid ? wb_count<WB_DEPTH-1 : wb_count<WB_DEPTH;
    wire write_ok=wb_space && !txn_read && !commit_staged;
    assign stall=!initialized || fault || (bus_valid && (bus_write ? !write_ok :
        !(bypass ? bypass_hit : hit))) || loader_valid || owner_loader;
    assign bus_rdata=bypass ? bypass_data_q[bus_addr[2:0]*8+:8] : lookup_data[bus_addr[2:0]*8+:8];
    assign bus_rword=bypass ? bypass_data_q : lookup_data;
    // Retire exactly once per bus write: load the retire slot.
    wire live_write=request_tick && write_launch && bus_valid && bus_write;
    wire enq_live=bus_commit && !commit_staged && live_write;
    wire enq_staged=commit_staged && stage_valid;
    wire enq=enq_live || enq_staged;
    wire [23:0] enq_addr=enq_staged ? stage_addr : bus_addr;
    wire [7:0] enq_byte=enq_staged ? stage_byte : bus_wdata;
    // Slot processing, one cycle after retirement, from registers only.
    wire [TAG_BITS:0] rs_tag=tags[rs_addr[INDEX_BITS+4:5]];
    wire rs_hit=rs_tag[TAG_BITS] && rs_tag[TAG_BITS-1:0]==rs_addr[23:INDEX_BITS+5];
    wire launch_write=state==IDLE && !fault && !wb_empty;
    // Reads and loader requests start only with no posted write left. A
    // live enqueue needs a bus write, so it never coincides with a bus read
    // request; a loader write accepted on that same cycle targets ROM space
    // (never CPU-writable) and needs no ordering against it.
    wire drained=state==IDLE && initialized && !fault && wb_empty && !rs_valid && !enq_staged;
    wire launch_loader=drained && loader_valid;
    wire read_request=request_tick && lookup_match && lookup_tag_ok && lookup_data_ok && bus_valid && !bus_write &&
        !(bypass ? bypass_hit : hit);
    wire launch_read=drained && !loader_valid && read_request;
    wire merge=rs_valid && !wb_empty && wb_addr[wb_young]==rs_addr[23:3] && !(launch_write && wb_count==1);
    wire push=rs_valid && !merge;
    assign memory_valid=state==SEND;
    assign loader_ready=drained;
    assign loader_response=state==WAIT && owner_loader && memory_rvalid;
    assign loader_rdata=memory_rdata;
    // One BRAM write port: line-fill beats or the retire slot updating a
    // resident line. They never coincide: a fill starts only when drained
    // (no slot, no buffered write), and no write is accepted, so no slot is
    // loaded, while a read transaction is in flight. Tags change only on
    // sweep and fills, so the slot's asynchronous tag read is always current.
    wire fill_beat=state==WAIT && txn_fill && memory_rvalid;
    wire fill_last=fill_beat && beat==3;
    wire rs_update=rs_valid && initialized && rs_hit && !fault;
    wire bram_we=fill_beat || rs_update;
    wire [WORD_BITS-1:0] bram_waddr=fill_beat ? {pending_addr[WORD_BITS+2:5],beat} : rs_addr[WORD_BITS+2:3];
    wire [63:0] bram_wdata=fill_beat ? memory_rdata : {8{rs_byte}};
    wire [7:0] bram_wbe=fill_beat ? 8'hff : 8'b1<<rs_addr[2:0];
    wire fill_set=fill_last && fill_ok && !memory_fault && !invalidate;
    wire tag_we=!initialized || fill_last || launch_read && !bypass;
    wire [INDEX_BITS-1:0] tag_waddr=!initialized ? sweep[INDEX_BITS-1:0] :
        fill_last ? pending_addr[INDEX_BITS+4:5] : bus_addr[INDEX_BITS+4:5];
    wire [TAG_BITS:0] tag_wdata=initialized && fill_last && fill_set ? {1'b1,pending_addr[23:INDEX_BITS+5]} : 0;
    // Simple dual-port block RAM with byte-lane write enables.
    always @(posedge clk)begin
        if(lookup_tick)lookup_data<=data[bus_addr[WORD_BITS+2:3]];
    end
    always @(posedge clk)begin
        for(integer lane=0;lane<8;lane=lane+1)
            if(bram_we && bram_wbe[lane])data[bram_waddr][lane*8+:8]<=bram_wdata[lane*8+:8];
    end
    always @(posedge clk)begin
        // CPU changes its bus on primary phase3. The phase1 lookup gives
        // that combinational bus two primary periods to settle, then gives
        // the CPU two periods for RAM/decode before the next phase3 commit.
        if(lookup_tick)begin
            lookup_tag<=tags[bus_addr[INDEX_BITS+4:5]];
            lookup_addr<=bus_addr[23:3];
            bypass_data_q<=bypass_data;bypass_addr_q<=bypass_addr;bypass_valid_q<=bypass_valid;
        end
        if(tag_we)tags[tag_waddr]<=tag_wdata;
    end
    always @(posedge clk)begin
        if(reset)begin
            state<=IDLE;sweep<=0;fault<=0;owner_loader<=0;txn_write<=0;txn_fill<=0;fill_ok<=0;beat<=0;
            bypass_valid<=0;hits<=0;misses<=0;wait_cycles<=0;
            memory_addr<=0;memory_write<=0;memory_wdata<=0;memory_wstrb<=0;memory_burst<=0;memory_busy<=0;
            pending_addr<=0;bypass_addr<=0;bypass_data<=0;
            lookup_tag_ok<=0;lookup_data_ok<=0;
            stage_valid<=0;stage_addr<=0;stage_byte<=0;
            rs_valid<=0;rs_addr<=0;rs_byte<=0;
            wb_head<=0;wb_tail<=0;wb_count<=0;
        end else begin
            if(lookup_tick)begin lookup_tag_ok<=!tag_we;lookup_data_ok<=!bram_we;end
            else begin
                if(tag_we)lookup_tag_ok<=0;
                if(bram_we)lookup_data_ok<=0;
            end
            if(commit_staged)stage_valid<=0;
            if(request_tick)begin
                stage_valid<=live_write;stage_addr<=bus_addr;stage_byte<=bus_wdata;
            end
            // Load the retire slot; the previous slot is processed this cycle.
            // Address and byte load every cycle, so no wide enable depends
            // on the CPU enable; only rs_valid records the retirement.
            rs_valid<=enq;rs_addr<=enq_addr;rs_byte<=enq_byte;
            if(!initialized)sweep<=sweep+1'b1;
            if(memory_fault)fault<=1;
            if(bus_commit)begin
                bypass_valid<=0;
                if(!bus_write && hit)hits<=hits+1'b1;
            end
            if(stall && bus_valid)wait_cycles<=wait_cycles+1'b1;
            // Write buffer: pop on launch; the retire slot merges into the
            // youngest unlaunched entry for the same word, otherwise appends.
            if(merge)begin
                for(integer lane=0;lane<8;lane=lane+1)if(rs_addr[2:0]==lane)
                    wb_data[wb_young][lane*8+:8]<=rs_byte;
                wb_strb[wb_young]<=wb_strb[wb_young] | (8'b1<<rs_addr[2:0]);
            end
            // The stall contract makes overflow unreachable; if it ever
            // happened, hold the machine instead of losing a retired write.
            if(push && wb_full && !launch_write)fault<=1;
            else if(push)begin
                wb_addr[wb_tail]<=rs_addr[23:3];wb_data[wb_tail]<={8{rs_byte}};
                wb_strb[wb_tail]<=8'b1<<rs_addr[2:0];wb_tail<=wb_tail+1'b1;
            end
            if(launch_write)wb_head<=wb_head+1'b1;
            wb_count<=wb_count+(push && (!wb_full || launch_write) ? 1'b1 : 1'b0)-(launch_write ? 1'b1 : 1'b0);
            // After a sticky fault the machine is held until reload, so its
            // posted writes are dropped: memory_busy then clears as soon as
            // any DDR transaction already in flight has completed.
            memory_busy<=state!=IDLE || (!fault && (!wb_empty || rs_valid || enq || launch_loader || launch_read));
            case(state)
                IDLE:if(launch_write)begin
                    owner_loader<=0;txn_write<=1;txn_fill<=0;
                    memory_addr<={wb_addr[wb_head],3'b000};memory_write<=1;memory_burst<=0;
                    memory_wdata<=wb_data[wb_head];memory_wstrb<=wb_strb[wb_head];state<=SEND;
                end else if(launch_loader)begin
                    owner_loader<=1;txn_write<=0;txn_fill<=0;memory_addr<=loader_addr;memory_burst<=0;
                    memory_write<=loader_write;memory_wdata<=loader_wdata;memory_wstrb<=loader_wstrb;
                    state<=SEND;bypass_valid<=0;
                end else if(launch_read)begin
                    owner_loader<=0;txn_write<=0;txn_fill<=!bypass;fill_ok<=1;beat<=0;pending_addr<=bus_addr;
                    memory_addr<=bypass ? {bus_addr[23:3],3'b000} : {bus_addr[23:5],5'b00000};
                    memory_burst<=!bypass;memory_write<=0;memory_wdata<=0;memory_wstrb<=0;
                    state<=SEND;misses<=misses+1'b1;
                end
                SEND:if(memory_ready)state<=WAIT;
                WAIT:if(memory_rvalid)begin
                    if(owner_loader)begin state<=IDLE;owner_loader<=0;end
                    else if(txn_write)begin state<=IDLE;txn_write<=0;end
                    else if(txn_fill)begin
                        beat<=beat+1'b1;
                        if(beat==3)begin state<=SETTLE;txn_fill<=0;end
                    end else begin
                        bypass_valid<=1;bypass_addr<=pending_addr[23:3];bypass_data<=memory_rdata;state<=SETTLE;
                    end
                end
                // Return to IDLE only after a lookup edge has seen the fill's
                // tag or the bypass token, so a request cannot repeat.
                SETTLE:if(lookup_tick)state<=IDLE;
                default:begin state<=IDLE;fault<=1;end
            endcase
            // Sweeping never drops posted writes; it only clears tags and
            // spoils a fill in flight.
            if(invalidate)begin sweep<=0;bypass_valid<=0;fill_ok<=0;end
            if(fault)begin rs_valid<=0;wb_head<=0;wb_tail<=0;wb_count<=0;end
        end
    end
endmodule
