// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Firmware-formatted register writes reach the real pixel pipeline. Acceptance
// is checked through register reads and quantized DVI pixels, not bank internals.
module tb_video_effects_control;
    import multitini_regs_pkg::*;
    reg control_clk=0,pixel_clk=0,resetn=0,hold_request=0;
    always #3.75 control_clk=~control_clk;
    always #3.367 pixel_clk=~pixel_clk;
    reg [2:0] shared_release=0;
    always @(posedge pixel_clk or negedge resetn)
        if(!resetn)shared_release<=0;else shared_release<={shared_release[1:0],1'b1};
    wire pixel_resetn,pixel_running;
    multitini_display_reset display_reset_i(.clk(pixel_clk),.clock_resetn(resetn),
        .hold_request(hold_request),.resetn(pixel_resetn));

    reg wr_en=0,rd_en=0;
    reg [11:0] wr_addr=0,rd_addr=0;
    reg [31:0] wr_data=0;
    wire [31:0] rd_data;
    wire effect_write,effect_commit,effect_busy,effect_valid,effect_rejected;
    wire [9:0] effect_address;
    wire [31:0] effect_data,effect_sequence,effect_commit_sequence;
    multitini_control_regs regs_i(
        .clk(control_clk),.resetn(resetn),.core_id(32'h43363420),.core_variant_index(32'd1),
        .core_ready(1'b1),.load_submit_ready(1'b1),.core_load_error(1'b0),
        .vectrex_overlay_status(2'd0),.input_submit_ready(1'b1),
        .media_request_valid(1'b0),.media_request_write(1'b0),.media_request_id(32'd0),
        .media_request_lba(32'd0),.media_request_block_count(16'd0),.media_request_generation(32'd0),
        .media_data_valid(1'b0),.media_data(8'd0),.media_data_last(1'b0),
        .media_state_mounted(1'b0),.media_state_dirty(1'b0),.media_state_eject_pending(1'b0),
        .media_submit_ready(1'b1),.ui_active_visible(1'b0),.ui_active_bank(1'b0),
        .ui_active_dense(1'b0),.ui_submit_ready(1'b1),
        .effect_busy(effect_busy),.effect_active_valid(effect_valid),.effect_rejected(effect_rejected),
        .effect_active_sequence(effect_sequence),.effect_write(effect_write),.effect_commit(effect_commit),
        .effect_address(effect_address),.effect_data(effect_data),.effect_commit_sequence(effect_commit_sequence),
        .wr_en(wr_en),.wr_addr(wr_addr),.wr_data(wr_data),.wr_strb(4'hf),
        .rd_en(rd_en),.rd_addr(rd_addr),.rd_data(rd_data));
    wire [11:0] x,y;
    wire vblank;
    wire [15:0] rgb;
    wire de,hs,vs;
    wire ui_opaque=(x>=19 && x<24) || y==8;
    wire border_opaque=x==2 || x==61 || y==10;
    multitini_video_test #(.DYNAMIC_DISPLAY(1),.SIGNED_VIEWPORT(1),.ENABLE_EFFECTS(1),
        .SHARED_PIXEL_RESET(1),.SHARED_PIXEL_LIVENESS(1)) video_i(
        .display_h_active(12'd64),.display_h_front(12'd2),.display_h_sync(12'd2),.display_h_total(12'd70),
        .display_v_active(12'd12),.display_v_front(12'd1),.display_v_sync(12'd1),.display_v_total(12'd15),
        .display_h_positive(1'b1),.display_v_positive(1'b1),
        .display_scale_num(4'd4),.display_scale_den(4'd1),
        .pixel_clk(pixel_clk),.resetn(pixel_resetn),.enable(1'b1),.machine_enable(1'b1),
        .control_clk(control_clk),.control_resetn(resetn),.shared_pixel_resetn(shared_release[2]),
        .shared_pixel_running(pixel_running),.effect_write(effect_write),.effect_address(effect_address),
        .effect_data(effect_data),.effect_commit(effect_commit),.effect_commit_sequence(effect_commit_sequence),
        .effect_busy(effect_busy),.effect_active_valid(effect_valid),.effect_rejected(effect_rejected),
        .effect_active_sequence(effect_sequence),
        .viewport_x(12'd3),.viewport_y(12'd1),.viewport_width(12'd58),.viewport_height(12'd9),
        .machine_rgb565(16'hffff),.machine_neighbor_rgb565(16'hffff),.machine_opposite_rgb565(16'hffff),
        .machine_dot_valid(1'b0),.machine_neighbor_weight(8'd0),
        .ui_opaque(ui_opaque),.ui_rgb565(16'h5b6a),.border_opaque(border_opaque),.border_rgb565(16'he234),
        .scan_x(x),.scan_y(y),.vblank_start(vblank),.dvi_red(rgb[15:11]),.dvi_grn(rgb[10:5]),
        .dvi_blu(rgb[4:0]),.dvi_de(de),.dvi_hsync(hs),.dvi_vsync(vs));
    // Use the production capture's shared reset/release and control liveness.
    // Capture is disabled, as it may be while CPU1 initializes effect tables.
    multitini_video_capture #(.DYNAMIC_DISPLAY(1),.WIDTH(16),.HEIGHT(3),.SHARED_PIXEL_RESET(1)) capture_i(
        .display_view_x(12'd0),.display_view_y(12'd0),.display_view_width(12'd64),.display_view_height(12'd12),
        .display_scale_num(4'd4),.display_scale_den(4'd1),.clk(control_clk),.resetn(resetn),.enable(1'b0),
        .consumer(32'd0),.pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.shared_pixel_resetn(shared_release[2]),
        .pixel_running(pixel_running),.vblank_start(vblank),.machine_ready(1'b1),.scan_x(x),.scan_y(y),
        .rgb565(16'hffff),.awready(1'b0),.wready(1'b0),.bresp(2'd0),.bvalid(1'b0));

    reg [41:0] table_words[0:1075];
    reg [15:0] expected_rows[0:35];
    integer write_pulses=0,commit_pulses=0,uploads=0,scenes=0;
    always @(posedge control_clk)if(resetn)begin
        if(effect_write)write_pulses=write_pulses+1;
        if(effect_commit)commit_pulses=commit_pulses+1;
    end
    // Adjacent writes have no idle control-clock cycle between them, so the
    // address/data staging is exercised at its fastest accepted beat rate.
    task bus_write(input [11:0] a,input [31:0] d);
        @(negedge control_clk);wr_addr=a;wr_data=d;wr_en=1;
        @(posedge control_clk);#0.001;wr_en=0;
    endtask
    task bus_read(input [11:0] a,output [31:0] d);
        @(negedge control_clk);rd_addr=a;rd_en=1;#0.001;d=rd_data;
        @(posedge control_clk);#0.001;rd_en=0;
    endtask
    task wait_writable;
        reg [31:0] status;
        integer tries;
        begin
            status=0;tries=0;
            while(tries<2000)begin
                bus_read(MT_REG_VIDEO_EFFECT_STATUS,status);tries=tries+1;
                if(status[3:0]==4'h8 || status[3:0]==4'ha)break;
            end
            if(tries==2000 || status[2] || !status[3])$fatal(1,"effect bank not writable status=%h",status);
        end
    endtask
    task upload(input integer table_id);
        integer i,before_writes;
        reg [41:0] word;
        begin
            wait_writable();before_writes=write_pulses;
            for(i=0;i<269;i=i+1)begin
                word=table_words[table_id*269+i];
                if(word[41:32] != (i<13 ? i:256+i-13))$fatal(1,"C table enumeration changed");
                bus_write(MT_REG_VIDEO_EFFECT_ADDRESS,{22'd0,word[41:32]});
                bus_write(MT_REG_VIDEO_EFFECT_DATA,word[31:0]);
            end
            @(posedge control_clk);#0.001;
            if(write_pulses-before_writes!=269)
                $fatal(1,"register path lost/duplicated a table write: got %0d",write_pulses-before_writes);
            uploads=uploads+1;
        end
    endtask
    task acknowledge(input [31:0] seq);
        reg [31:0] status,reported;
        integer tries;
        begin
            status=0;reported=0;tries=0;
            while(tries<2000)begin
                bus_read(MT_REG_VIDEO_EFFECT_STATUS,status);
                bus_read(MT_REG_VIDEO_EFFECT_SEQUENCE,reported);tries=tries+1;
                if(status==10 && reported==seq)break;
                if(status[2])$fatal(1,"firmware table rejected");
            end
            if(tries==2000)$fatal(1,"strict sequence acknowledgement failed status=%h got=%h expected=%h",status,reported,seq);
        end
    endtask

    reg checking=0;
    integer table_id=0,checks=0,scene_checks=0,scene_dark=0,scene_white=0;
    reg [18:0] expected[0:9];
    integer count=0,q;
    function automatic [15:0] expected_pixel(input integer px,py);
        if(px>=64 || py>=12)expected_pixel=0;
        else if((px>=19 && px<24) || py==8)expected_pixel=16'h5b6a;
        else if(px==2 || px==61 || py==10)expected_pixel=16'he234;
        else if(px>=3 && px<61 && py>=1 && py<10)expected_pixel=expected_rows[table_id*9+py-1];
        else expected_pixel=16'hffff;
    endfunction
    always @(posedge pixel_clk)begin
        if(checking)begin
            for(q=9;q>0;q=q-1)expected[q]=expected[q-1];
            expected[0]={y>=13 && y<14,x>=66 && x<68,x<64 && y<12,expected_pixel(x,y)};
            count=count+1;
            #0.001;
            if(count>10)begin
                if({vs,hs,de,rgb}!==expected[9])
                    $fatal(1,"register-to-DVI mismatch table=%0d checks=%0d expected=%h got=%h",table_id,scene_checks,expected[9],{vs,hs,de,rgb});
                scene_checks=scene_checks+1;checks=checks+1;
                if(de && rgb==16'hffff)scene_white=scene_white+1;
                if(de && (rgb==16'hbdf7 || rgb==16'h7bef || rgb==16'h39e7))scene_dark=scene_dark+1;
            end
        end
    end
    task verify_scene(input integer selected,input [31:0] seq);
        begin
            acknowledge(seq);
            wait(x==0 && y==0);@(negedge pixel_clk);
            table_id=selected;count=0;scene_checks=0;scene_dark=0;scene_white=0;checking=1;
            repeat(2100)@(negedge pixel_clk);checking=0;
            if(scene_checks<2089 || scene_white<100 || (selected!=0 && scene_dark<100) ||
               (selected==0 && scene_dark!=0))$fatal(1,"missing visible quantized scanline/master-bypass coverage");
            scenes=scenes+1;
            $display("PASS register effects sequence %0d: table %0d, %0d output pixels, %0d visibly dim pixels",seq,selected,scene_checks,scene_dark);
        end
    endtask
    task pixel_reset;
        reg [31:0] status,reported;
        begin
            @(negedge control_clk);hold_request=1;
            wait(!pixel_running);repeat(12)@(negedge control_clk);
            bus_read(MT_REG_VIDEO_EFFECT_STATUS,status);bus_read(MT_REG_VIDEO_EFFECT_SEQUENCE,reported);
            if(status[1] || reported!=0)$fatal(1,"pixel reset retained a stale active sequence");
            hold_request=0;wait_writable();
        end
    endtask
    initial begin
        $readmemh("effect-control-tables.mem",table_words);
        $readmemh("effect-control-pixels.mem",expected_rows);
        repeat(8)@(negedge control_clk);resetn=1;
        upload(1);bus_write(MT_REG_VIDEO_EFFECT_COMMIT,32'd101);verify_scene(1,101);
        upload(3);bus_write(MT_REG_VIDEO_EFFECT_COMMIT,32'd102);verify_scene(3,102);
        upload(0);bus_write(MT_REG_VIDEO_EFFECT_COMMIT,32'd103);verify_scene(0,103);
        pixel_reset();upload(2);bus_write(MT_REG_VIDEO_EFFECT_COMMIT,32'd104);verify_scene(2,104);
        // Reset a request before vertical blank can apply it. The live mailbox
        // must drain it, invalidate its status, and permit a complete reupload.
        upload(3);wait(x==0 && y==0);bus_write(MT_REG_VIDEO_EFFECT_COMMIT,32'd105);
        pixel_reset();upload(1);bus_write(MT_REG_VIDEO_EFFECT_COMMIT,32'd106);verify_scene(1,106);
        upload(1);bus_write(MT_REG_VIDEO_EFFECT_COMMIT,32'd107);verify_scene(1,107);
        begin
            reg [31:0] errors;
            bus_read(MT_REG_ERROR,errors);
            if(errors!=0 || uploads!=7 || scenes!=6 || write_pulses!=1883 || commit_pulses!=7)
                $fatal(1,"incomplete/error register integration coverage");
        end
        $display("PASS control-register video effects: 7 firmware tables, 1883 staged data writes, 6 strict sequence acknowledgements, %0d quantized DVI pixels, master bypass, reupload, pixel reset, pending-reset drain",checks);
        $finish;
    end
    initial begin #1000000;$fatal(1,"control-register effects timeout");end
endmodule
