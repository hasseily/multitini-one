// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// The runner builds amiga_top_profile_excerpt from owning production sources.
// Only clocks, PS reset, host commands and independent numeric checks live here.
module amiga_profile_settle_case #(parameter integer INDEX=0)(output reg done=0);
    reg clk=0,control_clk=0,video_clk=0;
    always #17.621145 clk=~clk; // 28.375MHz service/native-profile source
    initial begin #(0.001+1.7*(INDEX%4));forever #3.759398 control_clk=~control_clk;end
    initial begin #(0.003+9.3*(INDEX/4));forever #20 video_clk=~video_clk;end // 25MHz
    reg reset=1,control_resetn=0,video_control_resetn=0,write=0,owners_quiet=0;
    reg [31:0] command=0;
    wire [31:0] status;
    wire [1:0] capture_mode;
    wire source_hold;
    localparam integer MODE=INDEX%6,SCALE=1+(INDEX%4);
    multitini_amiga_host host_i(
        .control_clk(control_clk),.control_resetn(control_resetn),.write(write),.command(command),.status(status),
        .clk(clk),.reset(reset),.chip_tick(!reset),.io_fpga(),.io_uio(),.io_strobe(),.io_din(),
        .io_wait(1'b0),.io_dout(16'd0),.ide_req(6'd0),.ide_address(),.ide_read(),.ide_write(),
        .ide_wdata(),.ide_rdata(16'd0),.native_rtc(),.native_display_request(),
        .display_read_address(),.display_read_data(16'd0),.capture_mode(capture_mode),
        .display_switch_pending(source_hold),.display_quiescent(top_i.amiga_quiet_source));
    amiga_top_profile_excerpt top_i(
        .c64_clk(clk),.control_clk(control_clk),.video_clk(video_clk),
        .c64_aresetn(!reset),.control_aresetn(control_resetn),.video_control_resetn(video_control_resetn),
        .amiga_capture_mode(capture_mode),.amiga_switch_source(source_hold),
        .genesis_source_quiescent(owners_quiet),.framebuffer_display_quiescent(owners_quiet),
        .display_active_mode(4'(MODE)),.display_pixel_mode(4'(MODE)),.display_pixel_multiplier(3'(SCALE)));

    integer source_edges=0,commit_edges=0,expected_profile=0,expected_mode=0;
    integer queued_control=0,queued_pixel=0,queued_both=0,releases=0;
    reg monitor_release=0;
    always @(posedge clk)begin
        source_edges=source_edges+1;
        if(!reset && host_i.state==8)commit_edges=commit_edges+1;
    end
    always @(capture_mode)if(monitor_release && !reset)begin
        if(top_i.amiga_profile_to_control.source_busy &&
           top_i.amiga_profile_to_control.payload_hold!=expected_profile)queued_control=queued_control+1;
        if(top_i.amiga_profile_to_pixel.source_busy &&
           top_i.amiga_profile_to_pixel.payload_hold!=expected_profile)queued_pixel=queued_pixel+1;
        if(top_i.amiga_profile_to_control.source_busy && top_i.amiga_profile_to_pixel.source_busy &&
           top_i.amiga_profile_to_control.payload_hold!=expected_profile &&
           top_i.amiga_profile_to_pixel.payload_hold!=expected_profile)queued_both=queued_both+1;
    end
    always @(negedge control_resetn or negedge video_control_resetn)
        if(monitor_release)$fatal(1,"Profile hold reset a PS/mailbox domain");

    task tick;begin @(posedge clk);#0.1;end endtask
    task ps_reset;
        begin
            monitor_release=0;reset=1;control_resetn=0;video_control_resetn=0;owners_quiet=0;write=0;
            repeat(24)tick;
            @(negedge clk);reset=0;control_resetn=1;video_control_resetn=1;
            repeat(160)tick;
        end
    endtask
    task send(input integer target);
        begin
            @(negedge control_clk);command=32'h0a000000|target;write=1;
            @(negedge control_clk);write=0;
        end
    endtask
    task check_geometry(input integer profile);
        integer aw,ah,w,h,full_w,full_h,left,top,x0,y0,x1,y1,cx,cy;
        integer top_x,top_y,top_w,top_h,cap_n,cap_w,cap_h,cap_x,cap_y;
        begin
            case(MODE)
                0:begin aw=1920;ah=1080;end
                1:begin aw=1024;ah=768;end
                2:begin aw=1200;ah=800;end
                3:begin aw=1280;ah=1024;end
                4:begin aw=1360;ah=768;end
                5:begin aw=1680;ah=1050;end
            endcase
            w=profile==2 ? 1280:profile==1 ? 724:640;h=profile==1 ? 566:512;
            full_w=w*SCALE;full_h=h*SCALE;left=(aw-full_w)>>>1;top=(ah-full_h)>>>1;
            x0=left<0 ? 0:left;y0=top<0 ? 0:top;
            x1=left+full_w>aw ? aw:left+full_w;y1=top+full_h>ah ? ah:top+full_h;
            cx=left<0 ? -left:0;cy=top<0 ? -top:0;
            top_x=profile==2 ? x0:left;top_y=profile==2 ? y0:top;
            top_w=profile==2 && full_w>aw ? aw:full_w;
            top_h=profile==2 && full_h>ah ? ah:full_h;
            if(top_i.amiga_profile_source!==2'(profile) || top_i.amiga_profile_control!==2'(profile) ||
               top_i.amiga_profile_pixel!==2'(profile) ||
               {top_i.display_h_active,top_i.display_v_active}!={12'(aw),12'(ah)} ||
               {top_i.display_view_x,top_i.display_view_y,top_i.display_view_width,top_i.display_view_height}!==
                   {12'(top_x),12'(top_y),12'(top_w),12'(top_h)} ||
               {top_i.visible_x0,top_i.visible_y0,top_i.visible_x1,top_i.visible_y1}!==
                   {12'(x0),12'(y0),12'(x1),12'(y1)} ||
               {top_i.source_x0,top_i.source_y0,top_i.phase_x0,top_i.phase_y0,top_i.scale_num,top_i.scale_den}!==
                   {10'(cx/SCALE),10'(cy/SCALE),4'(cx%SCALE),4'(cy%SCALE),4'(SCALE),4'd1})
                $fatal(1,"Profile hold released before coherent geometry case%0d profile%0d source/control/pixel=%0d/%0d/%0d reader=%0d,%0d..%0d,%0d seeds%0d,%0d",
                    INDEX,profile,top_i.amiga_profile_source,top_i.amiga_profile_control,top_i.amiga_profile_pixel,
                    top_i.visible_x0,top_i.visible_y0,top_i.visible_x1,top_i.visible_y1,top_i.source_x0,top_i.source_y0);
            cap_n=(MODE==0 || MODE==3 || MODE==5) && 2*w<=aw && 2*h<=ah ? 2:1;
            cap_w=w*cap_n;cap_h=h*cap_n;cap_x=(aw-cap_w)>>>1;cap_y=(ah-cap_h)>>>1;
            if(profile==2)begin
                // Auto recapture uses blank raster clocks for all1280native
                // pixels; the final reader above still clips toactualHDMI.
                cap_x=((aw<1280 ? 1280:aw)-cap_w)>>>1;
                if(cap_y<0)begin cap_y=0;cap_h=ah;end
            end
            if({top_i.capture_display_view_x,top_i.capture_display_view_y,
                top_i.capture_display_view_width,top_i.capture_display_view_height,
                top_i.capture_display_scale_num,top_i.capture_display_scale_den}!==
               {12'(cap_x),12'(cap_y),12'(cap_w),12'(cap_h),4'(cap_n),4'd1})
                $fatal(1,"Profile hold released before capture geometry case%0d",INDEX);
            if(top_i.bridge_h_active!=(profile==2 && aw<1280 ? 1280:aw) ||
               cap_x+cap_w>top_i.bridge_h_active || cap_y+cap_h>ah)
                $fatal(1,"Profile hold released before virtual bridge width case%0d captureend%0d active%0d",INDEX,cap_x+cap_w,top_i.bridge_h_active);
        end
    endtask
    always @(negedge source_hold)if(monitor_release && !reset)begin
        #0.1;
        check_geometry(expected_profile);
        if(commit_edges!=64)$fatal(1,"Production DISPLAY_COMMIT did not hold64sourcecycles case%0d got%0d",INDEX,commit_edges);
        if(!control_resetn || !video_control_resetn || top_i.pixel_run)
            $fatal(1,"Pixel snapshot hold/PS reset contract changed");
        releases=releases+1;
    end
    task transition(input integer target);
        integer age;
        begin
            owners_quiet=0;expected_mode=target;expected_profile=target==3 ? 2:target==2 ? 1:0;
            commit_edges=0;monitor_release=1;send(target);
            age=0;while(!source_hold && age<100)begin tick;age=age+1;end
            if(!source_hold)$fatal(1,"Host never published profile hold");
            // Both continuous mailboxes must have old traffic in flight.
            age=0;while(!(top_i.amiga_profile_to_control.source_busy && top_i.amiga_profile_to_pixel.source_busy) && age<100)begin tick;age=age+1;end
            if(age==100)$fatal(1,"Old profile traffic was not queued");
            repeat(INDEX%7)tick;owners_quiet=1;
            age=0;while(source_hold && age<300)begin tick;age=age+1;end
            if(source_hold)$fatal(1,"Profile hold did not release");
            age=0;while(status[31] && age<100)begin tick;age=age+1;end
            if(status[31:30]!=0 || status[15:0]!=target)$fatal(1,"Host profile completion did not echo committed mode");
            repeat(80)tick;monitor_release=0;
        end
    endtask
    initial begin
        ps_reset;check_geometry(0);
        transition(2);transition(3);transition(1);transition(3);transition(0);
        // Reset with a new profile propagating. Drain-on-reset must discard
        // its stale held payloads without changing any production mailbox.
        owners_quiet=1;send(3);wait(capture_mode==3);repeat(2)tick;ps_reset;
        check_geometry(0);
        if(status[31:30]!=0 || capture_mode!=0 || source_hold)$fatal(1,"PS reset replayed an old host profile");
        if(releases!=5 || queued_control==0 || queued_pixel==0)
            $fatal(1,"Missing coherent profile release/queued-old coverage case%0d releases%0d queued%0d/%0d",INDEX,releases,queued_control,queued_pixel);
        $display("PASS top profile settle case%0d HDMI%0d scale%0d releases%0d oldcontrol%0d oldpixel%0d oldboth%0d",
            INDEX,MODE,SCALE,releases,queued_control,queued_pixel,queued_both);done=1;
    end
    initial begin #200000;$fatal(1,"Top profile settle watchdog case%0d",INDEX);end
endmodule

module tb_amiga_profile_cdc_settle;
    wire [15:0] done;
    for(genvar p=0;p<16;p=p+1)begin:phases
        amiga_profile_settle_case #(.INDEX(p)) check_case(.done(done[p]));
    end
    initial begin
        wait(&done);$display("PASS production top profile CDC/settle:16phasepairs,80commits,64sourceedges,25MHzpixels,PSresetdrain");$finish;
    end
endmodule
