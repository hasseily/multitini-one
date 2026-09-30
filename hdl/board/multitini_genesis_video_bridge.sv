// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Complete source fields live in a dedicated four-slot DDR ring on HP2.
// Keep the displayed slot owned until a later publication is acknowledged.
// Source-field repeats/drops absorb source/output rate differences; the shared
// ordered effects pipeline still consumes the output at its normal cadence.
module multitini_genesis_video_bridge (
    input wire source_clk,source_resetn,source_ready,
    input wire source_valid,source_de,source_vsync,source_mode_240,
    input wire [7:0] source_r,source_g,source_b,
    input wire control_clk,control_resetn,
    input wire pixel_clk,pixel_resetn,shared_pixel_resetn,
    input wire diagnostic_request,
    input wire [11:0] scan_x,scan_y,
    input wire vblank_start,
    input wire [11:0] display_view_x,display_view_y,display_view_width,display_view_height,
    input wire [3:0] display_scale_num,display_scale_den,
    input wire display_pause,
    output wire display_quiescent,
    output reg machine_active,
    output wire [15:0] machine_rgb565,
    output wire [11:0] viewport_x,viewport_y,viewport_width,viewport_height,
    output wire [31:0] capture_drops,capture_errors,read_errors,underruns,
    output wire [31:0] araddr,output wire [3:0] arlen,
    output wire [2:0] arsize,output wire [1:0] arburst,
    output wire arvalid,input wire arready,
    input wire [63:0] rdata,input wire [1:0] rresp,
    input wire rlast,rvalid,output wire rready,
    output wire [31:0] awaddr,output wire [3:0] awlen,
    output wire [2:0] awsize,output wire [1:0] awburst,
    output wire awvalid,input wire awready,
    output wire [63:0] wdata,output wire [7:0] wstrb,
    output wire wlast,wvalid,input wire wready,
    input wire [1:0] bresp,input wire bvalid,output wire bready
);
    wire [11:0] normalized_x,normalized_y;
    wire [15:0] normalized_rgb;
    wire normalized_start;
    wire [31:0] malformed_fields;
    multitini_genesis_video_normalizer normalizer(
        .clk(source_clk),.resetn(source_resetn),.ready(source_ready),
        .pixel_ce(source_valid),.de(source_de),.vsync(source_vsync),
        .red(source_r),.green(source_g),.blue(source_b),.mode_240(source_mode_240),
        .scan_x(normalized_x),.scan_y(normalized_y),.rgb565(normalized_rgb),
        .frame_start(normalized_start),.dropped_fields(malformed_fields));
    wire [31:0] producer;
    reg [31:0] consumer;
    wire capture_quiet,reader_quiet,submit_ready,active_valid,pixel_use;
    wire [31:0] active_value;
    wire [15:0] pixel_rgb;
    wire source_ready_control;
    cdc_bit_sync ready_control_sync(.clk(control_clk),.resetn(control_resetn),
        .din(source_ready),.dout(source_ready_control));
    wire enabled=source_ready_control && !display_pause;
    wire pause_source;
    cdc_bit_sync pause_source_sync(.clk(source_clk),.resetn(source_resetn),
        .din(display_pause),.dout(pause_source));
    // A paused/reset machine may never deliver the final source pixel. Only
    // the capture sampler resets here; its mailboxes and accepted AXI bursts
    // stay alive until the control-side drain acknowledges them.
    wire capture_source_resetn=source_resetn && source_ready && !pause_source;
    wire [31:0] next_value=active_valid ? active_value+1'b1 : consumer;
    wire submit=enabled && submit_ready && producer!=next_value;
    always @(posedge control_clk)begin
        if(!control_resetn)consumer<=0;
        else if(!enabled && capture_quiet && reader_quiet)consumer<=producer;
        else if(active_valid)consumer<=active_value;
    end
    assign display_quiescent=!enabled && capture_quiet && reader_quiet;
    multitini_video_capture #(.WIDTH(320),.HEIGHT(240),.VIEW_X(0),.VIEW_Y(0),
        .SCALE_NUM(1),.SCALE_DEN(1),.BASE(32'h23000000),.SHARED_PIXEL_RESET(1'b1),
        .ABORT_INCOMPLETE_ON_VBLANK(1'b1)) capture_i(
        .display_view_x(12'd0),.display_view_y(12'd0),.display_view_width(12'd320),.display_view_height(12'd240),
        .display_scale_num(4'd1),.display_scale_den(4'd1),
        .clk(control_clk),.resetn(control_resetn),.enable(enabled),.consumer(consumer),
        .producer(producer),.drops(capture_drops),.errors(capture_errors),.busy(),.quiescent(capture_quiet),.pixel_running(),
        .pixel_clk(source_clk),.pixel_resetn(capture_source_resetn),.shared_pixel_resetn(source_resetn),
        .machine_ready(source_ready),.scan_x(normalized_x),.scan_y(normalized_y),
        .vblank_start(normalized_start),.rgb565(normalized_rgb),
        .awaddr(awaddr),.awlen(awlen),.awsize(awsize),.awburst(awburst),.awvalid(awvalid),.awready(awready),
        .wdata(wdata),.wstrb(wstrb),.wlast(wlast),.wvalid(wvalid),.wready(wready),
        .bresp(bresp),.bvalid(bvalid),.bready(bready));
    multitini_video_frame_reader #(.DYNAMIC_DISPLAY(1'b1),.WIDTH(320),.HEIGHT(240),
        .VIEW_X(320),.VIEW_Y(60),.SCALE_NUM(4),.SCALE_DEN(1),.BASE(32'h23000000),
        .SHARED_PIXEL_RESET(1'b1)) reader_i(
        .display_view_x(display_view_x),.display_view_y(display_view_y),
        .display_view_width(display_view_width),.display_view_height(display_view_height),
        .display_scale_num(display_scale_num),.display_scale_den(display_scale_den),
        .clk(control_clk),.resetn(control_resetn),.enable(enabled),
        .submit(submit),.submit_value(next_value),.submit_ready(submit_ready),
        .active_value(active_value),.active_valid(active_valid),.busy(),.quiescent(reader_quiet),
        .error_count(read_errors),.underrun_count(underruns),
        .pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.shared_pixel_resetn(shared_pixel_resetn),.shared_pixel_running(1'b0),
        .scan_x(scan_x),.scan_y(scan_y),.vblank_start(vblank_start),.pixel_use(pixel_use),.pixel_rgb565(pixel_rgb),
        .araddr(araddr),.arlen(arlen),.arsize(arsize),.arburst(arburst),.arvalid(arvalid),.arready(arready),
        .rdata(rdata),.rresp(rresp),.rlast(rlast),.rvalid(rvalid),.rready(rready));
    wire frame_ready_pixel,diagnostic_pixel;
    cdc_bit_sync frame_ready_sync(.clk(pixel_clk),.resetn(pixel_resetn),
        .din(active_valid && source_ready_control),.dout(frame_ready_pixel));
    cdc_bit_sync diagnostic_sync(.clk(pixel_clk),.resetn(pixel_resetn),
        .din(diagnostic_request),.dout(diagnostic_pixel));
    always @(posedge pixel_clk)begin
        if(!pixel_resetn)machine_active<=0;
        else if(vblank_start)machine_active<=frame_ready_pixel && !diagnostic_pixel;
    end
    assign machine_rgb565=machine_active && pixel_use ? pixel_rgb : 16'd0;
    assign viewport_x=display_view_x,viewport_y=display_view_y;
    assign viewport_width=display_view_width,viewport_height=display_view_height;
endmodule
