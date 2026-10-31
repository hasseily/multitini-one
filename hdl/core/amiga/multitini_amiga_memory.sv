// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Coherent write-through store for the two native Minimig memory ports.
// Chip cycles begin with C1, as in upstream sdram_ctrl. A missing line stops
// the complete virtual machine before its next native edge; AXI and the host
// remain alive. All byte lanes use Amiga big-endian word order explicitly.
// This preserves virtual bus ordering, not a claim of real-time throughput.
module multitini_amiga_memory #(
    parameter integer INDEX_BITS=6
)(
    input wire clk,reset,chip_c1,
    input wire [23:1] chip_addr,
    input wire [15:0] chip_wdata,chip_wdata2,
    input wire chip_oe_n,chip_we_n,chip_ub_n,chip_lb_n,chip_long,
    output reg [15:0] chip_rdata,chip_rdata2,
    output reg [47:0] chip_rdata48,
    input wire cpu_req,input wire [28:1] cpu_addr,
    input wire [15:0] cpu_wdata,input wire [1:0] cpu_state,
    input wire cpu_ub_n,cpu_lb_n,cpu_long,input wire [15:0] cpu_wdata2,
    output reg [15:0] cpu_rdata,cpu_rdata2,output wire cpu_ready,
    input wire load_valid,input wire [23:0] load_addr,
    input wire [7:0] load_data,output wire load_ready,output reg load_done,
    output wire run_enable,busy,output reg fault,
    output reg [23:0] memory_addr,output reg memory_write,
    output reg [63:0] memory_wdata,output reg [7:0] memory_wstrb,
    output wire memory_valid,input wire memory_ready,
    input wire [63:0] memory_rdata,input wire memory_rvalid,memory_fault
);
    localparam integer LINES=1<<INDEX_BITS;
    localparam integer TAG_BITS=21-INDEX_BITS;
    localparam [1:0] IDLE=0,SEND=1,WAIT=2;
    localparam [1:0] CHIP=0,CPU=1,LOAD=2;
    (* ram_style="distributed" *) reg [63:0] words[0:LINES-1];
    reg [TAG_BITS-1:0] tags[0:LINES-1];
    reg [LINES-1:0] valid;
    reg [1:0] state,owner;
    reg chip_done,cpu_done,chip_seen;
    reg [64:0] cpu_key;
    wire cpu_write=cpu_state==2'b11;
    wire [15:0] key_data=cpu_write ?
        {cpu_ub_n ? 8'd0 : cpu_wdata[15:8],cpu_lb_n ? 8'd0 : cpu_wdata[7:0]} : 16'd0;
    wire [15:0] key_data2=cpu_write && cpu_long ? cpu_wdata2 : 16'd0;
    wire [64:0] current_cpu_key={cpu_addr,key_data,cpu_state,cpu_ub_n,cpu_lb_n,cpu_long,key_data2};
    reg [1:0] word_index;
    reg [63:0] cached_word;
    wire chip_need=chip_c1 && (!chip_oe_n || !chip_we_n) && !chip_done;
    wire cpu_need=cpu_req && (cpu_state!=2'b11 || !cpu_ub_n || !cpu_lb_n) &&
        (!cpu_done || cpu_key!=current_cpu_key);
    // CPU-only misses use the native DTACK/clkena wait path. CIA, video and
    // Paula continue at their normal rate. Only a chipset deadline, preload
    // or fatal memory condition can stop the shared virtual timeline.
    // C1 publishes a chip request just after the shared service edge. The
    // next service edge performs its cache lookup while the native machine
    // advances one slot. A hit is ready before the following consumer edge;
    // a miss (or a CPU request still occupying DDR) holds that edge.
    assign run_enable=!fault && !(chip_need && chip_seen) && !load_valid && (state==IDLE || owner==CPU);
    assign busy=state!=IDLE || chip_need || cpu_need || load_valid;
    assign cpu_ready=cpu_done && cpu_key==current_cpu_key;
    assign load_ready=state==IDLE && !chip_need && !cpu_need && !fault;
    assign memory_valid=state==SEND;
    reg [23:0] request_addr;
    reg request_write;
    reg [63:0] request_data;
    reg [7:0] request_mask;
    reg [1:0] request_owner;
    reg request_valid;
    wire [INDEX_BITS-1:0] index=request_addr[INDEX_BITS+2:3];
    wire hit=valid[index] && tags[index]==request_addr[23:INDEX_BITS+3];
    function automatic [15:0] word_at(input [63:0] line,input [1:0] position);
        word_at={line[(position*16)+:8],line[(position*16+8)+:8]};
    endfunction
    function automatic [63:0] merge(input [63:0] old_word,new_word,input [7:0] mask);
        integer n;
        begin merge=old_word;for(n=0;n<8;n=n+1)if(mask[n])merge[n*8+:8]=new_word[n*8+:8];end
    endfunction
    always @* begin
        request_valid=0;request_addr=0;request_write=0;request_data=0;request_mask=0;request_owner=CHIP;
        if(chip_need)begin
            request_valid=1;request_addr={1'b0,chip_addr[22:1],1'b0};
            request_write=!chip_we_n;
            request_data[chip_addr[2:1]*16+:16]={chip_wdata[7:0],chip_wdata[15:8]};
            request_mask[chip_addr[2:1]*2+:2]={!chip_lb_n,!chip_ub_n};
            if(chip_long)begin
                request_data[((chip_addr[2:1]+2'd1)&2'b11)*16+:16]={chip_wdata2[7:0],chip_wdata2[15:8]};
                request_mask[((chip_addr[2:1]+2'd1)&2'b11)*2+:2]=2'b11;
            end
        end else if(cpu_need)begin
            request_valid=1;request_owner=CPU;
            request_addr={|cpu_addr[28:26],cpu_addr[22:1],1'b0};
            request_write=cpu_state==2'b11;
            request_data[cpu_addr[2:1]*16+:16]={cpu_wdata[7:0],cpu_wdata[15:8]};
            request_mask[cpu_addr[2:1]*2+:2]={!cpu_lb_n,!cpu_ub_n};
            if(cpu_long)begin
                request_data[((cpu_addr[2:1]+2'd1)&2'b11)*16+:16]={cpu_wdata2[7:0],cpu_wdata2[15:8]};
                request_mask[((cpu_addr[2:1]+2'd1)&2'b11)*2+:2]=2'b11;
            end
        end else if(load_valid)begin
            request_valid=1;request_owner=LOAD;request_addr=load_addr;request_write=1;
            request_data[load_addr[2:0]*8+:8]=load_data;request_mask[load_addr[2:0]]=1;
        end
    end
    task automatic publish(input [1:0] who,input [63:0] line,input [1:0] pos);
        begin
            if(who==CHIP)begin
                chip_rdata<=word_at(line,pos);chip_rdata2<=word_at(line,pos+2'd1);
                chip_rdata48<={word_at(line,pos+2'd1),word_at(line,pos+2'd2),word_at(line,pos+2'd3)};
                chip_done<=1;
            end else if(who==CPU)begin cpu_rdata<=word_at(line,pos);cpu_rdata2<=word_at(line,pos+2'd1);cpu_done<=1;end
            else load_done<=1;
        end
    endtask
    always @(posedge clk)begin
        load_done<=0;
        if(reset)begin
            state<=IDLE;valid<=0;chip_done<=0;cpu_done<=0;chip_seen<=0;fault<=0;
            memory_addr<=0;memory_write<=0;memory_wdata<=0;memory_wstrb<=0;
            chip_rdata<=0;chip_rdata2<=0;chip_rdata48<=0;cpu_rdata<=0;cpu_rdata2<=0;
            owner<=CHIP;word_index<=0;cached_word<=0;cpu_key<=0;
        end else begin
            if(!chip_c1)begin chip_done<=0;chip_seen<=0;end
            else if(chip_need)chip_seen<=1;
            if(!cpu_req)cpu_done<=0;
            if(memory_fault)fault<=1;
            case(state)
                IDLE:if(request_valid && !fault)begin
                    if(request_owner==CPU)cpu_key<=current_cpu_key;
                    // Native DMA and preloads share the CPU's physical store.
                    // A still-asserted CPU request must see their new bytes.
                    if(request_write && request_owner!=CPU)cpu_done<=0;
                    if(!request_write && hit)publish(request_owner,words[index],request_addr[2:1]);
                    else begin
                        memory_addr<={request_addr[23:3],3'b0};memory_write<=request_write;
                        memory_wdata<=request_data;memory_wstrb<=request_mask;
                        owner<=request_owner;word_index<=request_addr[2:1];
                        cached_word<=hit ? words[index] : 64'd0;
                        if(request_write && hit)words[index]<=merge(words[index],request_data,request_mask);
                        state<=SEND;
                    end
                end
                SEND:if(memory_ready)state<=WAIT;
                WAIT:if(memory_rvalid)begin
                    if(!memory_write)begin
                        words[memory_addr[INDEX_BITS+2:3]]<=memory_rdata;
                        tags[memory_addr[INDEX_BITS+2:3]]<=memory_addr[23:INDEX_BITS+3];
                        valid[memory_addr[INDEX_BITS+2:3]]<=1;
                    end
                    publish(owner,memory_write ? merge(cached_word,memory_wdata,memory_wstrb) : memory_rdata,word_index);
                    state<=IDLE;
                end
                default:state<=IDLE;
            endcase
        end
    end
endmodule
