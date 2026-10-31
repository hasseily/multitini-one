// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Bounded one-word bridge for the original Minimig UIO, floppy FIFO and IDE
// register interfaces. Commands and completions cross as coherent mailboxes.
module multitini_amiga_host(
    input wire control_clk,control_resetn,write,
    input wire [31:0] command,output wire [31:0] status,
    input wire clk,reset,chip_tick,
    output reg io_fpga,io_uio,io_strobe,output reg [15:0] io_din,
    input wire io_wait,input wire [15:0] io_dout,
    input wire [5:0] ide_req,
    output reg [4:0] ide_address,output reg ide_read,ide_write,
    output reg [15:0] ide_wdata,input wire [15:0] ide_rdata,
    output reg [64:0] native_rtc
);
    // The service clock is the ungated 28MHz sibling of the native clock.
    // Publish the complete native bundle in its inactive half-cycle, before
    // the next accepted edge. This also makes a strobe exactly one accepted
    // native cycle, including arbitrary memory holds.
    reg staged_io_fpga,staged_io_uio,staged_io_strobe;
    reg [15:0] staged_io_din,staged_ide_wdata;
    reg [4:0] staged_ide_address;
    reg staged_ide_read,staged_ide_write;
    // The existing coherent command mailbox assembles one RTC snapshot.
    // Publish its data and toggle together before an accepted native edge.
    reg [64:0] staged_rtc,committed_rtc;
    reg [47:0] rtc_words;
    reg [1:0] rtc_stage;
    always @(negedge clk or posedge reset)begin
        if(reset)begin
            io_fpga<=0;io_uio<=0;io_strobe<=0;io_din<=0;
            ide_address<=0;ide_read<=0;ide_write<=0;ide_wdata<=0;
            native_rtc<=0;
        end else begin
            io_fpga<=staged_io_fpga;io_uio<=staged_io_uio;io_strobe<=staged_io_strobe;io_din<=staged_io_din;
            ide_address<=staged_ide_address;ide_read<=staged_ide_read;
            ide_write<=staged_ide_write;ide_wdata<=staged_ide_wdata;
            native_rtc<=staged_rtc;
        end
    end

    reg pending,outstanding,error,completion_toggle;
    reg [31:0] held;
    wire request_ready,request_valid;
    wire [31:0] request_data;
    reg [15:0] result;
    wire response_valid,response_ready;
    wire [16:0] response_data;
    reg response_pending,response_error;
    reg [15:0] response_word;
    reg [5:0] req_control;
    wire [5:0] req_snapshot;
    wire req_snapshot_valid;
    localparam [2:0] IDLE=0,PULSE=1,SETTLE=2,ARM=3,RTC_WAIT=4;
    reg [2:0] state;
    // These are encoded request states (e.g. 0 -> 5), not independent flags.
    // Publish a coherent held snapshot so a transition cannot look like a
    // different command. Snapshots are eventually current; an ACK followed
    // by an immediate raw status read is not a freshness barrier.
    multitini_async_mailbox #(.WIDTH(6)) request_status_i(
        .src_clk(clk),.src_resetn(!reset),.src_data(ide_req),.src_valid(1'b1),.src_ready(),
        .dst_clk(control_clk),.dst_resetn(control_resetn),.dst_data(req_snapshot),
        .dst_valid(req_snapshot_valid),.dst_ready(1'b1));
    always @(posedge control_clk)
        if(!control_resetn)req_control<=0;
        else if(req_snapshot_valid)req_control<=req_snapshot;
    // Bit28 advertises the optional framed RTC extension to ARM firmware.
    assign status={pending||outstanding,error,completion_toggle,1'b1,6'd0,req_control,result};
    multitini_async_mailbox #(.WIDTH(32)) request_i(
        .src_clk(control_clk),.src_resetn(control_resetn),.src_data(held),
        .src_valid(pending),.src_ready(request_ready),
        .dst_clk(clk),.dst_resetn(!reset),.dst_data(request_data),
        .dst_valid(request_valid),.dst_ready(state==IDLE && !response_pending));
    multitini_async_mailbox #(.WIDTH(17)) response_i(
        .src_clk(clk),.src_resetn(!reset),.src_data({response_error,response_word}),
        .src_valid(response_pending),.src_ready(response_ready),
        .dst_clk(control_clk),.dst_resetn(control_resetn),.dst_data(response_data),
        .dst_valid(response_valid),.dst_ready(1'b1));
    always @(posedge control_clk)begin
        if(!control_resetn)begin pending<=0;outstanding<=0;error<=0;completion_toggle<=0;held<=0;result<=0;end
        else begin
            if(pending && request_ready)begin pending<=0;outstanding<=1;end
            if(response_valid)begin result<=response_data[15:0];error<=error|response_data[16];outstanding<=0;completion_toggle<=~completion_toggle;end
            if(write)begin
                if(pending || outstanding)error<=1;
                else begin
                    if(command[31:24]==4)error<=0;
                    held<=command;pending<=1;
                end
            end
        end
    end
    reg [1:0] age;
    reg [15:0] timeout_count;
    reg [7:0] operation;
    reg [15:0] captured_ide_word;
    always @(posedge clk)begin
        if(reset)begin
            state<=IDLE;age<=0;timeout_count<=0;operation<=0;captured_ide_word<=0;
            staged_io_fpga<=0;staged_io_uio<=0;staged_io_strobe<=0;staged_io_din<=0;
            staged_ide_address<=0;staged_ide_read<=0;staged_ide_write<=0;staged_ide_wdata<=0;
            staged_rtc<=0;committed_rtc<=0;rtc_words<=0;rtc_stage<=0;
            response_pending<=0;response_error<=0;response_word<=0;
        end else begin
            if(response_pending && response_ready)response_pending<=0;
            case(state)
                IDLE:if(request_valid && !response_pending)begin
                    operation<=request_data[31:24];timeout_count<=0;age<=0;response_error<=0;
                    // A frame is three consecutive RTC commands. Any other
                    // command, including CLEAR_ERROR, discards partial words.
                    rtc_stage<=0;
                    if(request_data[31:24]==0)begin
                        staged_io_fpga<=request_data[0] && !request_data[1];
                        staged_io_uio<=request_data[1] && !request_data[0];
                        response_error<=&request_data[1:0];state<=SETTLE;
                    end else if(request_data[31:24]==1 && (staged_io_fpga || staged_io_uio))begin
                        staged_io_din<=request_data[15:0];state<=ARM;
                    end else if(request_data[31:24]==2 || request_data[31:24]==3)begin
                        staged_ide_address<=request_data[20:16];staged_ide_wdata<=request_data[15:0];
                        state<=ARM;
                    end else if(request_data[31:24]==4)begin
                        response_word<=0;response_pending<=1;
                    end else if(request_data[31:24]==5)begin
                        rtc_words[23:0]<=request_data[23:0];rtc_stage<=1;
                        response_word<=0;response_pending<=1;
                    end else if(request_data[31:24]==6 && rtc_stage==1)begin
                        rtc_words[47:24]<=request_data[23:0];rtc_stage<=2;
                        response_word<=0;response_pending<=1;
                    end else if(request_data[31:24]==7 && rtc_stage==2 && request_data[23:16]==0)begin
                        staged_rtc<={~committed_rtc[64],request_data[15:0],rtc_words};
                        state<=RTC_WAIT;
                    end else begin response_error<=1;response_word<=0;response_pending<=1;end
                end
                RTC_WAIT:begin
                    timeout_count<=timeout_count+1'b1;
                    if(chip_tick)begin
                        // This edge consumed the complete negedge-published
                        // snapshot. Only now may ARM observe completion.
                        committed_rtc<=staged_rtc;
                        response_word<=0;response_pending<=1;state<=IDLE;
                    end else if(&timeout_count)begin
                        // No native edge saw this toggle. Restore the last
                        // consumed snapshot so a retry cannot cancel it out.
                        staged_rtc<=committed_rtc;
                        response_error<=1;response_word<=0;response_pending<=1;state<=IDLE;
                    end
                end
                ARM:begin
                    timeout_count<=timeout_count+1'b1;
                    if(chip_tick)begin
                        staged_io_strobe<=operation==1;
                        staged_ide_read<=operation==2;staged_ide_write<=operation==3;
                        timeout_count<=0;state<=PULSE;
                    end else if(&timeout_count)begin
                        response_error<=1;response_word<=0;response_pending<=1;state<=IDLE;
                        staged_io_fpga<=0;staged_io_uio<=0;
                    end
                end
                PULSE:begin
                    timeout_count<=timeout_count+1'b1;
                    if(chip_tick)begin
                        // IDE advances its management FIFO on the falling
                        // read strobe. Capture before that edge, while this
                        // transaction's word is still selected.
                        captured_ide_word<=ide_rdata;
                        staged_io_strobe<=0;staged_ide_read<=0;staged_ide_write<=0;timeout_count<=0;state<=SETTLE;
                    end
                    else if(&timeout_count)begin
                        staged_io_strobe<=0;staged_ide_read<=0;staged_ide_write<=0;staged_io_fpga<=0;staged_io_uio<=0;
                        response_error<=1;response_word<=0;response_pending<=1;state<=IDLE;
                    end
                end
                SETTLE:begin
                    timeout_count<=timeout_count+1'b1;
                    if(chip_tick && age!=3)age<=age+1'b1;
                    if((age>=2 && !io_wait) || &timeout_count)begin
                        if(&timeout_count)begin response_error<=1;staged_io_fpga<=0;staged_io_uio<=0;end
                        response_word<=operation==2 ? captured_ide_word : io_dout;
                        response_pending<=1;state<=IDLE;
                    end
                end
                default:state<=IDLE;
            endcase
        end
    end
endmodule
