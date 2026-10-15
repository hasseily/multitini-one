// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// 32 KiB direct-mapped read cache with synchronous BRAM lookup. CPU and HDD
// DMA share this coherent path. Writes update hits and complete only after
// DDR acknowledgement; an unresolved access holds every bus side effect.
module multitini_apple_iigs_cache #(
    parameter integer INDEX_BITS=12
)(
    input wire clk,reset,invalidate,lookup_tick,request_tick,
    input wire [23:0] bus_addr,
    input wire [7:0] bus_wdata,
    input wire bus_valid,bus_write,bus_commit,write_launch,
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
    output wire memory_valid,
    input wire memory_ready,
    input wire [63:0] memory_rdata,
    input wire memory_rvalid,memory_fault
);
    localparam integer ENTRIES=1<<INDEX_BITS;
    localparam integer TAG_BITS=21-INDEX_BITS;
    (* ram_style="block" *) reg [63:0] data[0:ENTRIES-1];
    // Tags are narrow; distributed RAM preserves the full data cache
    // while leaving scarce BRAM available to the IIgs video/audio/floppies.
    (* ram_style="distributed" *) reg [TAG_BITS:0] tags[0:ENTRIES-1];
    reg [63:0] lookup_data;
    reg [TAG_BITS:0] lookup_tag;
    reg [23:3] lookup_addr;
    reg [INDEX_BITS:0] sweep;
    reg [2:0] state;
    reg owner_loader,pending_write;
    reg [23:0] pending_addr;
    reg [7:0] pending_byte;
    reg completed_write;
    reg [23:0] completed_addr;
    reg [7:0] completed_byte;
    reg bypass_valid;
    reg [23:3] bypass_addr;
    reg [63:0] bypass_data;
    reg bypass_valid_q;
    reg [23:3] bypass_addr_q;
    reg [63:0] bypass_data_q;
    localparam IDLE=0,SEND=1,WAIT=2,SETTLE=3;
    wire initialized=sweep[INDEX_BITS];
    wire hit=initialized && lookup_addr==bus_addr[23:3] &&
        lookup_tag[TAG_BITS] && lookup_tag[TAG_BITS-1:0]==bus_addr[23:INDEX_BITS+3];
    // A primary-clock CPU may consume adjacent bytes on consecutive edges.
    // Retirement invalidates the live token immediately; the staged token
    // alone would survive one more lookup edge and authorize a second read.
    wire bypass_hit=bypass_valid && bypass_valid_q && bypass_addr_q==bus_addr[23:3];
    // CPU data can depend on a peripheral's phi2-qualified read mirror.
    // Completion keys the held bus address; comparing live write data here
    // would feed a combinational loop through the machine's stall gate.
    wire write_done=completed_write && completed_addr==bus_addr;
    assign bus_rdata=bypass ? bypass_data_q[bus_addr[2:0]*8+:8] : lookup_data[bus_addr[2:0]*8+:8];
    assign bus_rword=bypass ? bypass_data_q : lookup_data;
    assign stall=!initialized || fault || (bus_valid && (bus_write ? !write_done :
        !(bypass ? bypass_hit : hit))) || loader_valid || owner_loader;
    assign memory_valid=state==SEND;
    assign loader_ready=state==IDLE && initialized;
    assign loader_response=state==WAIT && owner_loader && memory_rvalid;
    assign loader_rdata=memory_rdata;
    wire [63:0] merged_write=(lookup_data & ~(64'hff<<(pending_addr[2:0]*8))) |
        ({56'd0,pending_byte}<<(pending_addr[2:0]*8));
    // Simple dual-port block RAM: one registered lookup, one fill/write port.
    always @(posedge clk)begin
        // CPU changes its bus on primary phase3. The phase1 lookup gives
        // that combinational bus two primary periods to settle, then gives
        // the CPU two periods for RAM/decode before the next phase3 commit.
        if(lookup_tick)begin
            lookup_data<=data[bus_addr[INDEX_BITS+2:3]];
            lookup_tag<=tags[bus_addr[INDEX_BITS+2:3]];
            lookup_addr<=bus_addr[23:3];
            bypass_data_q<=bypass_data;bypass_addr_q<=bypass_addr;bypass_valid_q<=bypass_valid;
        end
        if(!initialized)tags[sweep[INDEX_BITS-1:0]]<=0;
        else if(state==WAIT && !owner_loader && memory_rvalid && !memory_fault)begin
            if(!pending_write)begin
                data[pending_addr[INDEX_BITS+2:3]]<=memory_rdata;
                tags[pending_addr[INDEX_BITS+2:3]]<={1'b1,pending_addr[23:INDEX_BITS+3]};
            end else if(lookup_tag[TAG_BITS] && lookup_tag[TAG_BITS-1:0]==pending_addr[23:INDEX_BITS+3] && lookup_addr==pending_addr[23:3])
                // A full-word merge gives Vivado one uniform write port;
                // variable-lane array writes fragment this into dozens of
                // block RAMs despite the small byte capacity.
                data[pending_addr[INDEX_BITS+2:3]]<=merged_write;
        end
    end
    always @(posedge clk)begin
        if(reset)begin
            state<=IDLE;sweep<=0;fault<=0;owner_loader<=0;
            completed_write<=0;bypass_valid<=0;hits<=0;misses<=0;wait_cycles<=0;
            memory_addr<=0;memory_write<=0;memory_wdata<=0;memory_wstrb<=0;
            pending_addr<=0;pending_byte<=0;pending_write<=0;
            completed_addr<=0;completed_byte<=0;bypass_addr<=0;bypass_data<=0;
        end else begin
            if(!initialized)sweep<=sweep+1'b1;
            if(invalidate)begin sweep<=0;completed_write<=0;bypass_valid<=0;end
            if(memory_fault)fault<=1;
            if(bus_commit)begin
                completed_write<=0;bypass_valid<=0;
                if(!bus_write && hit)hits<=hits+1'b1;
            end
            if(stall && bus_valid)wait_cycles<=wait_cycles+1'b1;
            case(state)
                IDLE:if(initialized && !fault)begin
                    if(loader_valid)begin
                        owner_loader<=1;memory_addr<=loader_addr;
                        memory_write<=loader_write;memory_wdata<=loader_wdata;memory_wstrb<=loader_wstrb;
                        state<=SEND;bypass_valid<=0;
                    end else if(request_tick && lookup_addr==bus_addr[23:3] && bus_valid &&
                        (bus_write ? (!write_done && write_launch) : !(bypass ? bypass_hit : hit)))begin
                        owner_loader<=0;pending_addr<=bus_addr;pending_byte<=bus_wdata;pending_write<=bus_write;
                        memory_addr<={bus_addr[23:3],3'b000};memory_write<=bus_write;
                        memory_wdata<={56'd0,bus_wdata}<<(bus_addr[2:0]*8);
                        memory_wstrb<=bus_write ? (8'b1<<bus_addr[2:0]) : 0;
                        state<=SEND;misses<=misses+1'b1;
                    end
                end
                SEND:if(memory_ready)state<=WAIT;
                WAIT:if(memory_rvalid)begin
                    state<=owner_loader ? IDLE : SETTLE;owner_loader<=0;
                    if(!owner_loader)begin
                        if(pending_write)begin
                            // Publish only on phase1, before both the parent's
                            // commit decision and the generated CPU edge on
                            // phase3. An arbitrary DDR response on phase3 can
                            // otherwise release RDY after the parent sampled
                            // it, advancing the CPU without retiring this write.
                            if(lookup_tick)begin
                                completed_write<=1;completed_addr<=pending_addr;completed_byte<=pending_byte;
                                state<=IDLE;
                            end
                        end
                        else begin bypass_valid<=1;bypass_addr<=pending_addr[23:3];bypass_data<=memory_rdata;end
                    end
                end
                SETTLE:if(lookup_tick)begin
                    if(pending_write)begin
                        completed_write<=1;completed_addr<=pending_addr;completed_byte<=pending_byte;
                    end
                    state<=IDLE;
                end
                default:begin state<=IDLE;fault<=1;end
            endcase
        end
    end
endmodule
