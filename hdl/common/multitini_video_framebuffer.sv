// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// HP0 owns capture/scanout; the autonomous frame engine owns HP1. Software
// configures effects and reads diagnostics, while hardware schedules every
// frame in order. The manual ring interface remains for isolated diagnostics.
module multitini_video_framebuffer #(
    parameter integer WIDTH=382,HEIGHT=270,VIEW_X=196,VIEW_Y=0,
    parameter integer SCALE_NUM=4,SCALE_DEN=1,
    parameter bit SHARED_PIXEL_RESET=1'b0
)(
    input wire clk,resetn,wr_en,
    input wire [11:0] wr_addr,rd_addr,
    input wire [31:0] wr_data,input wire [3:0] wr_strb,
    output reg [31:0] rd_data,
    input wire pixel_clk,pixel_resetn,shared_pixel_resetn,vblank_start,machine_ready,
    input wire [11:0] scan_x,scan_y,input wire [15:0] machine_rgb565,
    output wire pixel_use,pixel_running,output wire [15:0] pixel_rgb565,
    output wire [31:0] awaddr,output wire [3:0] awlen,
    output wire [2:0] awsize,output wire [1:0] awburst,
    output wire awvalid,input wire awready,
    output wire [63:0] wdata,output wire [7:0] wstrb,
    output wire wlast,wvalid,input wire wready,
    input wire [1:0] bresp,input wire bvalid,output wire bready,
    output wire [31:0] araddr,output wire [3:0] arlen,
    output wire [2:0] arsize,output wire [1:0] arburst,
    output wire arvalid,input wire arready,
    input wire [63:0] rdata,input wire [1:0] rresp,
    input wire rlast,rvalid,output wire rready,
    output wire [31:0] fx_awaddr,output wire [3:0] fx_awlen,
    output wire [2:0] fx_awsize,output wire [1:0] fx_awburst,
    output wire fx_awvalid,input wire fx_awready,
    output wire [63:0] fx_wdata,output wire [7:0] fx_wstrb,
    output wire fx_wlast,fx_wvalid,input wire fx_wready,
    input wire [1:0] fx_bresp,input wire fx_bvalid,output wire fx_bready,
    output wire [31:0] fx_araddr,output wire [3:0] fx_arlen,
    output wire [2:0] fx_arsize,output wire [1:0] fx_arburst,
    output wire fx_arvalid,input wire fx_arready,
    input wire [63:0] fx_rdata,input wire [1:0] fx_rresp,
    input wire fx_rlast,fx_rvalid,output wire fx_rready
);
    reg [2:0] control;
    reg [5:0] options;
    reg [31:0] consumer,submit_value;
    reg submit,bad_write,clear_errors,autonomous_owned,capture_enable;
    reg [31:0] capture_error_base,read_error_base,drop_base,underrun_base;
    wire [31:0] producer,capture_drops,capture_errors,read_errors,underruns,active_value;
    wire capture_busy,capture_quiet,reader_busy,reader_quiet,active_valid,submit_ready;
    wire scheduler_busy,scheduler_quiet,scheduler_error,hardware_submit;
    wire [31:0] hardware_consumer,hardware_value,processed_frames,deadline_misses;
    wire engine_start,engine_history_reset,engine_busy,engine_done,engine_error;
    wire [1:0] engine_input_slot,engine_output_slot;
    wire [5:0] engine_options;
    wire [31:0] engine_errors,engine_cycles,engine_max_stall;
    wire automatic_mode=control[2] || autonomous_owned;
    wire [31:0] selected_consumer=automatic_mode ? hardware_consumer : consumer;
    wire selected_submit=control[1] && (automatic_mode ? hardware_submit : submit);
    wire [31:0] selected_value=automatic_mode ? hardware_value : submit_value;
    // The registered enable must have fallen before IDLE means drained:
    // otherwise a just-completed capture could re-arm on the next clock.
    wire all_quiet=!capture_enable && capture_quiet && reader_quiet && scheduler_quiet && !engine_busy;
    wire transport_error=capture_errors!=capture_error_base || read_errors!=read_error_base ||
                         capture_drops!=drop_base || underruns!=underrun_base;
    wire [31:0] outstanding=producer-selected_consumer;
    wire [31:0] released=wr_data-consumer;
    wire frame_tick;
    multitini_cdc_pulse_toggle frame_tick_i(
        .src_clk(pixel_clk),.src_resetn(SHARED_PIXEL_RESET ? shared_pixel_resetn : pixel_resetn),
        .src_pulse(vblank_start),.dst_clk(clk),.dst_resetn(resetn),.dst_pulse(frame_tick));
    always @(posedge clk)begin
        if(!resetn)begin
            control<=0;options<=0;consumer<=0;submit<=0;submit_value<=0;bad_write<=0;clear_errors<=0;
            autonomous_owned<=0;
            capture_enable<=0;
            capture_error_base<=0;read_error_base<=0;drop_base<=0;underrun_base<=0;
        end
        else begin
            submit<=0;clear_errors<=0;
            // Launch a registered level into the pixel-domain synchronizer;
            // autonomous error/ownership logic must not precede that crossing.
            capture_enable<=control[0] && (!automatic_mode || !scheduler_error);
            if(control[2])autonomous_owned<=1;
            else if(scheduler_quiet && !engine_busy)autonomous_owned<=0;
            if(wr_en)begin
                if(wr_strb!=4'hf)bad_write<=1;
                else case(wr_addr)
                    12'h180:if(wr_data[31:3]==0 && (!wr_data[2] || wr_data[1:0]==3) &&
                               (!automatic_mode || wr_data==0 || wr_data==7))control<=wr_data[2:0];else bad_write<=1;
                    12'h18c:if(!automatic_mode && released<=outstanding && outstanding<=4)consumer<=wr_data;else bad_write<=1;
                    12'h190:if(!automatic_mode && control[1] && submit_ready && !submit)begin submit<=1;submit_value<=wr_data;end
                             else bad_write<=1;
                    12'h1ac:if(wr_data==1 && control==0 && all_quiet)begin
                        bad_write<=0;capture_error_base<=capture_errors;read_error_base<=read_errors;
                        drop_base<=capture_drops;underrun_base<=underruns;clear_errors<=1;
                    end else bad_write<=1;
                    12'h1b0:if(wr_data[31:6]==0)options<=wr_data[5:0];else bad_write<=1;
                    default:bad_write<=1;
                endcase
            end
        end
    end
    always @*begin
        case(rd_addr)
            12'h180:rd_data={29'd0,control};
            12'h184:rd_data={24'd0,scheduler_error,engine_busy,1'b1,
                (bad_write || transport_error || engine_error || scheduler_error),
                (control==0 && all_quiet),active_valid,(control[1] && (!submit_ready || selected_submit)),capture_busy};
            12'h188:rd_data=producer;
            12'h18c:rd_data=selected_consumer;
            12'h194:rd_data=active_value;
            12'h198:rd_data=(HEIGHT<<16)|WIDTH;
            12'h19c:rd_data=((WIDTH+63)/64)*128;
            12'h1a0:rd_data=capture_drops-drop_base;
            12'h1a4:rd_data=(capture_errors-capture_error_base)+(read_errors-read_error_base);
            12'h1a8:rd_data=underruns-underrun_base;
            12'h1b0:rd_data={26'd0,options};
            12'h1b4:rd_data=processed_frames;
            12'h1b8:rd_data=deadline_misses;
            12'h1bc:rd_data=engine_cycles;
            12'h1c0:rd_data=engine_max_stall;
            12'h1c4:rd_data=engine_errors;
            default:rd_data=0;
        endcase
    end
    multitini_video_capture #(.WIDTH(WIDTH),.HEIGHT(HEIGHT),.VIEW_X(VIEW_X),.VIEW_Y(VIEW_Y),
        .SCALE_NUM(SCALE_NUM),.SCALE_DEN(SCALE_DEN),.SHARED_PIXEL_RESET(SHARED_PIXEL_RESET)) capture_i(
        .clk(clk),.resetn(resetn),.enable(capture_enable),
        .consumer(selected_consumer),.producer(producer),
        .drops(capture_drops),.errors(capture_errors),.busy(capture_busy),.quiescent(capture_quiet),
        .pixel_running(pixel_running),
        .pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.shared_pixel_resetn(shared_pixel_resetn),.vblank_start(vblank_start),
        // Machine reset/load/diagnostic intervals still have an output raster.
        // Supply a black native frame then, preserving the ordered cadence.
        .machine_ready(1'b1),.scan_x(scan_x),.scan_y(scan_y),
        .rgb565(machine_ready ? machine_rgb565 : 16'b0),
        .awaddr(awaddr),.awlen(awlen),.awsize(awsize),.awburst(awburst),.awvalid(awvalid),.awready(awready),
        .wdata(wdata),.wstrb(wstrb),.wlast(wlast),.wvalid(wvalid),.wready(wready),
        .bresp(bresp),.bvalid(bvalid),.bready(bready));
    multitini_video_frame_reader #(.WIDTH(WIDTH),.HEIGHT(HEIGHT),.VIEW_X(VIEW_X),.VIEW_Y(VIEW_Y),
        .SCALE_NUM(SCALE_NUM),.SCALE_DEN(SCALE_DEN),.SHARED_PIXEL_RESET(SHARED_PIXEL_RESET),
        .SHARED_PIXEL_LIVENESS(1'b1)) reader_i(
        .clk(clk),.resetn(resetn),.enable(control[1]),.submit(selected_submit),.submit_value(selected_value),
        .submit_ready(submit_ready),.active_value(active_value),.active_valid(active_valid),
        .busy(reader_busy),.quiescent(reader_quiet),.error_count(read_errors),.underrun_count(underruns),
        .pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.shared_pixel_resetn(shared_pixel_resetn),.scan_x(scan_x),.scan_y(scan_y),.vblank_start(vblank_start),
        .shared_pixel_running(pixel_running),
        .pixel_use(pixel_use),.pixel_rgb565(pixel_rgb565),
        .araddr(araddr),.arlen(arlen),.arsize(arsize),.arburst(arburst),.arvalid(arvalid),.arready(arready),
        .rdata(rdata),.rresp(rresp),.rlast(rlast),.rvalid(rvalid),.rready(rready));

    multitini_video_frame_scheduler scheduler_i(
        .clk(clk),.resetn(resetn),.enable(control[2]),.clear_errors(clear_errors),
        .options(options),.frame_tick(frame_tick),.capture_producer(producer),
        .capture_consumer(hardware_consumer),.reader_submit_ready(submit_ready),
        .reader_active_valid(active_valid),.reader_active_value(active_value),.reader_quiescent(reader_quiet),
        .display_submit(hardware_submit),.display_value(hardware_value),
        .engine_start(engine_start),.engine_input_slot(engine_input_slot),.engine_output_slot(engine_output_slot),
        .engine_options(engine_options),.engine_history_reset(engine_history_reset),
        .engine_busy(engine_busy),.engine_done(engine_done),.engine_error(engine_error),
        .transport_error(transport_error),.busy(scheduler_busy),.quiescent(scheduler_quiet),
        .processed_frames(processed_frames),.deadline_misses(deadline_misses),.sticky_error(scheduler_error));

    multitini_video_frame_engine #(.WIDTH(WIDTH),.HEIGHT(HEIGHT),.STRIDE(((WIDTH+63)/64)*128)) engine_i(
        .clk(clk),.resetn(resetn),.start(engine_start),.clear_errors(clear_errors),
        .input_slot(engine_input_slot),.output_slot(engine_output_slot),
        .options(engine_options),.history_reset(engine_history_reset),
        .busy(engine_busy),.done(engine_done),.error(engine_error),.error_count(engine_errors),
        .cycles(engine_cycles),.max_stall(engine_max_stall),
        .awaddr(fx_awaddr),.awlen(fx_awlen),.awsize(fx_awsize),.awburst(fx_awburst),
        .awvalid(fx_awvalid),.awready(fx_awready),.wdata(fx_wdata),.wstrb(fx_wstrb),
        .wlast(fx_wlast),.wvalid(fx_wvalid),.wready(fx_wready),
        .bresp(fx_bresp),.bvalid(fx_bvalid),.bready(fx_bready),
        .araddr(fx_araddr),.arlen(fx_arlen),.arsize(fx_arsize),.arburst(fx_arburst),
        .arvalid(fx_arvalid),.arready(fx_arready),.rdata(fx_rdata),.rresp(fx_rresp),
        .rlast(fx_rlast),.rvalid(fx_rvalid),.rready(fx_rready));
endmodule
