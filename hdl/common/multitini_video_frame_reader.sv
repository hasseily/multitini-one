// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Native RGB565 DDR scanout with three complete-line caches. The publisher
// submits only immutable completed output slots. Publication changes ownership
// at vblank; active_value acknowledges only after old AXI reads also drain, so
// the old slot can then be reused. A cache miss falls back to the direct machine
// path for the rest of that scanout frame, never reading uninitialized RAM.
module multitini_video_frame_reader #(
    parameter bit DYNAMIC_DISPLAY=1'b0,
    parameter integer WIDTH = 382,
    parameter integer HEIGHT = 270,
    parameter integer VIEW_X = 196,
    parameter integer VIEW_Y = 0,
    parameter integer SCALE_NUM = 4,
    parameter integer SCALE_DEN = 1,
    parameter integer H_ACTIVE = 1920, V_ACTIVE = 1080,
    parameter integer H_TOTAL = 2200, V_TOTAL = 1125,
    parameter [31:0] BASE = 32'h22400000,
    parameter bit SHARED_PIXEL_RESET = 1'b0,
    parameter bit SHARED_PIXEL_LIVENESS = 1'b0
) (
    input wire [11:0] display_view_x, display_view_y, display_view_width, display_view_height,
    input wire [3:0] display_scale_num, display_scale_den,
    input wire [11:0] display_h_active, display_v_active, display_h_total, display_v_total,
    input wire [9:0] display_source_x0, display_source_y0,
    input wire [3:0] display_phase_x0, display_phase_y0,
    input wire clk, resetn, enable,
    input wire submit,
    input wire [31:0] submit_value,
    output wire submit_ready,
    output reg [31:0] active_value,
    output reg active_valid,
    output wire busy, quiescent,
    output reg [31:0] error_count, underrun_count,
    input wire pixel_clk, pixel_resetn, shared_pixel_resetn, shared_pixel_running,
    input wire [11:0] scan_x, scan_y,
    input wire vblank_start,
    output wire pixel_use,
    output wire [15:0] pixel_rgb565,
    output reg [15:0] pixel_neighbor_rgb565,
    output reg [15:0] pixel_opposite_rgb565,
    output reg [7:0] pixel_neighbor_weight,
    output reg [31:0] araddr,
    output reg [3:0] arlen,
    output wire [2:0] arsize,
    output wire [1:0] arburst,
    output wire arvalid,
    input wire arready,
    input wire [63:0] rdata,
    input wire [1:0] rresp,
    input wire rlast, rvalid,
    output wire rready
);
    // Dynamic origins are signed full-image positions. Clipping changes the
    // first source sample and phase, never the native frame stored in DDR.
    wire signed [12:0] view_x=DYNAMIC_DISPLAY ? $signed({display_view_x[11],display_view_x}) : VIEW_X;
    wire signed [12:0] view_y=DYNAMIC_DISPLAY ? $signed({display_view_y[11],display_view_y}) : VIEW_Y;
    wire [11:0] view_width=DYNAMIC_DISPLAY ? display_view_width : WIDTH*SCALE_NUM/SCALE_DEN;
    wire [11:0] view_height=DYNAMIC_DISPLAY ? display_view_height : HEIGHT*SCALE_NUM/SCALE_DEN;
    wire [11:0] h_active=DYNAMIC_DISPLAY ? display_h_active : H_ACTIVE;
    wire [11:0] v_active=DYNAMIC_DISPLAY ? display_v_active : V_ACTIVE;
    wire [11:0] h_total=DYNAMIC_DISPLAY ? display_h_total : H_TOTAL;
    wire [11:0] v_total=DYNAMIC_DISPLAY ? display_v_total : V_TOTAL;
    localparam integer STATIC_CROP_X=VIEW_X<0 ? -VIEW_X : 0;
    localparam integer STATIC_CROP_Y=VIEW_Y<0 ? -VIEW_Y : 0;
    wire signed [13:0] full_end_x=$signed({view_x[12],view_x})+$signed({2'b0,view_width});
    wire signed [13:0] full_end_y=$signed({view_y[12],view_y})+$signed({2'b0,view_height});
    wire [11:0] visible_x0,visible_y0,visible_x1,visible_y1;
    wire [11:0] prefetch_x0,prefetch_x1,prefetch_y0,prefetch_y1,last_x,last_y;
    wire [11:0] prepare_x,left_x,preload_x,start_x;
    wire clipped_lead,prepare_wrap,left_wrap,preload_wrap,start_wrap;
    wire first_x_zero,first_y_zero,end_x_nonzero,end_y_nonzero;
    wire [9:0] source_x0,source_y0;
    wire [3:0] phase_x0,phase_y0,scale_num,scale_den;
    localparam integer WORDS = (WIDTH + 3) / 4;
    localparam integer STRIDE = ((WIDTH + 63) / 64) * 128;
    localparam integer OUT_WIDTH = WIDTH * SCALE_NUM / SCALE_DEN;
    localparam integer OUT_HEIGHT = HEIGHT * SCALE_NUM / SCALE_DEN;
    localparam integer REQUEST_WIDTH = 85; // start word9, epoch32, value32, row10, bank2
    localparam integer REPLY_WIDTH = 45;   // success1, epoch32, row10, bank2
    localparam [2:0] IDLE=0, ISSUE=1, RECEIVE=2, REPLY=3;
    (* ASYNC_REG = "TRUE" *) reg [2:0] reset_control;
    reg [2:0] reset_pixel;
    wire mailbox_pixel_run;
    always @(posedge clk or negedge resetn)
        if (!resetn) reset_control <= 0;
        else reset_control <= {reset_control[1:0],1'b1};
    always @(posedge pixel_clk)
        if (!mailbox_pixel_run || !pixel_resetn) reset_pixel <= 0;
        else reset_pixel <= {reset_pixel[1:0],1'b1};
    wire control_run = reset_control[2], pixel_run = reset_pixel[2];
    // Runtime geometry is stable while the display transaction holds pixels
    // reset. Decode it there, in four short stages, during the caller's held
    // settle interval before pixel release. Running scanout compares stored
    // coordinates; clipping
    // and warmup arithmetic never feed its per-pixel clock enables.
    generate if(DYNAMIC_DISPLAY)begin : g_geometry_snapshot
        reg [11:0] x0_1,y0_1,ha_1,va_1,ht_1,vt_1;
        reg signed [13:0] xe_1,ye_1;
        reg [35:0] seeds_1,seeds_2,seeds_3,seeds_4;
        reg [11:0] x0_2,y0_2,x1_2,y1_2,ht_2,vt_2;
        reg [11:0] x0_3,y0_3,x1_3,y1_3,base_3,last_x_3,last_y_3;
        reg [3:0] wraps_3,wraps_4;
        reg clipped_3,clipped_4;
        reg [11:0] x0_4,y0_4,x1_4,y1_4,px0_4,px1_4,py0_4,py1_4,lx_4,ly_4;
        reg [11:0] prepare_4,left_4,preload_4,start_4;
        reg first_x_zero_4,first_y_zero_4,end_x_nonzero_4,end_y_nonzero_4;
        always @(posedge pixel_clk)if(!pixel_run)begin
            x0_1<=view_x[12] ? 12'd0:view_x[11:0];y0_1<=view_y[12] ? 12'd0:view_y[11:0];
            xe_1<=full_end_x;ye_1<=full_end_y;
            ha_1<=h_active;va_1<=v_active;ht_1<=h_total;vt_1<=v_total;
            seeds_1<={display_source_x0,display_source_y0,display_phase_x0,display_phase_y0,display_scale_num,display_scale_den};

            x0_2<=x0_1>ha_1 ? ha_1:x0_1;y0_2<=y0_1>va_1 ? va_1:y0_1;
            x1_2<=xe_1<=0 ? 12'd0:xe_1>$signed({2'b0,ha_1}) ? ha_1:xe_1[11:0];
            y1_2<=ye_1<=0 ? 12'd0:ye_1>$signed({2'b0,va_1}) ? va_1:ye_1[11:0];
            ht_2<=ht_1;vt_2<=vt_1;seeds_2<=seeds_1;

            x0_3<=x0_2;y0_3<=y0_2;x1_3<=x1_2;y1_3<=y1_2;seeds_3<=seeds_2;
            base_3<=ht_2+x0_2;last_x_3<=ht_2-12'd1;last_y_3<=vt_2-12'd1;
            wraps_3<={x0_2<12'd5,x0_2<12'd4,x0_2<12'd3,x0_2<12'd2};
            clipped_3<=x0_2<12'd4 || seeds_2[35:8]!=0;

            x0_4<=x0_3;y0_4<=y0_3;x1_4<=x1_3;y1_4<=y1_3;seeds_4<=seeds_3;
            px0_4<=x0_3-12'd1;px1_4<=x1_3-12'd1;py0_4<=y0_3-12'd1;py1_4<=y1_3-12'd1;
            lx_4<=last_x_3;ly_4<=last_y_3;wraps_4<=wraps_3;clipped_4<=clipped_3;
            prepare_4<=(wraps_3[3] ? base_3:x0_3)-12'd5;
            left_4<=(wraps_3[2] ? base_3:x0_3)-12'd4;
            preload_4<=(wraps_3[1] ? base_3:x0_3)-12'd3;
            start_4<=(wraps_3[0] ? base_3:x0_3)-12'd2;
            first_x_zero_4<=x0_3==0;first_y_zero_4<=y0_3==0;
            end_x_nonzero_4<=x1_3!=0;end_y_nonzero_4<=y1_3!=0;
        end
        assign {source_x0,source_y0,phase_x0,phase_y0,scale_num,scale_den}=seeds_4;
        assign visible_x0=x0_4;assign visible_y0=y0_4;assign visible_x1=x1_4;assign visible_y1=y1_4;
        assign prefetch_x0=px0_4;assign prefetch_x1=px1_4;assign prefetch_y0=py0_4;assign prefetch_y1=py1_4;
        assign last_x=lx_4;assign last_y=ly_4;
        assign {prepare_wrap,left_wrap,preload_wrap,start_wrap}=wraps_4;
        assign clipped_lead=clipped_4;
        assign prepare_x=prepare_4;assign left_x=left_4;assign preload_x=preload_4;assign start_x=start_4;
        assign first_x_zero=first_x_zero_4;assign first_y_zero=first_y_zero_4;
        assign end_x_nonzero=end_x_nonzero_4;assign end_y_nonzero=end_y_nonzero_4;
    end else begin : g_constant_geometry
        assign source_x0=STATIC_CROP_X*SCALE_DEN/SCALE_NUM;assign source_y0=STATIC_CROP_Y*SCALE_DEN/SCALE_NUM;
        assign phase_x0=STATIC_CROP_X*SCALE_DEN%SCALE_NUM;assign phase_y0=STATIC_CROP_Y*SCALE_DEN%SCALE_NUM;
        assign scale_num=SCALE_NUM;assign scale_den=SCALE_DEN;
        assign visible_x0=view_x<0 ? 12'd0:view_x>$signed({1'b0,h_active}) ? h_active:view_x[11:0];
        assign visible_y0=view_y<0 ? 12'd0:view_y>$signed({1'b0,v_active}) ? v_active:view_y[11:0];
        assign visible_x1=full_end_x<=0 ? 12'd0:full_end_x>$signed({2'b0,h_active}) ? h_active:full_end_x[11:0];
        assign visible_y1=full_end_y<=0 ? 12'd0:full_end_y>$signed({2'b0,v_active}) ? v_active:full_end_y[11:0];
        assign prefetch_x0=visible_x0-12'd1;assign prefetch_x1=visible_x1-12'd1;
        assign prefetch_y0=visible_y0-12'd1;assign prefetch_y1=visible_y1-12'd1;
        assign last_x=h_total-12'd1;assign last_y=v_total-12'd1;
        assign prepare_wrap=visible_x0<12'd5;assign left_wrap=visible_x0<12'd4;
        assign preload_wrap=visible_x0<12'd3;assign start_wrap=visible_x0<12'd2;
        assign clipped_lead=visible_x0<12'd4 || source_x0!=0 || source_y0!=0 || phase_x0!=0 || phase_y0!=0;
        assign prepare_x=(prepare_wrap ? h_total+visible_x0:visible_x0)-12'd5;
        assign left_x=(left_wrap ? h_total+visible_x0:visible_x0)-12'd4;
        assign preload_x=(preload_wrap ? h_total+visible_x0:visible_x0)-12'd3;
        assign start_x=(start_wrap ? h_total+visible_x0:visible_x0)-12'd2;
        assign first_x_zero=visible_x0==0;assign first_y_zero=visible_y0==0;
        assign end_x_nonzero=visible_x1!=0;assign end_y_nonzero=visible_y1!=0;
    end endgenerate
    wire pixel_live;
    generate if(SHARED_PIXEL_LIVENESS)begin : g_shared_liveness
        // The wrapper shares capture's single return crossing. Independently
        // sampling the same pixel reset could disagree during a transition.
        assign pixel_live=shared_pixel_running;
    end else begin : g_local_liveness
        (* ASYNC_REG = "TRUE" *) reg [2:0] live_sync;
        always @(posedge clk) begin
            if (!control_run) live_sync<=0;
            else live_sync<={live_sync[1:0],pixel_run};
        end
        assign pixel_live=live_sync[2];
    end endgenerate
    // XPM handshakes have no reset input. Keep their wrappers alive through
    // pixel resets and acknowledge discarded messages; clearing an in-flight
    // source handshake could make its next request look already acknowledged.
    generate if(SHARED_PIXEL_RESET)begin : g_shared_reset
        assign mailbox_pixel_run=shared_pixel_resetn;
    end else begin : g_local_reset
    (* ASYNC_REG = "TRUE" *) reg [2:0] mailbox_pixel_release;
    always @(posedge pixel_clk or negedge resetn)
        if (!resetn) mailbox_pixel_release<=0;
        else mailbox_pixel_release<={mailbox_pixel_release[1:0],1'b1};
    assign mailbox_pixel_run=mailbox_pixel_release[2];
    end endgenerate
    wire mailbox_control_run=control_run;

    // Bundled ownership publication. Old-frame prefetch continues until
    // vertical blank; queuing a frame mid-scan must not starve that scanout.
    reg pending, publish_launched, publish_pulse;
    reg [32:0] publish_data;
    wire publish_ready, publish_valid_pixel;
    wire [32:0] publish_pixel;
    reg [2:0] state;
    wire publish_accept_pixel = vblank_start || !pixel_run;
    multitini_async_mailbox #(.WIDTH(33),.DRAIN_ON_RESET(1'b1)) publication (
        .src_clk(clk),.src_resetn(mailbox_control_run),.src_data(publish_data),
        .src_valid(publish_pulse),.src_ready(publish_ready),
        .dst_clk(pixel_clk),.dst_resetn(mailbox_pixel_run),.dst_data(publish_pixel),
        .dst_valid(publish_valid_pixel),.dst_ready(publish_accept_pixel)
    );
    assign submit_ready = mailbox_control_run && pixel_live && enable && !pending && publish_ready;
    wire accepted_submit = submit && submit_ready;
    wire begin_disable = !enable && active_valid && !pending && publish_ready;
    wire complete_publication = pending && publish_launched &&
                                !publish_pulse && publish_ready && state==IDLE;
    assign busy = pending || state != IDLE;
    // With enable low every stale pixel-domain request is acknowledged as a
    // cancelled row without issuing AXI. Quiescence therefore remains safe
    // even if a stale mailbox request is still crossing the clock boundary.
    assign quiescent = control_run && !enable && !active_valid && !pending && state == IDLE;

    wire request_ready_pixel, request_valid_control;
    reg request_valid_pixel;
    reg [REQUEST_WIDTH-1:0] request_pixel;
    wire [REQUEST_WIDTH-1:0] request_control;
    wire request_accept_control = state == IDLE && !complete_publication &&
        (!pending || request_control[43:12] == active_value);
    multitini_async_mailbox #(.WIDTH(REQUEST_WIDTH),.DRAIN_ON_RESET(1'b1)) line_request (
        .src_clk(pixel_clk),.src_resetn(mailbox_pixel_run),.src_data(request_pixel),
        .src_valid(request_valid_pixel),.src_ready(request_ready_pixel),
        .dst_clk(clk),.dst_resetn(mailbox_control_run),.dst_data(request_control),
        .dst_valid(request_valid_control),.dst_ready(request_accept_control)
    );
    reg [REPLY_WIDTH-1:0] reply_control;
    wire [REPLY_WIDTH-1:0] reply_pixel;
    wire reply_ready_control, reply_valid_pixel;
    wire reply_launch = state == REPLY;
    multitini_async_mailbox #(.WIDTH(REPLY_WIDTH),.DRAIN_ON_RESET(1'b1)) line_reply (
        .src_clk(clk),.src_resetn(mailbox_control_run),.src_data(reply_control),
        .src_valid(reply_launch),.src_ready(reply_ready_control),
        .dst_clk(pixel_clk),.dst_resetn(mailbox_pixel_run),.dst_data(reply_pixel),
        .dst_valid(reply_valid_pixel),.dst_ready(1'b1)
    );

    // A 64-bit simple dual-port RAM allows one AXI beat to be stored at a
    // time. Pixel side reads one word synchronously and selects one of its
    // four pixels; no asynchronous or uninitialized data reaches scanout.
    (* ram_style = "block" *) reg [63:0] cache [0:3*WORDS-1];
    reg [1:0] fill_bank;
    reg [9:0] fill_row;
    reg [31:0] fill_epoch;
    reg [8:0] fill_word;
    reg [4:0] beat_index, burst_words;
    reg row_failed;
    assign arsize = 3'd3;
    assign arburst = 2'b01;
    assign arvalid = control_run && state == ISSUE;
    assign rready = control_run && state == RECEIVE;
    wire take_read = rvalid && rready;
    wire response_error = rresp != 0 || (rlast != (beat_index == burst_words-1));
    // Native row strides are 128-byte aligned. A cropped first burst stops
    // at the next 16-word boundary, so later full bursts cannot cross 4 KB.
    wire [4:0] first_burst_limit=5'd16-{1'b0,request_control[79:76]};
    always @(posedge clk) begin
        if (take_read && beat_index < burst_words &&
            fill_word + beat_index < WORDS)
            cache[fill_bank*WORDS + fill_word + beat_index] <= rdata;
    end

    (* ASYNC_REG = "TRUE" *) reg [2:0] underrun_sync;
    reg underrun_seen;
    reg underrun_toggle;
    always @(posedge clk) begin
        if (!control_run) begin
            pending<=0; publish_launched<=0; publish_pulse<=0; publish_data<=0;
            active_value<=0; active_valid<=0; state<=IDLE;
            araddr<=0; arlen<=0; fill_bank<=0; fill_row<=0; fill_epoch<=0;
            fill_word<=0; beat_index<=0; burst_words<=0; row_failed<=0;
            reply_control<=0; error_count<=0; underrun_count<=0;
            underrun_sync<=0; underrun_seen<=0;
        end else begin
            publish_pulse<=0;
            underrun_sync<={underrun_sync[1:0],underrun_toggle};
            if (underrun_sync[2] != underrun_seen) begin
                underrun_count<=underrun_count+1'b1;
                underrun_seen<=underrun_sync[2];
            end
            if (submit && !submit_ready)
                error_count<=error_count+1'b1;
            if (accepted_submit || begin_disable) begin
                pending<=1; publish_launched<=0;
                publish_data<={accepted_submit,accepted_submit ? submit_value : active_value};
            end
            if (pending && !publish_launched && publish_ready) begin
                // A disable before launch supersedes a queued frame. Once a
                // mailbox is in flight its payload is immutable.
                if (!enable) publish_data[32]<=0;
                publish_pulse<=1;
                publish_launched<=1;
            end else if (complete_publication) begin
                active_value<=publish_data[31:0];
                active_valid<=publish_data[32];
                pending<=0; publish_launched<=0;
            end

            case (state)
            IDLE: if (request_valid_control && request_accept_control && pixel_live) begin
                fill_bank<=request_control[1:0];
                fill_row<=request_control[11:2];
                fill_epoch<=request_control[75:44];
                fill_word<=request_control[84:76]; beat_index<=0; row_failed<=0;
                if (!enable || !active_valid || request_control[43:12] != active_value ||
                    request_control[11:2] >= HEIGHT || request_control[1:0] >= 3 ||
                    request_control[84:76] >= WORDS) begin
                    reply_control<={1'b0,request_control[75:44],request_control[11:0]};
                    state<=REPLY;
                end else begin
                    araddr<=BASE + ({30'd0,active_value[1:0]} << 20) +
                            request_control[11:2]*STRIDE+({23'd0,request_control[84:76]} << 3);
                    arlen<=WORDS-request_control[84:76] >= first_burst_limit ? first_burst_limit-1'b1 : WORDS-request_control[84:76]-1;
                    burst_words<=WORDS-request_control[84:76] >= first_burst_limit ? first_burst_limit : WORDS-request_control[84:76];
                    state<=ISSUE;
                end
            end
            ISSUE: if (arready) begin beat_index<=0; state<=RECEIVE; end
            RECEIVE: if (take_read) begin
                if (response_error) row_failed<=1;
                if (rlast) begin
                    if (!pixel_live) state<=IDLE;
                    else if (row_failed || response_error) begin
                        error_count<=error_count+1'b1;
                        reply_control<={1'b0,fill_epoch,fill_row,fill_bank};
                        state<=REPLY;
                    end else if (fill_word + burst_words >= WORDS) begin
                        reply_control<={1'b1,fill_epoch,fill_row,fill_bank};
                        state<=REPLY;
                    end else begin
                        fill_word<=fill_word+burst_words;
                        araddr<=araddr+({27'd0,burst_words} << 3);
                        arlen<=WORDS-(fill_word+burst_words) >= 16 ?
                               4'd15 : WORDS-(fill_word+burst_words)-1;
                        burst_words<=WORDS-(fill_word+burst_words) >= 16 ?
                                     5'd16 : WORDS-(fill_word+burst_words);
                        state<=ISSUE;
                    end
                end else if (beat_index < 31) beat_index<=beat_index+1'b1;
                // A missing RLAST drains indefinitely with rready asserted;
                // it never wraps the RAM index or pretends the bus is idle.
            end
            REPLY: if (reply_ready_control || !pixel_live) state<=IDLE;
            default: state<=IDLE;
            endcase
            // A pixel-domain reset must never cancel an accepted/held AXI
            // transaction. Invalidate ownership only after the current burst
            // has drained. Mailboxes reset independently while the bus drains.
            if (!pixel_live && state==IDLE) begin
                active_valid<=0;pending<=0;publish_launched<=0;publish_pulse<=0;
            end
        end
    end

    reg enabled_pixel, frame_failed, row_outstanding;
    reg [31:0] value_pixel, epoch;
    reg pixel_reset_seen;
    reg [43:0] outstanding_token; // epoch32, row10, bank2
    reg [9:0] next_row, current_row;
    reg [2:0] line_valid;
    reg [9:0] line_row [0:2];
    reg [31:0] line_epoch [0:2];
    reg [63:0] cache_word, current_word, previous_word;
    reg [7:0] current_word_index;
    reg [15:0] pixel_center;
    reg pixel_cached;
    reg [9:0] source_x, source_y;
    reg [1:0] read_bank, next_bank;
    reg [3:0] horizontal_phase, vertical_phase;
    // One pixel lookahead compensates the synchronous BRAM read. The
    // existing machine bridge presents data at the current scan coordinate.
    wire at_last_x=scan_x==last_x,at_last_y=scan_y==last_y;
    wire row_visible=scan_y>=visible_y0 && scan_y<visible_y1;
    // Compare the current counters with held-time shifted bounds instead of
    // adding one at pixel rate before each range check. Wrap uses two flags.
    wire following_row_visible=end_y_nonzero && (at_last_y ? first_y_zero :
        (first_y_zero || scan_y>=prefetch_y0) && scan_y<prefetch_y1);
    wire prefetch = end_x_nonzero && (clipped_lead && at_last_x ?
        first_x_zero && end_x_nonzero && following_row_visible :
        (!clipped_lead || !at_last_x) && (first_x_zero || scan_x>=prefetch_x0) && scan_x<prefetch_x1 && row_visible);
    wire visible = scan_x>=visible_x0 && scan_x<visible_x1 && row_visible;
    wire cache_hit = line_valid[read_bank] && line_row[read_bank] == source_y &&
                     line_epoch[read_bank] == epoch;
    assign pixel_use = enabled_pixel && !frame_failed && visible && pixel_cached;
    assign pixel_rgb565 = pixel_center;
    // One synchronous BRAM read port supplies a previous/current/next word
    // window. Prefetching the next packed word costs no additional DDR reads.
    // Centered Auto keeps its existing x=0 bank advance and two-word warmup.
    // Cropped/zero-margin images prepare the next row during horizontal blank,
    // then load previous/current/next words before the first visible sample.
    wire row_prepare=clipped_lead ? scan_x==prepare_x : scan_x==0;
    wire prepare_first_or_blank=!end_y_nonzero || (clipped_lead && prepare_wrap ?
        at_last_y || scan_y<visible_y0 || scan_y>=prefetch_y1 : scan_y<=visible_y0 || scan_y>=visible_y1);
    wire preload_left=clipped_lead && scan_x==left_x &&
        (left_wrap ? following_row_visible : row_visible);
    wire preload=scan_x==preload_x && (preload_wrap ? following_row_visible : row_visible);
    wire start_line=scan_x==start_x && (start_wrap ? following_row_visible : row_visible);
    wire [8:0] first_word={1'b0,source_x0[9:2]};
    wire [8:0] first_fill_word=first_word==0 ? 9'd0 : first_word-1'b1;
    wire word_advance = source_x[9:2] != current_word_index;
    wire [63:0] center_word = word_advance ? cache_word : current_word;
    wire [63:0] left_word = word_advance ? current_word : previous_word;
    wire [15:0] center_pixel = center_word[source_x[1:0]*16+:16];
    wire signed [5:0] centered_phase = $signed({1'b0,horizontal_phase,1'b0}) +
        $signed({2'b0,scale_den}) - $signed({2'b0,scale_num});
    wire [5:0] phase_distance = centered_phase[5] ? -centered_phase : centered_phase;
    function automatic [7:0] phase_weight(input [5:0] distance, input [3:0] numerator);
        // Fixed small divisors synthesize as a tiny phase table, not a wide
        // variable divider on the pixel clock. Values are nearest Q8 weights.
        begin
            case(numerator)
                2:phase_weight=distance*64;
                3:case(distance) 1:phase_weight=43;2:phase_weight=85;3:phase_weight=128;default:phase_weight=0;endcase
                4:phase_weight=distance*32;
                5:case(distance) 1:phase_weight=26;2:phase_weight=51;3:phase_weight=77;4:phase_weight=102;default:phase_weight=0;endcase
                6:case(distance) 1:phase_weight=21;2:phase_weight=43;3:phase_weight=64;4:phase_weight=85;5:phase_weight=107;default:phase_weight=0;endcase
                default:phase_weight=0;
            endcase
        end
    endfunction
    wire [15:0] left_pixel = source_x == 0 ? center_pixel :
        (source_x[1:0] == 0 ? left_word[63:48] : center_word[(source_x[1:0]-2'd1)*16+:16]);
    wire [15:0] right_pixel = source_x == WIDTH-1 ? center_pixel :
        (source_x[1:0] == 3 ? cache_word[15:0] : center_word[(source_x[1:0]+2'd1)*16+:16]);
    // Replies can outlive a pixel-only reset because accepted AXI reads must
    // drain. Never reuse their epoch when scanout restarts. PS reset alone
    // initializes the epoch; its mailbox recovery discards pre-reset traffic.
    always @(posedge pixel_clk) begin
        if (!mailbox_pixel_run) begin epoch<=0; pixel_reset_seen<=0; end
        else if (!pixel_run) begin
            if (!pixel_reset_seen) epoch<=epoch+1'b1;
            pixel_reset_seen<=1;
        end else begin
            pixel_reset_seen<=0;
            if (vblank_start) epoch<=epoch+1'b1;
        end
    end
    integer bank;
    always @(posedge pixel_clk) begin
        if (preload_left || preload || start_line || (prefetch && source_x < WIDTH))
            cache_word<=cache[read_bank*WORDS + (preload_left ? first_fill_word : preload ? first_word :
                start_line ? (first_word+1 < WORDS ? first_word+1 : first_word) :
                (source_x[9:2]+1 < WORDS ? source_x[9:2]+1 : source_x[9:2]))];
        if (!pixel_run) begin
            enabled_pixel<=0; frame_failed<=0; value_pixel<=0;
            next_row<=0; current_row<=0; line_valid<=0; row_outstanding<=0;
            outstanding_token<=0;
            request_valid_pixel<=0; request_pixel<=0; underrun_toggle<=0;
            pixel_center<=0;pixel_neighbor_rgb565<=0;pixel_opposite_rgb565<=0;pixel_neighbor_weight<=0;pixel_cached<=0;
            current_word<=0;previous_word<=0;current_word_index<=0;
            source_x<=0;source_y<=0;read_bank<=0;next_bank<=0;
            horizontal_phase<=0;vertical_phase<=0;
            for (bank=0;bank<3;bank=bank+1) begin line_row[bank]<=0; line_epoch[bank]<=0; end
        end else begin
            request_valid_pixel<=0;
            // Fractional phase accumulators replace pixel-rate division by
            // three/six. Each source row repeats at the existing core ratio;
            // the circular cache bank advances only when source_y advances.
            if (row_prepare) begin
                if (prepare_first_or_blank) begin
                    source_y<=source_y0;vertical_phase<=phase_y0;read_bank<=0;
                end else if (vertical_phase+scale_den>=scale_num) begin
                    source_y<=source_y+1'b1;
                    vertical_phase<=vertical_phase+scale_den-scale_num;
                    read_bank<=read_bank==2 ? 0 : read_bank+1'b1;
                end else vertical_phase<=vertical_phase+scale_den;
            end
            if (preload && clipped_lead) previous_word<=cache_word;
            if (start_line) begin
                source_x<=source_x0;horizontal_phase<=phase_x0;
                current_word<=cache_word;current_word_index<=source_x0[9:2];
                if(!clipped_lead)previous_word<=cache_word;
            end else if (prefetch) begin
                if (horizontal_phase+scale_den>=scale_num) begin
                    source_x<=source_x+1'b1;
                    horizontal_phase<=horizontal_phase+scale_den-scale_num;
                end else horizontal_phase<=horizontal_phase+scale_den;
                if(word_advance)begin
                    previous_word<=current_word;current_word<=cache_word;current_word_index<=source_x[9:2];
                end
            end
            pixel_center<=center_pixel;
            pixel_neighbor_rgb565<=centered_phase[5] ? left_pixel : right_pixel;
            pixel_opposite_rgb565<=centered_phase[5] ? right_pixel : left_pixel;
            pixel_neighbor_weight<=prefetch && cache_hit ? phase_weight(phase_distance,scale_num) : 0;
            pixel_cached<=prefetch && cache_hit;
            if (prefetch) current_row<=source_y;
            if (enabled_pixel && !frame_failed && prefetch && !cache_hit) begin
                frame_failed<=1;
                underrun_toggle<=!underrun_toggle;
            end
            if (reply_valid_pixel && row_outstanding &&
                reply_pixel[43:0] == outstanding_token) begin
                row_outstanding<=0;
                if (reply_pixel[43:12] == epoch && reply_pixel[44]) begin
                    line_valid[reply_pixel[1:0]]<=1;
                    line_row[reply_pixel[1:0]]<=reply_pixel[11:2];
                    line_epoch[reply_pixel[1:0]]<=reply_pixel[43:12];
                end
            end
            if (enabled_pixel && !frame_failed && !row_outstanding &&
                request_ready_pixel && !request_valid_pixel && next_row < HEIGHT &&
                next_row <= current_row+2 && !vblank_start) begin
                request_pixel<={first_fill_word,epoch,value_pixel,next_row,next_bank};
                outstanding_token<={epoch,next_row,next_bank};
                request_valid_pixel<=1; row_outstanding<=1;
                line_valid[next_bank]<=0;
                next_row<=next_row+1'b1;
                next_bank<=next_bank==2 ? 0 : next_bank+1'b1;
            end
            if (vblank_start) begin
                next_row<=source_y0; current_row<=source_y0; line_valid<=0;
                next_bank<=0;
                frame_failed<=0; pixel_cached<=0;pixel_neighbor_weight<=0;
                if (publish_valid_pixel) begin
                    value_pixel<=publish_pixel[31:0];
                    enabled_pixel<=publish_pixel[32];
                end
            end
        end
    end
endmodule
