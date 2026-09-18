// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Ordered native-frame processor on a dedicated 64-bit AXI3 HP port. All
// signals belong to clk (133 MHz); resetn is the full PL reset, not a job
// cancellation. An accepted address/beat is never cancelled by software.
//
// Four small distributed FIFOs decouple the independent read/write channels.
// Bursts are <=16 beats and never cross a row or 4 KiB boundary. History is
// dense 00BBGGRR, WIDTH*4 bytes/row, and is written BEFORE spatial processing.
// Its old value has already been read before the corresponding write queues.
// Source/output slots are bounded 1 MiB regions; no programmable DMA address
// exists. A bad response completes/drains the job with error, never publishes
// a partially acknowledged result. A wedged AXI slave keeps busy asserted.
module multitini_video_frame_engine #(
    parameter integer WIDTH=540, HEIGHT=720,
    parameter integer STRIDE=((WIDTH+63)/64)*128,
    parameter [31:0] CAPTURE_BASE=32'h22000000,
    parameter [31:0] OUTPUT_BASE=32'h22400000,
    parameter [31:0] HISTORY_BASE=32'h22800000,
    parameter integer FIFO_DEPTH=64
) (
    input wire clk,resetn,clear_errors,start,
    input wire [1:0] input_slot,output_slot,
    input wire [5:0] options,
    input wire history_reset,
    output reg busy,done,error,
    output reg [31:0] error_count,cycles,max_stall,
    output reg [31:0] araddr,
    output wire [3:0] arlen,
    output wire [2:0] arsize,
    output wire [1:0] arburst,
    output wire arvalid,input wire arready,
    input wire [63:0] rdata,input wire [1:0] rresp,
    input wire rlast,rvalid,output wire rready,
    output reg [31:0] awaddr,
    output wire [3:0] awlen,
    output wire [2:0] awsize,
    output wire [1:0] awburst,
    output wire awvalid,input wire awready,
    output wire [63:0] wdata,output wire [7:0] wstrb,
    output wire wlast,wvalid,input wire wready,
    input wire [1:0] bresp,input wire bvalid,output wire bready
);
    localparam integer PIXELS=WIDTH*HEIGHT;
    localparam integer ROW_WORDS=(WIDTH+3)/4;
    localparam integer FRAME_WORDS=ROW_WORDS*HEIGHT;
    localparam integer HISTORY_WORDS=(PIXELS+1)/2;
    localparam integer CW=$clog2(FIFO_DEPTH)+1;
    localparam [2:0] R_IDLE=0,R_ADDR=1,R_DATA=2,R_FILL=3,R_DRAIN=4,R_PLAN=5;
    localparam [2:0] W_IDLE=0,W_ADDR=1,W_DATA=2,W_RESP=3,W_PLAN=4;

    // Static geometry checks are also elaborated by the focused benches.
    initial begin
        if(WIDTH<1 || WIDTH>640 || HEIGHT<1 || HEIGHT>720 || STRIDE<WIDTH*2 ||
           STRIDE%128!=0 || STRIDE*HEIGHT>1048576 || PIXELS*4>2097152)
            $error("Invalid native-frame DMA geometry");
        if(FIFO_DEPTH<32 || (FIFO_DEPTH & (FIFO_DEPTH-1))!=0)
            $error("Frame DMA FIFO depth must be a power of two >=32");
    end

    wire begin_job=start && !busy;
    reg [5:0] job_options;
    reg use_history;
    reg [2:0] read_state;
    reg [2:0] write_state;
    reg read_history,write_history,read_prefer_history,write_prefer_history;
    reg [4:0] read_beats,write_beats,read_index,write_index;
    reg [31:0] source_address,history_read_address,output_address,history_write_address;
    reg [19:0] source_left,history_read_left,output_left,history_write_left;
    reg [9:0] source_row_left,output_row_left;

    wire [63:0] source_data,old_data;
    wire [71:0] new_data,output_data;
    wire source_full,source_empty,old_full,old_empty,new_full,new_empty,output_full,output_empty;
    wire [CW-1:0] source_count,old_count,new_count,output_count;
    wire source_push,old_push,source_pop,old_pop,new_push,new_pop,output_push,output_pop;
    wire [71:0] new_input,output_input;
    wire read_accept=rvalid && rready;
    wire read_payload=(read_state==R_DATA && read_accept) ||
                      (read_state==R_FILL && !(read_history?old_full:source_full));
    wire [63:0] read_value=(read_state==R_DATA && rresp==0)?rdata:64'd0;
    assign source_push=read_payload && !read_history;
    assign old_push=read_payload && read_history;
    multitini_video_frame_fifo #(.WIDTH(64),.DEPTH(FIFO_DEPTH)) source_fifo(
        .clk(clk),.resetn(resetn),.clear(begin_job),.push(source_push),.pop(source_pop),
        .din(read_value),.dout(source_data),.full(source_full),.empty(source_empty),.count(source_count));
    multitini_video_frame_fifo #(.WIDTH(64),.DEPTH(FIFO_DEPTH)) old_fifo(
        .clk(clk),.resetn(resetn),.clear(begin_job),.push(old_push),.pop(old_pop),
        .din(read_value),.dout(old_data),.full(old_full),.empty(old_empty),.count(old_count));
    multitini_video_frame_fifo #(.WIDTH(72),.DEPTH(FIFO_DEPTH)) new_fifo(
        .clk(clk),.resetn(resetn),.clear(begin_job),.push(new_push),.pop(new_pop),
        .din(new_input),.dout(new_data),.full(new_full),.empty(new_empty),.count(new_count));
    multitini_video_frame_fifo #(.WIDTH(72),.DEPTH(FIFO_DEPTH)) output_fifo(
        .clk(clk),.resetn(resetn),.clear(begin_job),.push(output_push),.pop(output_pop),
        .din(output_input),.dout(output_data),.full(output_full),.empty(output_empty),.count(output_count));

    // Restrict bursts before accepting an address. Source/output padding is
    // skipped, not copied; partial final words use byte strobes.
    function automatic [4:0] burst_length(input [31:0] address,input [19:0] remaining,
                                          input [9:0] row_remaining);
        reg [9:0] n,boundary;
        begin
            boundary=10'd512-{1'b0,address[11:3]};
            n=16;
            if(remaining<n)n=remaining[9:0];
            if(row_remaining<n)n=row_remaining;
            if(boundary<n)n=boundary;
            burst_length=n[4:0];
        end
    endfunction
    // Address/remaining bounds change only after a completed transaction.
    // Precompute their lengths before arbitrating FIFO capacity: combining
    // both steps made a 15-level path through the AXI address register enable.
    reg [4:0] source_burst,history_read_burst;
    wire source_available=source_left!=0 && source_count+source_burst<=FIFO_DEPTH;
    wire old_available=history_read_left!=0 && old_count+history_read_burst<=FIFO_DEPTH;
    wire choose_read_history=old_available && (!source_available || read_prefer_history);
    wire read_finish=(read_state==R_DATA && read_accept && rlast && read_index==read_beats-1'b1) ||
                     (read_state==R_FILL && read_payload && read_index==read_beats-1'b1) ||
                     (read_state==R_DRAIN && read_accept && rlast);
    assign arlen=read_beats[3:0]-1'b1;
    assign arsize=3'd3;
    assign arburst=2'b01;
    assign arvalid=read_state==R_ADDR;
    assign rready=read_state==R_DRAIN ||
                  (read_state==R_DATA && !(read_history?old_full:source_full));

    always @(posedge clk) begin
        if(!resetn)begin
            read_state<=R_IDLE;read_history<=0;read_prefer_history<=0;
            read_beats<=0;read_index<=0;araddr<=0;source_burst<=0;history_read_burst<=0;
            source_address<=0;history_read_address<=0;source_left<=0;history_read_left<=0;source_row_left<=0;
        end else if(begin_job)begin
            read_state<=R_PLAN;read_prefer_history<=0;
            source_address<=CAPTURE_BASE+{10'd0,input_slot,20'd0};
            history_read_address<=HISTORY_BASE;source_left<=FRAME_WORDS;
            source_row_left<=ROW_WORDS;
            history_read_left<=(!history_reset && options[1:0]!=0)?HISTORY_WORDS:0;
        end else begin
            case(read_state)
                R_PLAN:begin
                    source_burst<=burst_length(source_address,source_left,source_row_left);
                    history_read_burst<=burst_length(history_read_address,history_read_left,10'd512);
                    read_state<=R_IDLE;
                end
                R_IDLE:if(busy && (source_available || old_available))begin
                    read_history<=choose_read_history;
                    araddr<=choose_read_history?history_read_address:source_address;
                    read_beats<=choose_read_history?history_read_burst:source_burst;
                    read_index<=0;read_state<=R_ADDR;
                end
                R_ADDR:if(arready)read_state<=R_DATA;
                R_DATA:if(read_accept)begin
                    read_index<=read_index+1'b1;
                    if(rlast)begin
                        if(read_index==read_beats-1'b1)read_state<=R_PLAN;
                        else read_state<=R_FILL;
                    end else if(read_index==read_beats-1'b1)read_state<=R_DRAIN;
                end
                R_FILL:if(read_payload)begin
                    read_index<=read_index+1'b1;
                    if(read_index==read_beats-1'b1)read_state<=R_PLAN;
                end
                R_DRAIN:if(read_accept && rlast)read_state<=R_PLAN;
                default:read_state<=R_IDLE;
            endcase
            if(read_finish)begin
                read_prefer_history<=!read_history;
                if(read_history)begin
                    history_read_left<=history_read_left-read_beats;
                    history_read_address<=history_read_address+{24'd0,read_beats,3'd0};
                end else begin
                    source_left<=source_left-read_beats;
                    if(source_row_left==read_beats)begin
                        source_row_left<=ROW_WORDS;
                        source_address<=source_address+{24'd0,read_beats,3'd0}+STRIDE-ROW_WORDS*8;
                    end else begin
                        source_row_left<=source_row_left-read_beats;
                        source_address<=source_address+{24'd0,read_beats,3'd0};
                    end
                end
            end
        end
    end

    // Three elastic stages keep FIFO selection, decay multiplication, and
    // threshold/max separate. Decay uses exactly one 60 Hz step; the ordered
    // scheduler never selects a newer frame in place of an older one.
    reg p0_valid,p1_valid,p2_valid;
    reg [23:0] p0_current,p0_old,p1_current,p2_rgb;
    reg [5:0] p0_coefficient;
    (* use_dsp="yes" *) reg [13:0] p1_r,p1_g,p1_b;
    reg [19:0] input_pixels,persist_pixels;
    reg [9:0] input_x;
    reg [1:0] source_lane;
    wire [15:0] source_pixel=source_data[source_lane*16+:16];
    wire [23:0] old_pixel=use_history ? (input_pixels[0]?old_data[55:32]:old_data[23:0]) :24'd0;
    wire [7:0] old_max_rg=old_pixel[7:0]>old_pixel[15:8]?old_pixel[7:0]:old_pixel[15:8];
    wire [7:0] old_max=old_max_rg>old_pixel[23:16]?old_max_rg:old_pixel[23:16];
    function automatic [5:0] decay_coefficient(input [1:0] strength,input [7:0] maximum);
        begin
            case(strength)
                1:decay_coefficient=maximum>=128?14:maximum>=32?40:58;
                2:decay_coefficient=maximum>=128?18:maximum>=32?48:60;
                3:decay_coefficient=maximum>=128?24:maximum>=32?56:61;
                default:decay_coefficient=0;
            endcase
        end
    endfunction
    wire [7:0] decayed_r=p1_r[13:6],decayed_g=p1_g[13:6],decayed_b=p1_b[13:6];
    wire extinct=decayed_r<4 && decayed_g<4 && decayed_b<4;
    wire [7:0] prior_r=extinct?8'd0:decayed_r,prior_g=extinct?8'd0:decayed_g,prior_b=extinct?8'd0:decayed_b;
    wire filter_in_ready,filter_out_valid,filter_out_ready,filter_busy,filter_done;
    wire [15:0] filter_out_rgb;
    reg filter_finished;
    wire history_flush=persist_pixels[0] || persist_pixels==PIXELS-1;
    wire history_pack_ready=!history_flush || !new_full;
    wire persist_accept=p2_valid && filter_in_ready && history_pack_ready;
    wire p2_ready=!p2_valid || persist_accept;
    wire p1_ready=!p1_valid || p2_ready;
    wire p0_ready=!p0_valid || p1_ready;
    wire input_accept=busy && input_pixels<PIXELS && !source_empty &&
                      (!use_history || !old_empty) && p0_ready;
    assign source_pop=input_accept && (source_lane==3 || input_x==WIDTH-1);
    assign old_pop=input_accept && use_history && (input_pixels[0] || input_pixels==PIXELS-1);
    multitini_video_frame_filter #(.WIDTH(WIDTH),.HEIGHT(HEIGHT)) filter(
        .clk(clk),.resetn(resetn),.start(begin_job),.options(options),
        .in_valid(p2_valid && history_pack_ready),.in_ready(filter_in_ready),.in_rgb(p2_rgb),
        .out_valid(filter_out_valid),.out_ready(filter_out_ready),.out_rgb565(filter_out_rgb),
        .busy(filter_busy),.done(filter_done));
    reg [31:0] history_pack;
    assign new_push=persist_accept && history_flush;
    assign new_input=persist_pixels[0]?{8'hff,8'd0,p2_rgb,history_pack}:
                                      {8'h0f,32'd0,8'd0,p2_rgb};
    always @(posedge clk)begin
        if(!resetn || begin_job)begin
            p0_valid<=0;p1_valid<=0;p2_valid<=0;
            p0_current<=0;p0_old<=0;p0_coefficient<=0;p1_current<=0;p1_r<=0;p1_g<=0;p1_b<=0;p2_rgb<=0;
            input_pixels<=0;persist_pixels<=0;input_x<=0;source_lane<=0;history_pack<=0;filter_finished<=0;
        end else begin
            if(filter_done)filter_finished<=1;
            if(p2_ready)begin
                p2_valid<=p1_valid;
                if(p1_valid)p2_rgb<={p1_current[23:16]>prior_b?p1_current[23:16]:prior_b,
                                     p1_current[15:8]>prior_g?p1_current[15:8]:prior_g,
                                     p1_current[7:0]>prior_r?p1_current[7:0]:prior_r};
            end
            if(p1_ready)begin
                p1_valid<=p0_valid;
                if(p0_valid)begin
                    p1_current<=p0_current;
                    p1_r<=p0_old[7:0]*p0_coefficient;
                    p1_g<=p0_old[15:8]*p0_coefficient;
                    p1_b<=p0_old[23:16]*p0_coefficient;
                end
            end
            if(p0_ready)begin
                p0_valid<=input_accept;
                if(input_accept)begin
                    p0_current<={source_pixel[4:0],source_pixel[4:2],source_pixel[10:5],source_pixel[10:9],
                                 source_pixel[15:11],source_pixel[15:13]};
                    p0_old<=old_pixel;p0_coefficient<=decay_coefficient(job_options[1:0],old_max);
                end
            end
            if(input_accept)begin
                input_pixels<=input_pixels+1'b1;
                if(input_x==WIDTH-1)begin input_x<=0;source_lane<=0;end
                else begin input_x<=input_x+1'b1;source_lane<=source_lane+1'b1;end
            end
            if(persist_accept)begin
                persist_pixels<=persist_pixels+1'b1;
                if(!persist_pixels[0])history_pack<={8'd0,p2_rgb};
            end
        end
    end

    reg [47:0] output_pack;
    reg [1:0] output_lane;
    reg [9:0] output_x;
    reg [19:0] output_pixels;
    wire output_flush=output_lane==3 || output_x==WIDTH-1;
    wire output_accept=filter_out_valid && filter_out_ready;
    assign filter_out_ready=busy && (!output_flush || !output_full);
    assign output_push=output_accept && output_flush;
    reg [63:0] output_word;
    always @* begin
        output_word={16'd0,output_pack};
        output_word[output_lane*16+:16]=filter_out_rgb;
    end
    assign output_input={8'hff >> ((3-output_lane)*2),output_word};
    always @(posedge clk)begin
        if(!resetn || begin_job)begin output_pack<=0;output_lane<=0;output_x<=0;output_pixels<=0;end
        else if(output_accept)begin
            output_pixels<=output_pixels+1'b1;
            if(output_flush)begin output_pack<=0;output_lane<=0;end
            else begin output_pack[output_lane*16+:16]<=filter_out_rgb;output_lane<=output_lane+1'b1;end
            if(output_x==WIDTH-1)output_x<=0;else output_x<=output_x+1'b1;
        end
    end

    reg [4:0] history_write_burst,output_burst;
    wire new_available=history_write_left!=0 && new_count>=history_write_burst;
    wire output_available=output_left!=0 && output_count>=output_burst;
    wire choose_write_history=new_available && (!output_available || write_prefer_history);
    assign awlen=write_beats[3:0]-1'b1;
    assign awsize=3'd3;
    assign awburst=2'b01;
    assign awvalid=write_state==W_ADDR;
    assign wvalid=write_state==W_DATA;
    assign wdata=write_history?new_data[63:0]:output_data[63:0];
    assign wstrb=write_history?new_data[71:64]:output_data[71:64];
    assign wlast=write_index==write_beats-1'b1;
    assign new_pop=wvalid && wready && write_history;
    assign output_pop=wvalid && wready && !write_history;
    assign bready=write_state==W_RESP;
    always @(posedge clk)begin
        if(!resetn)begin
            write_state<=W_IDLE;write_history<=0;write_prefer_history<=1;
            write_beats<=0;write_index<=0;awaddr<=0;history_write_burst<=0;output_burst<=0;
            output_address<=0;history_write_address<=0;output_left<=0;history_write_left<=0;output_row_left<=0;
        end else if(begin_job)begin
            write_state<=W_PLAN;write_prefer_history<=1;
            output_address<=OUTPUT_BASE+{10'd0,output_slot,20'd0};
            history_write_address<=HISTORY_BASE;output_left<=FRAME_WORDS;
            output_row_left<=ROW_WORDS;history_write_left<=HISTORY_WORDS;
        end else begin
            case(write_state)
                W_PLAN:begin
                    history_write_burst<=burst_length(history_write_address,history_write_left,10'd512);
                    output_burst<=burst_length(output_address,output_left,output_row_left);
                    write_state<=W_IDLE;
                end
                W_IDLE:if(busy && (new_available || output_available))begin
                    write_history<=choose_write_history;
                    awaddr<=choose_write_history?history_write_address:output_address;
                    write_beats<=choose_write_history?history_write_burst:output_burst;
                    write_index<=0;write_state<=W_ADDR;
                end
                W_ADDR:if(awready)write_state<=W_DATA;
                W_DATA:if(wready)begin
                    write_index<=write_index+1'b1;
                    if(wlast)write_state<=W_RESP;
                end
                W_RESP:if(bvalid)begin
                    write_state<=W_PLAN;write_prefer_history<=!write_history;
                    if(write_history)begin
                        history_write_left<=history_write_left-write_beats;
                        history_write_address<=history_write_address+{24'd0,write_beats,3'd0};
                    end else begin
                        output_left<=output_left-write_beats;
                        if(output_row_left==write_beats)begin
                            output_row_left<=ROW_WORDS;
                            output_address<=output_address+{24'd0,write_beats,3'd0}+STRIDE-ROW_WORDS*8;
                        end else begin
                            output_row_left<=output_row_left-write_beats;
                            output_address<=output_address+{24'd0,write_beats,3'd0};
                        end
                    end
                end
                default:write_state<=W_IDLE;
            endcase
        end
    end

    wire response_error=(read_state==R_DATA && read_accept &&
                         (rresp!=0 || rlast!=(read_index==read_beats-1'b1))) ||
                        (read_state==R_DRAIN && read_accept && rresp!=0) ||
                        (bready && bvalid && bresp!=0) || (start && busy);
    wire read_wait=(arvalid && !arready) ||
                   ((read_state==R_DATA || read_state==R_DRAIN) && !rvalid);
    wire write_wait=(awvalid && !awready) || (wvalid && !wready) || (bready && !bvalid);
    reg [31:0] read_stall,write_stall;
    always @(posedge clk)begin
        if(!resetn)begin
            busy<=0;done<=0;error<=0;error_count<=0;cycles<=0;max_stall<=0;
            job_options<=0;use_history<=0;read_stall<=0;write_stall<=0;
        end else begin
            done<=0;
            if(begin_job)begin
                busy<=1;error<=0;cycles<=0;job_options<=options;
                use_history<=!history_reset && options[1:0]!=0;
            end else if(busy)begin
                cycles<=cycles+1'b1;
                if(filter_finished && input_pixels==PIXELS && persist_pixels==PIXELS && output_pixels==PIXELS &&
                   !p0_valid && !p1_valid && !p2_valid &&
                   read_state==R_IDLE && source_left==0 && history_read_left==0 &&
                   write_state==W_IDLE && output_left==0 && history_write_left==0 &&
                   source_empty && old_empty && new_empty && output_empty)begin busy<=0;done<=1;end
            end
            if(clear_errors && !busy)begin error<=0;error_count<=0;max_stall<=0;end
            else begin
                if(response_error)begin error<=1;error_count<=error_count+1'b1;end
                if(read_wait && read_stall>=max_stall)max_stall<=read_stall+1'b1;
                if(write_wait && write_stall>=max_stall && (!read_wait || write_stall>=read_stall))
                    max_stall<=write_stall+1'b1;
            end
            read_stall<=read_wait?read_stall+1'b1:0;
            write_stall<=write_wait?write_stall+1'b1:0;
        end
    end
endmodule

// Small single-clock fall-through queue. The data array has no reset so Vivado
// can map it to distributed RAM; all validity lives in pointers/count.
module multitini_video_frame_fifo #(parameter integer WIDTH=64,DEPTH=64)(
    input wire clk,resetn,clear,push,pop,input wire [WIDTH-1:0] din,
    output wire [WIDTH-1:0] dout,output wire full,empty,
    output reg [$clog2(DEPTH):0] count
);
    localparam integer PW=$clog2(DEPTH);
    (* ram_style="distributed" *) reg [WIDTH-1:0] memory[0:DEPTH-1];
    reg [PW-1:0] read_pointer,write_pointer;
    assign dout=memory[read_pointer];
    assign full=count==DEPTH;
    assign empty=count==0;
    always @(posedge clk)begin
        if(!resetn || clear)begin count<=0;read_pointer<=0;write_pointer<=0;end
        else begin
            if(push)begin memory[write_pointer]<=din;write_pointer<=write_pointer+1'b1;end
            if(pop)read_pointer<=read_pointer+1'b1;
            case({push,pop})2'b10:count<=count+1'b1;2'b01:count<=count-1'b1;default:count<=count;endcase
        end
    end
endmodule
