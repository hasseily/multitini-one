// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Native RGB565 capture into four bounded 1 MiB DDR slots. Capture observes the
// existing scaled image, never backpressures the core, and publishes only after
// every AXI write response succeeds. Each padded row is a multiple of 128 bytes.
module multitini_video_capture #(
    parameter bit DYNAMIC_DISPLAY=1'b0,
    parameter integer WIDTH=382, HEIGHT=270,
    parameter integer ALT_WIDTH=WIDTH, ALT_HEIGHT=HEIGHT,
    parameter integer WIDE_WIDTH=WIDTH, WIDE_HEIGHT=HEIGHT,
    parameter integer SLOT_SHIFT=20, WIDE_SLOT_SHIFT=SLOT_SHIFT,
    parameter integer VIEW_X=196, VIEW_Y=0,
    parameter integer SCALE_NUM=4, SCALE_DEN=1,
    parameter [31:0] BASE=32'h22000000,
    parameter [31:0] WIDE_BASE=BASE,
    parameter bit SHARED_PIXEL_RESET=1'b0,
    // Sparse source streams can end early on a malformed chip field. Never
    // concatenate their partial pixels with the following field.
    parameter bit ABORT_INCOMPLETE_ON_VBLANK=1'b0
) (
    input wire [11:0] display_view_x, display_view_y, display_view_width, display_view_height,
    input wire [3:0] display_scale_num, display_scale_den,
    // Pixel-domain profile, stable for an accepted source frame. Ignored by
    // legacy instances whose alternate dimensions equal the base profile.
    input wire source_alt,source_weave,
    input wire [1:0] source_profile, // pixel domain; ignored without WIDE geometry
    input wire clk,resetn,enable,
    input wire [31:0] consumer,
    output reg [31:0] producer,drops,errors,
    output reg [3:0] slot_alt,
    output reg [7:0] slot_profile, // control-domain completed-slot metadata
    output wire busy,quiescent,pixel_running,
    input wire pixel_clk,pixel_resetn,shared_pixel_resetn,vblank_start,machine_ready,
    input wire [11:0] scan_x,scan_y,
    input wire [15:0] rgb565,
    output reg [31:0] awaddr,
    output wire [3:0] awlen,
    output wire [2:0] awsize,
    output wire [1:0] awburst,
    output wire awvalid,input wire awready,
    output wire [63:0] wdata,output wire [7:0] wstrb,
    output wire wlast,wvalid,input wire wready,
    input wire [1:0] bresp,input wire bvalid,output wire bready
);
    reg [11:0] view_x,view_y,view_right,view_bottom;
    reg [13:0] padding_right;
    reg [3:0] scale_num,scale_den;
    localparam integer STRIDE_PIXELS=((WIDTH+63)/64)*64;
    localparam integer PAD_PIXELS=STRIDE_PIXELS-WIDTH;
    localparam integer FRAME_PIXELS=STRIDE_PIXELS*HEIGHT;
    localparam integer FRAME_BURSTS=FRAME_PIXELS/64;
    localparam bit HAS_ALT=ALT_WIDTH!=WIDTH || ALT_HEIGHT!=HEIGHT;
    localparam bit HAS_WIDE=WIDE_WIDTH!=WIDTH || WIDE_HEIGHT!=HEIGHT;
    localparam bit PROFILED=HAS_ALT || HAS_WIDE;
    localparam integer PROFILE_BITS=HAS_WIDE ? 2:1;
    localparam integer WIDE_STRIDE_PIXELS=((WIDE_WIDTH+63)/64)*64;
    localparam integer WIDE_FRAME_PIXELS=WIDE_STRIDE_PIXELS*WIDE_HEIGHT;
    wire [1:0] requested_profile=HAS_WIDE ? source_profile : {1'b0,HAS_ALT && source_alt};
    reg [1:0] active_profile_pixel,frame_profile_control;
    localparam integer ALT_STRIDE_PIXELS=((ALT_WIDTH+63)/64)*64;
    localparam integer ALT_FRAME_PIXELS=ALT_STRIDE_PIXELS*ALT_HEIGHT;
    reg active_alt_pixel,active_weave_pixel;
    reg profile_launch;
    wire profile_ready,profile_valid;
    wire [PROFILE_BITS:0] profile_data;
    wire [1:0] received_profile=HAS_WIDE ? 2'(profile_data>>1):{1'b0,profile_data[1]};
    wire [31:0] received_base=HAS_WIDE && received_profile==2 ? WIDE_BASE:BASE;
    wire [5:0] received_shift=HAS_WIDE && received_profile==2 ? WIDE_SLOT_SHIFT:SLOT_SHIFT;
    wire [31:0] received_frame_base=received_base+({30'd0,producer[1:0]}<<received_shift);
    reg frame_alt_control,frame_weave_control,profile_received,profile_pending;
    wire [19:0] selected_frame_pixels=HAS_WIDE && active_profile_pixel==2 ? WIDE_FRAME_PIXELS:
        HAS_ALT && active_profile_pixel==1 ? ALT_FRAME_PIXELS:FRAME_PIXELS;
    wire [11:0] selected_width=HAS_WIDE && active_profile_pixel==2 ? WIDE_WIDTH:
        HAS_ALT && active_profile_pixel==1 ? ALT_WIDTH:WIDTH;
    wire [11:0] selected_height=HAS_WIDE && active_profile_pixel==2 ? WIDE_HEIGHT:
        HAS_ALT && active_profile_pixel==1 ? ALT_HEIGHT:HEIGHT;
    wire [11:0] selected_stride=HAS_WIDE && active_profile_pixel==2 ? WIDE_STRIDE_PIXELS:
        HAS_ALT && active_profile_pixel==1 ? ALT_STRIDE_PIXELS:STRIDE_PIXELS;
    wire [11:0] capture_right=PROFILED && !DYNAMIC_DISPLAY ? view_x+selected_width*SCALE_NUM/SCALE_DEN:view_right;
    wire [11:0] capture_bottom=PROFILED && !DYNAMIC_DISPLAY ? view_y+selected_height*SCALE_NUM/SCALE_DEN:view_bottom;
    wire [13:0] capture_padding_right=PROFILED ? {2'd0,capture_right}+selected_stride-selected_width:padding_right;
    wire mailbox_pixel_run;
    wire [15:0] selected_frame_bursts=HAS_WIDE && frame_profile_control==2 ? WIDE_FRAME_PIXELS/64:
        HAS_ALT && frame_profile_control==1 ? ALT_FRAME_PIXELS/64:FRAME_BURSTS;
    wire [5:0] row_bursts=HAS_WIDE && frame_profile_control==2 ? WIDE_STRIDE_PIXELS/64:
        HAS_ALT && frame_profile_control==1 ? ALT_STRIDE_PIXELS/64:STRIDE_PIXELS/64;
    wire [15:0] row_bytes=HAS_WIDE && frame_profile_control==2 ? WIDE_STRIDE_PIXELS*2:
        HAS_ALT && frame_profile_control==1 ? ALT_STRIDE_PIXELS*2:STRIDE_PIXELS*2;
    wire [9:0] half_rows=HAS_WIDE && frame_profile_control==2 ? WIDE_HEIGHT/2:
        HAS_ALT && frame_profile_control==1 ? ALT_HEIGHT/2:HEIGHT/2;
    // Return source profile through a coherent mailbox before dispatching any
    // DDR address. Data may already be queued safely in the ordinary FIFO.
    multitini_async_mailbox #(.WIDTH(PROFILE_BITS+1),.DRAIN_ON_RESET(1'b1)) profile_mailbox(
        .src_clk(pixel_clk),.src_resetn(mailbox_pixel_run),
        .src_data({PROFILE_BITS'(active_profile_pixel),active_weave_pixel}),.src_valid(profile_launch),.src_ready(profile_ready),
        .dst_clk(clk),.dst_resetn(resetn),.dst_data(profile_data),.dst_valid(profile_valid),.dst_ready(1'b1));
    localparam integer VIEW_WIDTH=WIDTH*SCALE_NUM/SCALE_DEN;
    localparam integer VIEW_HEIGHT=HEIGHT*SCALE_NUM/SCALE_DEN;
    // Synchronize PS reset separately. Mailboxes and FIFO stay alive through
    // a pixel-only reset so accepted AXI transactions can finish draining.
    generate if(SHARED_PIXEL_RESET)begin : g_shared_reset
        assign mailbox_pixel_run=shared_pixel_resetn;
    end else begin : g_local_reset
    (* ASYNC_REG = "TRUE" *) reg [2:0] mailbox_pixel_release;
    always @(posedge pixel_clk or negedge resetn)
        if(!resetn)mailbox_pixel_release<=0;
        else mailbox_pixel_release<={mailbox_pixel_release[1:0],1'b1};
    assign mailbox_pixel_run=mailbox_pixel_release[2];
    end endgenerate
    reg [2:0] pixel_release;
    always @(posedge pixel_clk)
        if(!mailbox_pixel_run || !pixel_resetn)pixel_release<=0;
        else pixel_release<={pixel_release[1:0],1'b1};
    wire pixel_run=pixel_release[2];
    generate if(DYNAMIC_DISPLAY)begin : g_dynamic_geometry
        // Mode changes occur while scanout is held in reset. Snapshot the
        // complete viewport, including exclusive bounds, before capture runs.
        // The existing three-cycle pixel_release barrier also settles a
        // geometry change arriving with reset release; no pixel is delayed.
        always @(posedge pixel_clk) if(!pixel_run)begin
            view_x<=display_view_x;
            view_y<=display_view_y;
            view_right<=display_view_x+display_view_width;
            view_bottom<=display_view_y+display_view_height;
            // The original native comparisons add 12-bit operands; padding
            // uses an integer constant and therefore must not wrap at 4096.
            padding_right<={2'b0,display_view_x}+{2'b0,display_view_width}+PAD_PIXELS;
            scale_num<=display_scale_num;
            scale_den<=display_scale_den;
        end
    end else begin : g_static_geometry
        assign view_x=12'(VIEW_X),view_y=12'(VIEW_Y);
        assign view_right=12'(VIEW_X)+12'(VIEW_WIDTH);
        assign view_bottom=12'(VIEW_Y)+12'(VIEW_HEIGHT);
        assign padding_right=14'(12'(VIEW_X))+14'(12'(VIEW_WIDTH))+PAD_PIXELS;
        assign scale_num=4'(SCALE_NUM),scale_den=4'(SCALE_DEN);
    end endgenerate
    wire enable_pixel;
    cdc_bit_sync running_sync(.clk(clk),.resetn(resetn),.din(pixel_run),.dout(pixel_running));
    cdc_bit_sync enable_sync(.clk(pixel_clk),.resetn(pixel_run),.din(enable),.dout(enable_pixel));
    wire arm_ready,arm_valid_pixel;
    reg arm_launch;
    wire unused_arm;
    reg capturing,capture_bad,finish_launch,finish_bad;
    wire finish_ready,finish_valid,finish_bad_control;
    multitini_async_mailbox #(.WIDTH(1),.DRAIN_ON_RESET(1'b1)) arm_mailbox(
        .src_clk(clk),.src_resetn(resetn),.src_data(1'b1),
        .src_valid(arm_launch),.src_ready(arm_ready),.dst_clk(pixel_clk),.dst_resetn(mailbox_pixel_run),
        .dst_data(unused_arm),.dst_valid(arm_valid_pixel),
        .dst_ready((!pixel_run || (vblank_start && !capturing && finish_ready)) && (!PROFILED || profile_ready)));
    multitini_async_mailbox #(.WIDTH(1),.DRAIN_ON_RESET(1'b1)) finish_mailbox(
        .src_clk(pixel_clk),.src_resetn(mailbox_pixel_run),.src_data(finish_bad),
        .src_valid(finish_launch),.src_ready(finish_ready),.dst_clk(clk),.dst_resetn(resetn),
        .dst_data(finish_bad_control),.dst_valid(finish_valid),.dst_ready(1'b1));

    // The source ratios are fixed for a core image. Track their small phases
    // instead of putting coordinate subtraction/modulo on the pixel-to-FIFO
    // write-enable path. At 3:2, output positions 0 and 2 capture each pair.
    reg [3:0] horizontal_phase,vertical_phase;
    always @(posedge pixel_clk)begin
        if(!pixel_run)begin horizontal_phase<=0;vertical_phase<=0;end
        else begin
            if(scan_x==view_x-1)horizontal_phase<=0;
            else if(scan_x>=view_x && scan_x<capture_right)
                horizontal_phase<=horizontal_phase==scale_num-1 ? 0 : horizontal_phase+1'b1;
            if(scan_x==0)begin
                if(scan_y<=view_y || scan_y>=capture_bottom)vertical_phase<=0;
                else vertical_phase<=vertical_phase==scale_num-1 ? 0 : vertical_phase+1'b1;
            end
        end
    end
    wire selected_row=scan_y>=view_y && scan_y<capture_bottom &&
        (scale_den==2 ? vertical_phase!=1 : vertical_phase==0);
    wire native_sample=scan_x>=view_x && scan_x<capture_right &&
        (scale_den==2 ? horizontal_phase!=1 : horizontal_phase==0);
    wire padding_sample=(PROFILED || PAD_PIXELS!=0) && scan_x>=capture_right &&
                        scan_x<capture_padding_right;
    wire sample=capturing && selected_row && (native_sample || padding_sample);
    wire [15:0] sampled_pixel=padding_sample?16'd0:rgb565;
    reg [1:0] pack_index;
    reg [47:0] pack;
    reg [19:0] sample_count;
    wire fifo_full,fifo_empty,fifo_read_busy,fifo_write_busy;
    wire [63:0] fifo_data;
    wire fifo_write=pixel_run && sample && pack_index==3 && !capture_bad && !fifo_full && !fifo_write_busy;
    wire fifo_read;
    wire [63:0] fifo_input={sampled_pixel,pack};
    reg skipped_toggle;
    wire skipped_control;
    reg skipped_previous;
    cdc_bit_sync skip_sync(.clk(clk),.resetn(resetn),.din(skipped_toggle),.dout(skipped_control));
    always @(posedge pixel_clk) begin
        if(!pixel_run) begin
            capturing<=0;capture_bad<=0;finish_launch<=0;finish_bad<=0;
            pack_index<=0;pack<=0;sample_count<=0;skipped_toggle<=0;
            active_alt_pixel<=0;active_profile_pixel<=0;active_weave_pixel<=0;profile_launch<=0;
            if(PROFILED && arm_valid_pixel && profile_ready)profile_launch<=1;
        end else begin
            finish_launch<=0;profile_launch<=0;
            if(vblank_start && enable_pixel && !(arm_valid_pixel && !capturing && finish_ready))
                skipped_toggle<=!skipped_toggle;
            if(arm_valid_pixel && vblank_start && !capturing && finish_ready && (!PROFILED || profile_ready)) begin
                pack_index<=0;sample_count<=0;capture_bad<=0;
                active_profile_pixel<=requested_profile;active_alt_pixel<=requested_profile==1;
                active_weave_pixel<=PROFILED && source_weave;
                profile_launch<=PROFILED;
                if(enable_pixel && machine_ready)capturing<=1;
                else begin finish_bad<=1;finish_launch<=1;end
            end
            if(capturing && (!machine_ready || !enable_pixel))capture_bad<=1;
            if(ABORT_INCOMPLETE_ON_VBLANK && vblank_start && capturing)begin
                capturing<=0;capture_bad<=1;finish_bad<=1;finish_launch<=1;
            end else if(sample) begin
                if(pack_index!=3)pack[pack_index*16+:16]<=sampled_pixel;
                pack_index<=pack_index+1'b1;
                sample_count<=sample_count+1'b1;
                if(pack_index==3 && (fifo_full || fifo_write_busy))capture_bad<=1;
                if(sample_count==selected_frame_pixels-1)begin
                    capturing<=0;
                    finish_bad<=capture_bad || !machine_ready || !enable_pixel || fifo_full || fifo_write_busy;
                    finish_launch<=1;
                end
            end
        end
    end

    // 256 x 64 bits (2 KiB) decouples the native samples from DDR stalls.
    // No data is accepted while either XPM reset-busy output is asserted.
    xpm_fifo_async #(.FIFO_MEMORY_TYPE("block"),.FIFO_WRITE_DEPTH(256),
        .WRITE_DATA_WIDTH(64),.READ_DATA_WIDTH(64),.READ_MODE("fwft"),
        .FIFO_READ_LATENCY(0),.CDC_SYNC_STAGES(2),.DOUT_RESET_VALUE("0"),
        .ECC_MODE("no_ecc"),.FULL_RESET_VALUE(0),.PROG_FULL_THRESH(240),
        .PROG_EMPTY_THRESH(16),.RD_DATA_COUNT_WIDTH(9),.WR_DATA_COUNT_WIDTH(9),
        .RELATED_CLOCKS(0),.SIM_ASSERT_CHK(1),.USE_ADV_FEATURES("0000"),.WAKEUP_TIME(0)) fifo_i(
        .rst(!mailbox_pixel_run),.wr_clk(pixel_clk),.wr_en(fifo_write),.din(fifo_input),
        .full(fifo_full),.wr_rst_busy(fifo_write_busy),.overflow(),.prog_full(),
        .wr_data_count(),.almost_full(),.wr_ack(),.rd_clk(clk),.rd_en(fifo_read),
        .dout(fifo_data),.empty(fifo_empty),.rd_rst_busy(fifo_read_busy),
        .underflow(),.prog_empty(),.rd_data_count(),.almost_empty(),.data_valid(),
        .sleep(1'b0),.injectsbiterr(1'b0),.injectdbiterr(1'b0),.sbiterr(),.dbiterr());
    localparam IDLE=0,FILL=1,ADDRESS=2,DATA=3,RESPONSE=4,DRAIN=5;
    reg [2:0] state;
    reg [63:0] staging[0:15];
    reg [4:0] staged;
    reg [3:0] beat;
    reg [3:0] drain_empty_count;
    reg [15:0] completed_bursts;
    reg ended,failed;
    reg [5:0] burst_in_row;
    reg [9:0] row_in_field;
    reg second_field;
    reg [31:0] frame_base;
    assign busy=state!=IDLE || !arm_ready || !fifo_empty || fifo_read_busy;
    assign quiescent=state==IDLE && arm_ready && !arm_launch && fifo_empty && !fifo_read_busy && (!PROFILED || !profile_pending);
    assign fifo_read=!fifo_empty && !fifo_read_busy &&
                     ((state==FILL && staged<16 && !failed) || state==DRAIN);
    assign awlen=15,awsize=3,awburst=1;
    assign awvalid=state==ADDRESS;
    assign wdata=staging[beat];
    assign wstrb=8'hff,wlast=beat==15,wvalid=state==DATA,bready=state==RESPONSE;
    always @(posedge clk) begin
        if(!resetn) begin
            state<=IDLE;producer<=0;drops<=0;errors<=0;awaddr<=BASE;
            arm_launch<=0;staged<=0;beat<=0;completed_bursts<=0;ended<=0;failed<=0;
            skipped_previous<=0;drain_empty_count<=0;
            slot_alt<=0;slot_profile<=0;frame_profile_control<=0;frame_alt_control<=0;frame_weave_control<=0;profile_received<=0;
            profile_pending<=0;
            burst_in_row<=0;row_in_field<=0;second_field<=0;frame_base<=BASE;
        end else begin
            arm_launch<=0;
            if(state!=DRAIN || !ended || !fifo_empty || fifo_read_busy)drain_empty_count<=0;
            else if(drain_empty_count!=15)drain_empty_count<=drain_empty_count+1'b1;
            skipped_previous<=skipped_control;
            if(skipped_control!=skipped_previous)drops<=drops+1'b1;
            if(finish_valid)begin ended<=1;failed<=failed || finish_bad_control;end
            if(PROFILED && profile_valid)begin
                frame_profile_control<=received_profile;frame_alt_control<=received_profile==1;
                frame_weave_control<=profile_data[0];profile_received<=1;profile_pending<=0;
                frame_base<=received_frame_base;awaddr<=received_frame_base;
            end
            // Unexpected pixel reset aborts this frame, but never cancels an
            // accepted AXI burst. ADDRESS/DATA/RESPONSE drain normally first.
            if(!pixel_running && state!=IDLE)begin ended<=1;failed<=1;end
            case(state)
                IDLE:if(!fifo_empty && !fifo_read_busy)begin
                    // Defensive cleanup for a discarded tail. Never start a
                    // new frame while an old FIFO word is still visible.
                    state<=DRAIN;ended<=1;failed<=1;
                end else if(enable && pixel_running && arm_ready && (producer-consumer)<4 && fifo_empty && !fifo_read_busy)begin
                    arm_launch<=1;state<=FILL;staged<=0;completed_bursts<=0;ended<=0;failed<=0;
                    awaddr<=BASE+({30'd0,producer[1:0]}<<SLOT_SHIFT);
                    frame_base<=BASE+({30'd0,producer[1:0]}<<SLOT_SHIFT);
                    profile_received<=!PROFILED;burst_in_row<=0;row_in_field<=0;second_field<=0;
                    profile_pending<=PROFILED;
                end
                FILL:begin
                    if(failed || (finish_valid && finish_bad_control))begin state<=DRAIN;staged<=0;end
                    else if(staged==16 && profile_received)begin state<=ADDRESS;beat<=0;end
                    else if(fifo_read)begin staging[staged]<=fifo_data;staged<=staged+1'b1;end
                    else if(ended && completed_bursts==selected_frame_bursts)begin
                        slot_alt[producer[1:0]]<=PROFILED && frame_alt_control;
                        slot_profile[producer[1:0]*2+:2]<=PROFILED ? frame_profile_control:2'd0;
                        producer<=producer+1'b1;state<=IDLE;
                    end
                end
                ADDRESS:if(awready)state<=DATA;
                DATA:if(wready)begin if(beat==15)state<=RESPONSE;else beat<=beat+1'b1;end
                RESPONSE:if(bvalid)begin
                    if(bresp[1])begin failed<=1;errors<=errors+1'b1;state<=DRAIN;end
                    else begin
                        completed_bursts<=completed_bursts+1'b1;
                        awaddr<=awaddr+128;
                        if(PROFILED && frame_weave_control)begin
                            if(burst_in_row==row_bursts-1)begin
                                burst_in_row<=0;row_in_field<=row_in_field+1'b1;
                                awaddr<=awaddr+row_bytes+128;
                                if(row_in_field==half_rows-1)begin
                                    row_in_field<=0;second_field<=1;
                                    if(!second_field)awaddr<=frame_base+row_bytes;
                                end
                            end else burst_in_row<=burst_in_row+1'b1;
                        end
                        staged<=0;state<=FILL;
                    end
                end
                // XPM's FWFT pointer/prefetch pipeline can reveal a final
                // word after pixel_running falls. Require eight settled
                // empty control clocks before acknowledging a discarded frame.
                DRAIN:if(ended && fifo_empty && !fifo_read_busy && drain_empty_count>=8)begin
                    state<=IDLE;drops<=drops+1'b1;
                end
                default:state<=IDLE;
            endcase
        end
    end
endmodule
