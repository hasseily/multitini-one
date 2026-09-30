// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// One outstanding demand plus a next-line prefetch over the existing board
// memory service. A response is never inferred from latency. Accepted PSRAM
// operations drain across cartridge invalidation; their old data is discarded.
module multitini_genesis_rom_cache #(
    parameter integer LINES=128
)(
    input wire clk,resetn,invalidate,loading,
    input wire [23:0] read_addr,
    input wire read_valid,output wire read_ready,
    output wire cached,output wire [15:0] cached_data,
    output reg [15:0] read_data,output reg read_response,
    input wire [23:0] write_addr,input wire [63:0] write_data,
    input wire [7:0] write_strobe,input wire write_valid,
    output wire write_ready,output reg write_response,
    output reg [23:0] memory_addr,output reg memory_write,
    output reg [63:0] memory_wdata,output reg [7:0] memory_wstrb,
    output reg memory_valid,input wire memory_ready,
    input wire [63:0] memory_rdata,input wire memory_rvalid,memory_fault,
    output reg fault,output reg [31:0] misses,wait_cycles
);
    localparam integer INDEX=$clog2(LINES),TAG=24-3-INDEX;
    (* ram_style="distributed" *) reg [63:0] data[0:LINES-1];
    reg [TAG-1:0] tags[0:LINES-1];
    reg [LINES-1:0] valid;
    reg pending;
    reg [23:0] demand;
    reg [23:0] prefetch;
    reg prefetch_valid,discard;
    reg [1:0] state;
    localparam IDLE=0,ISSUE=1,WAIT=2;
    wire [INDEX-1:0] demand_index=demand[INDEX+2:3];
    wire [INDEX-1:0] prefetch_index=prefetch[INDEX+2:3];
    wire hit=valid[demand_index] && tags[demand_index]==demand[23:INDEX+3];
    wire prefetch_hit=valid[prefetch_index] && tags[prefetch_index]==prefetch[23:INDEX+3];
    wire [INDEX-1:0] lookup_index=read_addr[INDEX+2:3];
    assign cached=!invalidate && !loading && !fault && !read_addr[0] &&
        read_addr<24'h800000 && valid[lookup_index] && tags[lookup_index]==read_addr[23:INDEX+3];
    assign cached_data={data[lookup_index][read_addr[2:0]*8+:8],
                        data[lookup_index][(read_addr[2:0]+1)*8+:8]};
    assign read_ready=!pending && !loading && !invalidate && !fault;
    assign write_ready=state==IDLE && !memory_valid && !pending && !fault;
    always @(posedge clk) begin
        read_response<=0;write_response<=0;
        if(!resetn)begin
            valid<=0;pending<=0;demand<=0;prefetch<=0;prefetch_valid<=0;
            state<=IDLE;memory_valid<=0;memory_addr<=0;memory_write<=0;
            memory_wdata<=0;memory_wstrb<=0;read_data<=0;discard<=0;
            fault<=0;misses<=0;wait_cycles<=0;
        end else begin
            if(memory_fault)fault<=1;
            if(pending)wait_cycles<=wait_cycles+1'b1;
            if(read_valid && read_ready)begin
                pending<=1;demand<=read_addr;
                if(read_addr[0] || read_addr>=24'h800000)begin
                    fault<=1;pending<=0;
                end
            end
            if(pending && hit && !invalidate && !loading)begin
                read_data<={data[demand_index][demand[2:0]*8+:8],
                            data[demand_index][(demand[2:0]+1)*8+:8]};
                read_response<=1;pending<=0;
                prefetch<={demand[23:3]+1'b1,3'b000};
                prefetch_valid<=demand[23:3] < (24'h800000/8-1);
            end
            case(state)
            IDLE:begin
                if(write_valid && write_ready)begin
                    memory_addr<=write_addr;memory_wdata<=write_data;memory_wstrb<=write_strobe;
                    memory_write<=1;memory_valid<=1;discard<=0;state<=ISSUE;
                    valid[write_addr[INDEX+2:3]]<=0;
                    if(write_addr[2:0]!=0 || write_addr>=24'h800000 || write_strobe==0)begin
                        memory_valid<=0;fault<=1;state<=IDLE;
                    end
                end else if(!loading && !invalidate && !fault &&
                             ((pending && !hit) || (!pending && prefetch_valid && !prefetch_hit)))begin
                    memory_addr<={pending ? demand[23:3] : prefetch[23:3],3'b000};
                    memory_write<=0;memory_wdata<=0;memory_wstrb<=0;
                    memory_valid<=1;discard<=0;state<=ISSUE;
                    if(pending)misses<=misses+1'b1;
                    else prefetch_valid<=0;
                end
            end
            ISSUE:if(memory_valid && memory_ready)begin memory_valid<=0;state<=WAIT;end
            WAIT:if(memory_rvalid)begin
                if(memory_write)write_response<=1;
                else if(!discard && !loading && !invalidate)begin
                    data[memory_addr[INDEX+2:3]]<=memory_rdata;
                    tags[memory_addr[INDEX+2:3]]<=memory_addr[23:INDEX+3];
                    valid[memory_addr[INDEX+2:3]]<=1;
                end
                state<=IDLE;
            end
            default:begin fault<=1;memory_valid<=0;end
            endcase
            if(invalidate)begin valid<=0;pending<=0;prefetch_valid<=0;discard<=1;end
        end
    end
    initial if(LINES<2 || (LINES & (LINES-1))!=0)$error("ROM cache lines must be a power of two");
endmodule
