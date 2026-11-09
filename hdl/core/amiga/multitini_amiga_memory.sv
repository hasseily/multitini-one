// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Coherent store for the two native Minimig memory ports: a direct-mapped
// cache of 32-byte lines (32 KiB by default) with posted, merging writes.
//
// Lookup. Chip, then CPU, then loader requests are selected on each service
// edge, as before. Tags are asynchronous distributed RAM, so a chip or CPU hit
// is decided on its lookup edge: chip_done/cpu_done register there and
// run_enable never depends on the tag compare. Line data is synchronous block
// RAM read on that same edge. Its output drives the requester's read data for
// one service period and is then held in a publish register, so a hit is
// still valid before the next native edge, as with the former LUT cache.
//
// Writes. A chip or CPU write retires on its lookup edge when the write buffer
// has room; DDR acknowledgement is no longer awaited. On the next edge the
// retire slot merges into the youngest buffered entry for the same 64-bit word
// or appends an entry, and updates a resident line (write-update, no
// write-allocate). The buffer drains in order, one strobed 64-bit beat each.
//
// Ordering. AXI does not order reads against writes. A read miss starts its
// 4-beat line fill only when no buffered or retiring write touches that line;
// otherwise the buffer drains first. No write is accepted while a fill is in
// flight, so the fill owns the single block-RAM write port and its line. A
// lookup on the edge where the retire slot writes the same word is not a hit.
// The loader writes DDR directly once every posted write has drained, and
// invalidates the affected line.
//
// Deadlines. A chip request that is neither a hit nor an accepted write holds
// the complete virtual machine from the edge after its first lookup, as
// before. CPU-only misses use the CPU readiness path. A fill publishes the
// requested word on the beat that carries it; the native machine may resume
// while the remaining beats arrive. Byte lanes use Amiga big-endian order.
module multitini_amiga_memory #(
    parameter integer INDEX_BITS=10,
    parameter integer WB_BITS=2,
    parameter bit USE_AP68040=1'b0
)(
    input wire clk,reset,chip_c1,
    input wire [23:1] chip_addr,
    input wire [15:0] chip_wdata,chip_wdata2,
    input wire chip_oe_n,chip_we_n,chip_ub_n,chip_lb_n,chip_long,
    output wire [15:0] chip_rdata,chip_rdata2,
    output wire [47:0] chip_rdata48,
    input wire cpu_req,input wire [28:1] cpu_addr,
    input wire [15:0] cpu_wdata,input wire [1:0] cpu_state,
    input wire cpu_ub_n,cpu_lb_n,cpu_long,input wire [15:0] cpu_wdata2,
    output wire [15:0] cpu_rdata,cpu_rdata2,output wire cpu_ready,
    input wire cpu_line,cpu_line_stop,
    output wire cpu_line_ready,output wire [127:0] cpu_line_data,
    input wire load_valid,input wire [23:0] load_addr,
    input wire [7:0] load_data,output wire load_ready,output reg load_done,
    output wire run_enable,busy,output reg fault,
    output wire [7:0] monitor,
    output reg [24:0] memory_addr,output reg memory_write,
    output reg [63:0] memory_wdata,output reg [7:0] memory_wstrb,
    output reg memory_burst,
    output wire memory_valid,input wire memory_ready,
    input wire [63:0] memory_rdata,input wire memory_rvalid,memory_fault
);
    localparam integer LINES=1<<INDEX_BITS;
    localparam integer WORD_BITS=INDEX_BITS+2;
    localparam integer WORDS=1<<WORD_BITS;
    localparam integer TAG_BITS=20-INDEX_BITS;
    localparam integer WB_DEPTH=1<<WB_BITS;
    localparam [1:0] IDLE=0,SEND=1,WAIT=2;
    // Request owners; DRAIN marks a posted-write transaction.
    localparam [1:0] CHIP=0,CPU=1,LOAD=2,DRAIN=3;

    (* ram_style="block" *) reg [63:0] data[0:WORDS-1];
    (* ram_style="distributed" *) reg [TAG_BITS:0] tags[0:LINES-1];
    reg [63:0] bram_q;

    reg [1:0] state,owner;
    reg chip_done,cpu_done,chip_seen;
    reg [64:0] cpu_key;
    wire cpu_write=cpu_state==2'b11;
    wire [15:0] key_data=cpu_write ?
        {cpu_ub_n ? 8'd0 : cpu_wdata[15:8],cpu_lb_n ? 8'd0 : cpu_wdata[7:0]} : 16'd0;
    wire [15:0] key_data2=cpu_write && cpu_long ? cpu_wdata2 : 16'd0;
    wire [64:0] current_cpu_key={cpu_addr,key_data,cpu_state,cpu_ub_n,cpu_lb_n,cpu_long,key_data2};
    wire chip_need=chip_c1 && (!chip_oe_n || !chip_we_n) && !chip_done;


    // Requests in priority order: chip, CPU, loader. Continuous assignments
    // also evaluate at time zero in simulation, before any input changes.
    function automatic [63:0] place_data(input [1:0] position,input [15:0] word,word2,input long);
        begin
            place_data=0;
            place_data[position*16+:16]={word[7:0],word[15:8]};
            if(long)place_data[((position+2'd1)&2'b11)*16+:16]={word2[7:0],word2[15:8]};
        end
    endfunction
    function automatic [7:0] place_mask(input [1:0] position,input ub_n,lb_n,long);
        begin
            place_mask=0;
            place_mask[position*2+:2]={!lb_n,!ub_n};
            if(long)place_mask[((position+2'd1)&2'b11)*2+:2]=2'b11;
        end
    endfunction
    // The 040 wrapper marks its 16 MiB window separately from the legacy
    // banks. Keep the bank in all cache tags and posted-write addresses.
    wire extra_fast=USE_AP68040 && cpu_addr[28:24]==5'b01000;
    wire line_selected=cpu_req && cpu_line && extra_fast && !cpu_line_stop;
    wire [24:0] cpu_memory_addr=extra_fast ? {1'b1,cpu_addr[23:1],1'b0} :
        {1'b0,|cpu_addr[28:26],cpu_addr[22:1],1'b0};


    // Tag invalidation sweep after reset; nothing is looked up until done.
    reg [INDEX_BITS:0] sweep;
    wire initialized=sweep[INDEX_BITS];
    // Line fill in flight.
    reg [1:0] beat,fill_word,fill_pos;
    reg [24:5] fill_line;
    reg fill_ok,drain_cpu;
    // AP68040 Zorro III streaming read buffer. Adjacent DDR lines use
    // different slots. Register the selected 16-byte reply before sending
    // it to the gated native clock and retain it until the request retires.
    reg line_fill,speculative;
    reg [255:0] line_store[0:1];
    reg [24:5] line_tag_full[0:1];
    reg [3:0] line_present[0:1];
    wire [24:5] cpu_line_tag=cpu_memory_addr[24:5];
    wire [1:0] cpu_line_first_beat={cpu_memory_addr[4],1'b0};
    wire line_slot=cpu_line_tag[5];
    wire [1:0] cpu_line_present=line_present[line_slot][cpu_line_first_beat+:2];
    reg line_seen,prefetch_pending;
    reg [24:5] last_cpu_line,prefetch_tag;
    wire new_cpu_line=line_selected && (!line_seen || last_cpu_line!=cpu_line_tag);
    wire prefetch_slot=prefetch_tag[5];
    wire prefetch_resident=line_tag_full[prefetch_slot]==prefetch_tag && (&line_present[prefetch_slot]);
    wire [63:0] cpu_line_first=line_store[line_slot][cpu_line_first_beat*64+:64];
    wire [63:0] cpu_line_second=line_store[line_slot][(cpu_line_first_beat+2'd1)*64+:64];
    reg line_reply_valid;
    reg [24:4] line_reply_tag;
    reg [127:0] line_reply_data;
    wire line_buffer_hit=line_tag_full[line_slot]==cpu_line_tag && (&cpu_line_present);
    wire line_reply_hit=line_reply_valid && line_reply_tag==cpu_memory_addr[24:4];
    assign cpu_line_ready=line_selected && line_reply_hit && !fault;
    assign cpu_line_data=line_reply_data;
    wire cpu_need=cpu_req && !line_selected && (cpu_state!=2'b11 || !cpu_ub_n || !cpu_lb_n) &&
        (!cpu_done || cpu_key!=current_cpu_key);
    wire line_need=line_selected && !line_buffer_hit && !line_reply_hit;
    wire request_valid=chip_need || cpu_need || line_need || load_valid;
    wire [1:0] request_owner=chip_need ? CHIP : (cpu_need || line_need) ? CPU : load_valid ? LOAD : CHIP;
    wire [24:0] request_addr=chip_need ? {2'b00,chip_addr[22:1],1'b0} :
        (cpu_need || line_need) ? cpu_memory_addr : load_valid ? {1'b0,load_addr} : 25'd0;
    wire request_write=chip_need ? !chip_we_n : cpu_need ? cpu_state==2'b11 : line_need ? 1'b0 : load_valid;
    wire [63:0] request_data=chip_need ? place_data(chip_addr[2:1],chip_wdata,chip_wdata2,chip_long) :
        cpu_need ? place_data(cpu_addr[2:1],cpu_wdata,cpu_wdata2,cpu_long) :
        load_valid ? {56'd0,load_data}<<{load_addr[2:0],3'b000} : 64'd0;
    wire [7:0] request_mask=chip_need ? place_mask(chip_addr[2:1],chip_ub_n,chip_lb_n,chip_long) :
        cpu_need ? place_mask(cpu_addr[2:1],cpu_ub_n,cpu_lb_n,cpu_long) :
        load_valid ? 8'b1<<load_addr[2:0] : 8'd0;

    // Retire slot: one accepted write, merged and applied on the next edge.
    // Its payload loads every edge; only rs_valid records an acceptance.
    reg rs_valid,rs_cpu;
    reg [24:3] rs_addr;
    reg [63:0] rs_data;
    reg [7:0] rs_mask;
    // Posted write buffer: unlaunched entries only, oldest at wb_head.
    (* ram_style="registers" *) reg [24:3] wb_addr[0:WB_DEPTH-1];
    (* ram_style="registers" *) reg [63:0] wb_data[0:WB_DEPTH-1];
    (* ram_style="registers" *) reg [7:0] wb_strb[0:WB_DEPTH-1];
    reg [WB_DEPTH-1:0] wb_valid,wb_cpu;
    reg [WB_BITS-1:0] wb_head,wb_tail;
    reg [WB_BITS:0] wb_count;
    wire [WB_BITS-1:0] wb_young=wb_tail-1'b1;
    wire wb_empty=wb_count==0;
    wire wb_full=wb_count==WB_DEPTH;

    // Lookup of the selected chip or CPU request, every service edge.
    wire [INDEX_BITS-1:0] line_index=request_addr[INDEX_BITS+4:5];
    wire [TAG_BITS:0] line_tag=tags[line_index];
    wire tag_hit=line_tag[TAG_BITS] && line_tag[TAG_BITS-1:0]==request_addr[24:INDEX_BITS+5];
    wire rs_word=rs_valid && rs_addr==request_addr[24:3];
    wire lookup=initialized && !fault && request_valid && request_owner!=LOAD;
    wire hit=lookup && !request_write && tag_hit && !rs_word && (chip_need || !line_need);
    // Write acceptance uses registered state only: room in the buffer, with
    // the retire slot counted as an entry, and no line fill in flight.
    wire fill_busy=state!=IDLE && (owner==CHIP || owner==CPU) && !speculative;
    wire load_busy=state!=IDLE && owner==LOAD;
    wire wb_space=rs_valid ? wb_count<WB_DEPTH-1 : wb_count<WB_DEPTH;
    wire accept_write=lookup && request_write && wb_space && !fill_busy;
    wire [WB_DEPTH-1:0] wb_line_match;
    genvar g;
    generate for(g=0;g<WB_DEPTH;g=g+1)begin:g_wb_line
        assign wb_line_match[g]=wb_valid[g] && wb_addr[g][24:5]==request_addr[24:5];
    end endgenerate
    wire wb_line_hit=(|wb_line_match) || (rs_valid && rs_addr[24:5]==request_addr[24:5]);
    wire launch_fill=lookup && !request_write && !hit && state==IDLE && !wb_line_hit;
    wire launch_drain=state==IDLE && initialized && !fault && !wb_empty && !launch_fill;
    wire drained=state==IDLE && initialized && !fault && wb_empty && !rs_valid;
    assign load_ready=drained && !chip_need && !cpu_need && !line_need;
    wire launch_load=load_valid && load_ready;
    // The top bit selects the 16 MiB bank. An increment past its end clears
    // that bit. Start only with a drained write queue and no demand owner.
    wire launch_prefetch=USE_AP68040 && !cpu_line_stop && prefetch_pending &&
        !prefetch_resident && !new_cpu_line && prefetch_tag[24] && drained && !request_valid;
    wire line_invalidate=accept_write && request_addr[24] &&
        line_tag_full[request_addr[5]]==request_addr[24:5];
    wire line_cancel=cpu_line_stop || memory_fault ||
        (line_invalidate && fill_line==request_addr[24:5]);

    // One block-RAM write port: fill beats or the retire slot updating a
    // resident line. They never coincide: no write is accepted during a fill,
    // and a fill starts only from a read lookup, never with an acceptance.
    wire fill_beat=state==WAIT && (owner==CHIP || owner==CPU) && memory_rvalid;
    wire fill_last=fill_beat && beat==2'd3;
    // Prefetch owns only the read-only line buffer. A later chip write can
    // use the normal retire slot and BRAM port during a speculative burst.
    wire cache_beat=fill_beat && !speculative;
    wire cache_last=fill_last && !speculative;
    wire [TAG_BITS:0] rs_tag=tags[rs_addr[INDEX_BITS+4:5]];
    wire rs_update=rs_valid && initialized && !fault &&
        rs_tag[TAG_BITS] && rs_tag[TAG_BITS-1:0]==rs_addr[24:INDEX_BITS+5];
    wire bram_we=cache_beat || rs_update;
    wire [WORD_BITS-1:0] bram_waddr=cache_beat ? {fill_line[INDEX_BITS+4:5],beat} : rs_addr[WORD_BITS+2:3];
    wire [63:0] bram_wdata=cache_beat ? memory_rdata : rs_data;
    wire [7:0] bram_wbe=cache_beat ? 8'hff : rs_mask;
    // One tag write port: sweep, line invalidation on fill or loader launch,
    // and the completed fill. Launches occur only in IDLE, fill_last in WAIT.
    wire tag_we=!initialized || launch_fill || launch_load || cache_last;
    wire [INDEX_BITS-1:0] tag_waddr=!initialized ? sweep[INDEX_BITS-1:0] :
        cache_last ? fill_line[INDEX_BITS+4:5] : launch_load ? load_addr[INDEX_BITS+4:5] : line_index;
    wire [TAG_BITS:0] tag_wdata=initialized && cache_last && fill_ok && !memory_fault ?
        {1'b1,fill_line[24:INDEX_BITS+5]} : {(TAG_BITS+1){1'b0}};

    always @(posedge clk)if(hit)bram_q<=data[request_addr[WORD_BITS+2:3]];
    integer lane;
    always @(posedge clk)
        for(lane=0;lane<8;lane=lane+1)
            if(bram_we && bram_wbe[lane])data[bram_waddr][lane*8+:8]<=bram_wdata[lane*8+:8];
    always @(posedge clk)if(tag_we)tags[tag_waddr]<=tag_wdata;

    // Read data. A hit drives its requester from block RAM for one period,
    // then from the publish register loaded with the same word.
    reg chip_fresh,cpu_fresh;
    // A CPU fill's word is published only to the request that launched it.
    // Any later CPU completion, or a withdrawn request, retires that claim.
    reg cpu_fill_live;
    reg [1:0] chip_pos,cpu_pos;
    reg [15:0] chip_word_q,chip_word2_q,cpu_word_q,cpu_word2_q;
    reg [47:0] chip_word48_q;
    function automatic [15:0] word_at(input [63:0] line,input [1:0] position);
        word_at={line[(position*16)+:8],line[(position*16+8)+:8]};
    endfunction
    function automatic [47:0] words48(input [63:0] line,input [1:0] position);
        words48={word_at(line,position+2'd1),word_at(line,position+2'd2),word_at(line,position+2'd3)};
    endfunction
    assign chip_rdata=chip_fresh ? word_at(bram_q,chip_pos) : chip_word_q;
    assign chip_rdata2=chip_fresh ? word_at(bram_q,chip_pos+2'd1) : chip_word2_q;
    assign chip_rdata48=chip_fresh ? words48(bram_q,chip_pos) : chip_word48_q;
    assign cpu_rdata=cpu_fresh ? word_at(bram_q,cpu_pos) : cpu_word_q;
    assign cpu_rdata2=cpu_fresh ? word_at(bram_q,cpu_pos+2'd1) : cpu_word2_q;

    // CPU-only misses use the native DTACK/clkena wait path. CIA, video and
    // Paula continue at their normal rate. Only a chipset deadline, preload
    // or fatal memory condition can stop the shared virtual timeline.
    // C1 publishes a chip request just after the shared service edge. The
    // next service edge performs its lookup while the native machine advances
    // one slot. A hit or accepted write is ready before the following
    // consumer edge; any other outcome holds that edge.
    assign run_enable=!fault && !(chip_need && chip_seen) && !load_valid && !load_busy;
    assign busy=state!=IDLE || chip_need || cpu_need || line_need || load_valid || !wb_empty || rs_valid;
    assign cpu_ready=cpu_done && cpu_key==current_cpu_key;
    assign memory_valid=state==SEND;
    // Read-only pre-edge diagnostics: no feedback to request/ready/arbitration.
    // [0]/[1] count CPU reads served (hit or fill start) and CPU writes
    // accepted. [2..5] split DDR occupancy into CPU and chip classes: a fill
    // belongs to its requester, a posted write to CPU when it holds CPU bytes.
    // [6] counts CPU line fills and [7] posted DDR writes carrying CPU bytes.
    wire cpu_class=owner==CPU || (owner==DRAIN && drain_cpu);
    wire chip_class=owner==CHIP || (owner==DRAIN && !drain_cpu);
    assign monitor[0]=request_owner==CPU && (hit || launch_fill);
    assign monitor[1]=request_owner==CPU && accept_write;
    assign monitor[2]=state==SEND && cpu_class;
    assign monitor[3]=state==WAIT && cpu_class;
    assign monitor[4]=state==SEND && chip_class;
    assign monitor[5]=state==WAIT && chip_class;
    assign monitor[6]=state==SEND && memory_ready && owner==CPU;
    assign monitor[7]=state==SEND && memory_ready && owner==DRAIN && drain_cpu;

    wire merge=rs_valid && !wb_empty && wb_addr[wb_young]==rs_addr && !(launch_drain && wb_count==1);
    wire push=rs_valid && !merge;
    integer m;
    always @(posedge clk)begin
        load_done<=0;
        if(reset)begin
            state<=IDLE;owner<=CHIP;sweep<=0;chip_done<=0;cpu_done<=0;chip_seen<=0;fault<=0;
            memory_addr<=0;memory_write<=0;memory_wdata<=0;memory_wstrb<=0;memory_burst<=0;
            cpu_key<=0;beat<=0;fill_word<=0;fill_pos<=0;fill_line<=0;fill_ok<=0;drain_cpu<=0;
            line_fill<=0;speculative<=0;line_seen<=0;prefetch_pending<=0;last_cpu_line<=0;prefetch_tag<=0;
            line_reply_valid<=0;line_reply_tag<=0;line_reply_data<=0;
            for(integer slot=0;slot<2;slot=slot+1)begin
                line_store[slot]<=0;line_tag_full[slot]<=0;line_present[slot]<=0;
            end
            chip_fresh<=0;cpu_fresh<=0;cpu_fill_live<=0;chip_pos<=0;cpu_pos<=0;
            chip_word_q<=0;chip_word2_q<=0;chip_word48_q<=0;cpu_word_q<=0;cpu_word2_q<=0;
            rs_valid<=0;rs_cpu<=0;rs_addr<=0;rs_data<=0;rs_mask<=0;
            wb_valid<=0;wb_cpu<=0;wb_head<=0;wb_tail<=0;wb_count<=0;
        end else begin
            if(!initialized)sweep<=sweep+1'b1;
            if(!chip_c1)begin chip_done<=0;chip_seen<=0;end
            else if(chip_need)chip_seen<=1;
            if(!cpu_req)begin cpu_done<=0;cpu_fill_live<=0;end
            if(memory_fault)begin fault<=1;fill_ok<=0;end
            if(!line_selected || fault)line_reply_valid<=0;
            else if(!line_reply_hit && line_buffer_hit)begin
                line_reply_valid<=1;
                line_reply_tag<=cpu_memory_addr[24:4];
                line_reply_data<={word_at(cpu_line_first,0),word_at(cpu_line_first,1),
                                  word_at(cpu_line_first,2),word_at(cpu_line_first,3),
                                  word_at(cpu_line_second,0),word_at(cpu_line_second,1),
                                  word_at(cpu_line_second,2),word_at(cpu_line_second,3)};
            end
            if(cpu_line_stop || memory_fault)begin
                line_seen<=0;prefetch_pending<=0;line_present[0]<=0;line_present[1]<=0;
            end else if(new_cpu_line)begin
                prefetch_pending<=line_seen && cpu_line_tag==last_cpu_line+1'b1;
                prefetch_tag<=cpu_line_tag+1'b1;last_cpu_line<=cpu_line_tag;line_seen<=1;
            end else if(launch_prefetch || prefetch_resident)prefetch_pending<=0;
            if(line_invalidate)line_present[request_addr[5]]<=0;
            // Let the shared AXI request drain, but discard unpublished
            // bytes if reset/pause or a newer write supersedes the line.
            if(line_cancel)line_fill<=0;
            // A hit's block-RAM word becomes the held value one edge later.
            if(chip_fresh)begin
                chip_word_q<=word_at(bram_q,chip_pos);chip_word2_q<=word_at(bram_q,chip_pos+2'd1);
                chip_word48_q<=words48(bram_q,chip_pos);chip_fresh<=0;
            end
            if(cpu_fresh)begin
                cpu_word_q<=word_at(bram_q,cpu_pos);cpu_word2_q<=word_at(bram_q,cpu_pos+2'd1);cpu_fresh<=0;
            end
            // cpu_key names the request that cpu_done describes. It changes only
            // when a CPU request completes or starts its own fill, never on a
            // retried lookup, so an early-published word cannot acknowledge a
            // later request that is still waiting. A later CPU hit or write
            // retires cpu_fill_live, so an abandoned request's fill cannot
            // overwrite that newer request's data.
            if(hit)begin
                if(request_owner==CHIP)begin chip_fresh<=1;chip_pos<=request_addr[2:1];chip_done<=1;end
                else begin
                    cpu_fresh<=1;cpu_pos<=request_addr[2:1];cpu_done<=1;cpu_key<=current_cpu_key;cpu_fill_live<=0;
                end
            end
            if(accept_write)begin
                if(request_owner==CHIP)begin
                    chip_done<=1;
                    // Native DMA shares the CPU's physical store: a CPU read
                    // that is still asserted must look up the new bytes. A
                    // retired CPU write is never repeated after the DMA write.
                    if(cpu_state!=2'b11)cpu_done<=0;
                end else begin cpu_done<=1;cpu_key<=current_cpu_key;cpu_fill_live<=0;end
            end
            if(launch_fill && request_owner==CPU)begin cpu_done<=0;cpu_key<=current_cpu_key;cpu_fill_live<=!line_need;end
            rs_valid<=accept_write;rs_cpu<=request_owner==CPU;
            rs_addr<=request_addr[24:3];rs_data<=request_data;rs_mask<=request_mask;
            // Write buffer: pop on launch; the retire slot merges into the
            // youngest unlaunched entry for the same word, otherwise appends.
            if(merge)begin
                for(m=0;m<8;m=m+1)if(rs_mask[m])wb_data[wb_young][m*8+:8]<=rs_data[m*8+:8];
                wb_strb[wb_young]<=wb_strb[wb_young] | rs_mask;
                if(rs_cpu)wb_cpu[wb_young]<=1'b1;
            end
            // The acceptance rule makes overflow unreachable; if it ever
            // happened, hold the machine instead of losing a retired write.
            // A launch clears its slot before a push may reuse that slot.
            if(launch_drain)begin wb_valid[wb_head]<=1'b0;wb_head<=wb_head+1'b1;end
            if(push && wb_full && !launch_drain)fault<=1;
            else if(push)begin
                wb_addr[wb_tail]<=rs_addr;wb_data[wb_tail]<=rs_data;wb_strb[wb_tail]<=rs_mask;
                wb_cpu[wb_tail]<=rs_cpu;wb_valid[wb_tail]<=1'b1;wb_tail<=wb_tail+1'b1;
            end
            wb_count<=wb_count+(push && (!wb_full || launch_drain) ? 1'b1 : 1'b0)-(launch_drain ? 1'b1 : 1'b0);
            if(cache_beat && rs_update)fault<=1;
            case(state)
                IDLE:if(launch_fill)begin
                    owner<=request_owner;fill_line<=request_addr[24:5];fill_word<=request_addr[4:3];
                    fill_pos<=request_addr[2:1];beat<=0;fill_ok<=1;
                    memory_addr<={request_addr[24:5],5'b00000};memory_write<=0;memory_burst<=1;
                    memory_wdata<=0;memory_wstrb<=0;state<=SEND;
                    speculative<=0;
                    line_fill<=request_owner==CPU && line_need;
                    if(request_owner==CPU && line_need)begin
                        line_tag_full[request_addr[5]]<=request_addr[24:5];
                        line_present[request_addr[5]]<=0;
                    end
                end else if(launch_drain)begin
                    speculative<=0;
                    owner<=DRAIN;drain_cpu<=wb_cpu[wb_head];
                    memory_addr<={wb_addr[wb_head],3'b000};memory_write<=1;memory_burst<=0;
                    memory_wdata<=wb_data[wb_head];memory_wstrb<=wb_strb[wb_head];state<=SEND;
                end else if(launch_load)begin
                    speculative<=0;
                    owner<=LOAD;cpu_done<=0;
                    memory_addr<={1'b0,load_addr[23:3],3'b000};memory_write<=1;memory_burst<=0;
                    memory_wdata<=request_data;memory_wstrb<=request_mask;state<=SEND;
                end else if(launch_prefetch)begin
                    owner<=CPU;fill_line<=prefetch_tag;fill_word<=0;fill_pos<=0;beat<=0;fill_ok<=1;
                    memory_addr<={prefetch_tag,5'b00000};memory_write<=0;memory_burst<=1;
                    memory_wdata<=0;memory_wstrb<=0;state<=SEND;
                    line_fill<=1;speculative<=1;
                    line_tag_full[prefetch_slot]<=prefetch_tag;line_present[prefetch_slot]<=0;
                end
                SEND:if(memory_ready)state<=WAIT;
                WAIT:if(memory_rvalid)begin
                    if(owner==DRAIN)state<=IDLE;
                    else if(owner==LOAD)begin load_done<=1;state<=IDLE;end
                    else begin
                        beat<=beat+1'b1;
                        if(line_fill && fill_ok && !line_cancel)begin
                            line_store[fill_line[5]][beat*64+:64]<=memory_rdata;
                            line_present[fill_line[5]][beat]<=1;
                        end
                        if(beat==fill_word)begin
                            if(owner==CHIP)begin
                                chip_word_q<=word_at(memory_rdata,fill_pos);
                                chip_word2_q<=word_at(memory_rdata,fill_pos+2'd1);
                                chip_word48_q<=words48(memory_rdata,fill_pos);
                                chip_fresh<=0;chip_done<=1;
                            end else if(cpu_fill_live && cpu_req)begin
                                cpu_word_q<=word_at(memory_rdata,fill_pos);
                                cpu_word2_q<=word_at(memory_rdata,fill_pos+2'd1);
                                cpu_fresh<=0;cpu_done<=1;cpu_fill_live<=0;
                            end
                        end
                        if(beat==2'd3)begin state<=IDLE;speculative<=0;end
                    end
                end
                default:begin state<=IDLE;fault<=1;end
            endcase
            // After a sticky fault the machine is held until reset, so its
            // posted writes are dropped; busy then clears once any DDR
            // transaction already in flight has completed.
            if(fault)begin rs_valid<=0;wb_valid<=0;wb_head<=0;wb_tail<=0;wb_count<=0;end
        end
    end
endmodule
