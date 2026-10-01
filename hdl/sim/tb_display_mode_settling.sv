// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Mode switching crosses the always-on mailbox before the complete geometry
// bundle is registered. The scanout hold must outlast that configuration path.
module tb_display_mode_settling;
    import multitini_display_pkg::*;
    reg control_clk=0,pixel_clk=0,resetn=0,request=0,quiescent=0,frame_latched=0;
    always #3.75 control_clk=~control_clk;
    realtime pixel_half=3.367;
    reg pixel_clock_enabled=1;
    initial forever begin
        #(pixel_half);
        if(pixel_clock_enabled)pixel_clk=~pixel_clk;
        else pixel_clk=0;
    end
    reg [3:0] requested_mode=0;
    wire [3:0] active_mode;
    wire commit,busy,error,pause_reader,hold_video,clock_resetn;
    wire [10:0] awaddr;
    wire [31:0] wdata;
    wire awvalid,wvalid,bready;
    reg awready=0,wready=0,bvalid=0,locked=1;
    wire geometry_ready;
    reg geometry_pending=0,geometry_seen=0;
    multitini_display_control #(.TIMEOUT_CYCLES(4096)) control_i(
        .clk(control_clk),.resetn(resetn),.request(request),.requested_mode(requested_mode),
        .reader_quiescent(quiescent),.frame_latched(frame_latched),
        .geometry_ready(geometry_ready && !geometry_pending),.clock_locked(locked),
        .active_mode(active_mode),.commit(commit),.busy(busy),.error(error),
        .reader_pause(pause_reader),.video_hold(hold_video),.clock_resetn(clock_resetn),
        .clock_awaddr(awaddr),.clock_awvalid(awvalid),.clock_awready(awready),
        .clock_wdata(wdata),.clock_wvalid(wvalid),.clock_wready(wready),
        .clock_bresp(2'd0),.clock_bvalid(bvalid),.clock_bready(bready));

    reg [2:0] video_control_release=0;
    always @(posedge pixel_clk or negedge resetn)
        if(!resetn)video_control_release<=0;
        else video_control_release<={video_control_release[1:0],1'b1};
    wire video_control_resetn=video_control_release[2];
    wire video_resetn;
    multitini_display_reset display_reset_i(.clk(pixel_clk),.clock_resetn(resetn && locked),
        .hold_request(hold_video),.resetn(video_resetn));
    wire [3:0] mode_payload;
    wire mode_valid;
    reg [3:0] pixel_mode;
    multitini_async_mailbox #(.WIDTH(4),.DRAIN_ON_RESET(1'b1)) mode_mailbox(
        .src_clk(control_clk),.src_resetn(resetn),.src_data(active_mode),
        .src_valid(commit),.src_ready(geometry_ready),.dst_clk(pixel_clk),
        .dst_resetn(video_control_resetn),.dst_data(mode_payload),
        .dst_valid(mode_valid),.dst_ready(1'b1));
    always @(posedge control_clk)begin
        if(!resetn)begin geometry_pending<=0;geometry_seen<=0;end
        else if(commit)begin geometry_pending<=1;geometry_seen<=0;end
        else if(geometry_pending)begin
            if(!geometry_ready)geometry_seen<=1;
            if(geometry_seen && geometry_ready)geometry_pending<=0;
        end
    end
    always @(posedge pixel_clk)begin
        if(!video_control_resetn)pixel_mode<=0;
        else if(mode_valid)pixel_mode<=mode_payload;
    end
    wire [11:0] raw_ha,raw_hf,raw_hs,raw_ht,raw_va,raw_vf,raw_vs,raw_vt;
    wire raw_hp,raw_vp;
    wire [3:0] raw_sn,raw_sd;
    wire [11:0] raw_x,raw_y,raw_w,raw_h;
    multitini_display_geometry geometry_i(.mode(pixel_mode),
        .h_active(raw_ha),.h_front(raw_hf),.h_sync(raw_hs),.h_total(raw_ht),
        .v_active(raw_va),.v_front(raw_vf),.v_sync(raw_vs),.v_total(raw_vt),
        .h_positive(raw_hp),.v_positive(raw_vp),.scale_num(raw_sn),.scale_den(raw_sd),
        .view_x(raw_x),.view_y(raw_y),.view_width(raw_w),.view_height(raw_h));
    wire [153:0] raw_geometry={raw_ha,raw_hf,raw_hs,raw_ht,raw_va,raw_vf,raw_vs,raw_vt,
        raw_hp,raw_vp,raw_sn,raw_sd,raw_x,raw_y,raw_w,raw_h};
    reg [153:0] geometry;
    always @(posedge pixel_clk)geometry<=raw_geometry;
    wire [3:0] scale_num,scale_den;
    wire [11:0] view_x,view_y,view_w,view_h;
    assign {scale_num,scale_den,view_x,view_y,view_w,view_h}=geometry[55:0];
    multitini_video_capture #(.DYNAMIC_DISPLAY(1),.SHARED_PIXEL_RESET(1)) capture_i(
        .display_view_x(view_x),.display_view_y(view_y),.display_view_width(view_w),
        .display_view_height(view_h),.display_scale_num(scale_num),.display_scale_den(scale_den),
        .clk(control_clk),.resetn(resetn),.enable(1'b0),.consumer(32'd0),
        .pixel_clk(pixel_clk),.pixel_resetn(video_resetn),.shared_pixel_resetn(video_control_resetn),
        .vblank_start(1'b0),.machine_ready(1'b1),.scan_x(12'd0),.scan_y(12'd0),
        .rgb565(16'd0),.awready(1'b0),.wready(1'b0),.bresp(2'd0),.bvalid(1'b0));

    function automatic realtime mode_half(input integer mode_id);
        case(mode_id)
            1:mode_half=7.692;2:mode_half=7.407;3:mode_half=4.630;
            4:mode_half=5.848;5:mode_half=4.202;default:mode_half=3.367;
        endcase
    endfunction
    function automatic [153:0] expected_geometry(input logic [3:0] id);
        video_mode_t timing;
        reg [3:0] scale;
        reg [11:0] width,height,left,top;
        begin
            timing=video_mode(id);
            scale=id==0 ? 4 : (id==1 || id==2 || id==4) ? 2 : 3;
            width=382*scale;height=270*scale;
            left=(timing.width-width)>>1;top=(timing.height-height)>>1;
            expected_geometry={timing.width,timing.h_front,timing.h_sync,timing.h_total,
                timing.height,timing.v_front,timing.v_sync,timing.v_total,
                timing.h_positive,timing.v_positive,scale,4'd1,left,top,width,height};
        end
    endfunction

    // Minimal clock-wizard AXI slave: independently stalled AW/W, exactly
    // 24 ordered writes, followed by an unlocked interval and relock.
    integer cycle=0,writes=0,register_index=0,lock_timer=0;
    reg aw_seen=0,w_seen=0;
    reg [10:0] saved_addr;
    reg [31:0] saved_data;
    always @(negedge control_clk)begin
        cycle=cycle+1;awready=cycle%3==0;wready=cycle%3==1;
    end
    always @(posedge control_clk)begin
        if(!clock_resetn)begin
            aw_seen<=0;w_seen<=0;bvalid<=0;locked<=0;lock_timer<=8;register_index<=0;
        end else begin
            if(awvalid && awready)begin aw_seen<=1;saved_addr<=awaddr;end
            if(wvalid && wready)begin w_seen<=1;saved_data<=wdata;end
            if(bvalid && bready)bvalid<=0;
            if(aw_seen && w_seen && !bvalid)begin
                if(saved_addr!=11'h200+register_index*4)$fatal(1,"clock register sequence");
                if(saved_addr==11'h25c)begin
                    if(saved_data!=3)$fatal(1,"clock LOAD value");
                    if(video_resetn)$fatal(1,"clock changed before pixel reset");
                    locked<=0;lock_timer<=16;pixel_half=mode_half(requested_mode);
                end
                register_index<=register_index==23 ? 0 : register_index+1;
                writes=writes+1;bvalid<=1;aw_seen<=0;w_seen<=0;
            end
            if(lock_timer!=0)begin
                lock_timer<=lock_timer-1;
                if(lock_timer==1)locked<=1;
            end
        end
    end

    realtime last_pixel_edge=0,last_control_edge=0;
    integer release_edges=0,active_checks=0,mailbox_accepts=0;
    always @(posedge control_clk)last_control_edge=$realtime;
    always @(hold_video)if(resetn && $realtime!=last_control_edge)
        $fatal(1,"hold request changed off a control-clock edge");
    always @(video_resetn)if(resetn && locked && $realtime!=last_pixel_edge)
        $fatal(1,"hold caused an asynchronous pixel-reset transition");
    always @(posedge pixel_clk)begin
        last_pixel_edge=$realtime;
        if(!video_control_resetn || !video_resetn)release_edges=0;
        else if(release_edges<3)release_edges=release_edges+1;
        if(mode_valid)begin
            if(video_resetn)$fatal(1,"mode mailbox accepted while scanout active");
            mailbox_accepts=mailbox_accepts+1;
        end
        #0.001;
        if(resetn && capture_i.pixel_run !== (release_edges==3))
            $fatal(1,"capture three-cycle release changed");
        if(video_resetn)begin
            if(geometry !== expected_geometry(active_mode))
                $fatal(1,"unsettled geometry visible at active pixel mode %0d got=%h expected=%h",
                    active_mode,geometry,expected_geometry(active_mode));
            active_checks=active_checks+1;
        end
        if(capture_i.pixel_run &&
           {capture_i.view_x,capture_i.view_y,capture_i.scale_num,capture_i.scale_den} !==
           {view_x,view_y,scale_num,scale_den})
            $fatal(1,"capture started with stale geometry");
    end

    integer settled_edges,total_settled_edges=0;
    initial begin
        repeat(8)@(negedge control_clk);resetn=1;
        wait(geometry_ready && video_resetn);
        repeat(8)@(negedge pixel_clk);
        for(integer m=0;m<6;m=m+1)begin
            @(negedge control_clk);quiescent=0;requested_mode=4'(m);request=1;
            @(negedge control_clk);request=0;
            repeat(12)begin
                @(negedge control_clk);
                if(!busy || !pause_reader || hold_video || awvalid || !video_resetn)
                    $fatal(1,"mode %0d failed drain-before-hold contract",m);
            end
            quiescent=1;
            wait(commit);
            if(video_resetn || active_mode!=m || !locked)$fatal(1,"unsafe mode commit");
            if(m==3)begin
                @(negedge pixel_clk);pixel_clock_enabled=0;
                repeat(50)@(negedge control_clk);
                if(!hold_video || video_resetn || !geometry_pending)
                    $fatal(1,"scanout released with stalled mode mailbox");
                pixel_clock_enabled=1;
            end
            wait(pixel_mode==m && geometry===expected_geometry(4'(m)));
            if(video_resetn)$fatal(1,"geometry not settled during reset");
            settled_edges=0;
            while(!video_resetn)begin
                @(negedge pixel_clk);
                if(geometry !== expected_geometry(4'(m)))$fatal(1,"geometry changed before release");
                if(!video_resetn)settled_edges=settled_edges+1;
            end
            if(settled_edges<2)$fatal(1,"insufficient registered-geometry settling");
            total_settled_edges=total_settled_edges+settled_edges;
            repeat(64)begin
                @(negedge pixel_clk);
                if(!video_resetn || geometry !== expected_geometry(4'(m)))
                    $fatal(1,"mode %0d unstable first active pixels",m);
            end
            @(negedge control_clk);frame_latched=1;
            @(negedge control_clk);frame_latched=0;
            wait(!busy);
            if(error || writes!=(m+1)*24)$fatal(1,"mode transaction failed");
        end
        if(mailbox_accepts!=6 || active_checks<384)$fatal(1,"missing mode-switch coverage");
        $display("PASS display mode settling: 6 modes, %0d reset-held geometry edges, %0d active checks, mailbox stall, 3-cycle capture release",total_settled_edges,active_checks);
        $finish;
    end
    initial begin #200000;$fatal(1,"display mode settling timeout");end
endmodule
