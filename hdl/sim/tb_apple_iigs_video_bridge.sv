// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_video_bridge;
    reg source_clk=0,control_clk=0,pixel_clk=0,resetn=0;
    reg pixel_resetn=0,pixel_clock_running=1;
    always #5 source_clk=~source_clk;
    always #3 control_clk=~control_clk;
    always #7 if(pixel_clock_running)pixel_clk=~pixel_clk;
    reg source_ready=0,display_pause=0,stall=0,pal=0;
    integer sx=0,sy=494,phase=0;
    reg [11:0] scan_x=0,scan_y=0;
    wire source_valid=phase==0;
    wire [8:0] source_row=pal ? 9'(sy-232) : sy>=494 ? 9'(sy-494) : 9'(sy-232);
    wire [7:0] source_r={5'(sx-2),3'd0},source_g={6'(source_row),2'd0},source_b={5'((sx-2)^source_row),3'd0};
    wire [15:0] machine_rgb565;
    wire machine_active,display_quiescent;
    wire [31:0] capture_drops,capture_errors,read_errors,underruns;
    wire [31:0] araddr,awaddr;
    wire [3:0] arlen,awlen;
    wire [2:0] arsize,awsize;
    wire [1:0] arburst,awburst;
    wire arvalid,arready,awvalid,awready,rready,wlast,wvalid,wready,bready;
    wire [63:0] wdata;
    wire [7:0] wstrb;
    reg [63:0] rdata=0;
    reg rvalid=0,rlast=0,bvalid=0;
    reg [63:0] memory[0:524287];
    reg seen[0:359103];
    integer slot_words[0:3];
    integer ticks=0,write_word=0,write_beat=0,read_word=0,read_left=0,bdelay=0;
    integer checked=0,unique_checked=0,writes=0,reads=0,padding=0,published_frames=0;
    reg writing=0;
    reg [31:0] previous_producer=0;
    wire shared_pixel_running;
    // Use the shell's independent pixel-liveness producer, including startup
    // while the output is held reset and a later stopped-clock transaction.
    multitini_video_capture #(.WIDTH(1),.HEIGHT(1),.SHARED_PIXEL_RESET(1'b1)) shared_liveness(
        .display_view_x(12'd0),.display_view_y(12'd0),.display_view_width(12'd1),.display_view_height(12'd1),
        .display_scale_num(4'd1),.display_scale_den(4'd1),
        .clk(control_clk),.resetn(resetn),.enable(1'b0),.consumer(32'd0),
        .producer(),.drops(),.errors(),.busy(),.quiescent(),.pixel_running(shared_pixel_running),
        .pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.shared_pixel_resetn(resetn),
        .vblank_start(1'b0),.machine_ready(1'b0),.scan_x(12'd0),.scan_y(12'd0),.rgb565(16'd0),
        .awaddr(),.awlen(),.awsize(),.awburst(),.awvalid(),.awready(1'b1),
        .wdata(),.wstrb(),.wlast(),.wvalid(),.wready(1'b1),.bresp(2'd0),.bvalid(1'b0),.bready());
    assign awready=!stall && !writing && !bvalid && bdelay==0 && ticks%5!=0;
    assign wready=!stall && writing && ticks%4!=0;
    assign arready=!stall && read_left==0 && ticks%3!=0;
    reg source_ready_launch=0;
    wire source_ready_control;
    always @(posedge source_clk)begin
        if(!resetn)source_ready_launch<=0;
        else source_ready_launch<=source_ready;
    end
    cdc_bit_sync source_ready_sync(.clk(control_clk),.resetn(resetn),
        .din(source_ready_launch),.dout(source_ready_control));
    multitini_apple_iigs_video_bridge dut(
        .source_clk(source_clk),.source_resetn(resetn),.source_ready(source_ready),
        .source_ready_control(source_ready_control),.source_pal(pal),
        .source_valid(source_valid),.source_x(10'(sx)),.source_y(9'(sy)),
        .source_r(source_r),.source_g(source_g),.source_b(source_b),
        .control_clk(control_clk),.control_resetn(resetn),.pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.shared_pixel_resetn(resetn),
        .shared_pixel_running(shared_pixel_running),
        .diagnostic_request(1'b0),.scan_x(scan_x),.scan_y(scan_y),.vblank_start(scan_x==0 && scan_y==512),
        .display_view_x(12'd32),.display_view_y(12'd8),.display_view_width(12'd724),.display_view_height(12'd496),
        .display_scale_num(4'd1),.display_scale_den(4'd1),.display_pause(display_pause),.display_quiescent(display_quiescent),
        .display_h_active(12'd800),.display_v_active(12'd528),.display_h_total(12'd816),.display_v_total(12'd542),
        .machine_active(machine_active),.machine_rgb565(machine_rgb565),.viewport_x(),.viewport_y(),.viewport_width(),.viewport_height(),
        .capture_drops(capture_drops),.capture_errors(capture_errors),.read_errors(read_errors),.underruns(underruns),
        .araddr(araddr),.arlen(arlen),.arsize(arsize),.arburst(arburst),.arvalid(arvalid),.arready(arready),
        .rdata(rdata),.rresp(2'd0),.rlast(rlast),.rvalid(rvalid),.rready(rready),
        .awaddr(awaddr),.awlen(awlen),.awsize(awsize),.awburst(awburst),.awvalid(awvalid),.awready(awready),
        .wdata(wdata),.wstrb(wstrb),.wlast(wlast),.wvalid(wvalid),.wready(wready),.bresp(2'd0),.bvalid(bvalid),.bready(bready));
    always @(posedge source_clk)if(resetn)begin
        if(phase==3)begin
            phase<=0;
            if(sx==911)begin sx<=0;sy<=sy==511 ? (pal ? 200 : 250) : sy+1;end else sx<=sx+1;
        end else phase<=phase+1;
    end
    always @(posedge pixel_clk)if(resetn && pixel_resetn)begin
        if(scan_x==815)begin scan_x<=0;scan_y<=scan_y==541 ? 0 : scan_y+1'b1;end
        else scan_x<=scan_x+1'b1;
    end else begin scan_x<=0;scan_y<=0;end
    function automatic [15:0] pattern(input integer x,y);
        pattern={5'(x),6'(y/2),5'(x^(y/2))};
    endfunction
    integer lane,index,x,y,slot;
    always @(posedge control_clk)if(resetn)begin
        if(dut.reader_i.pixel_live!==shared_pixel_running)$fatal(1,"Reader did not use shared liveness");
        if(!shared_pixel_running && dut.submit_ready)$fatal(1,"Publication allowed before pixel startup");
        ticks<=ticks+1;
        if(dut.producer!=previous_producer)begin
            slot=2'(dut.producer-1);
            if(dut.producer!=previous_producer+1 || slot_words[slot]!=95232)
                $fatal(1,"Published incomplete/padded frame producer%0d words%0d",dut.producer,slot_words[slot]);
            previous_producer=dut.producer;published_frames=published_frames+1;
        end
        if(awvalid && awready)begin
            if(awaddr<32'h23000000 || awaddr>=32'h23400000 || awaddr[6:0]!=0 || awlen!=15 || awsize!=3 || awburst!=1)
                $fatal(1,"Source capture escaped bounded DDR ring");
            if(dut.active_valid && awaddr[21:20]==dut.active_value[1:0])$fatal(1,"Capture overwrote owned display slot");
            if(awaddr[19:0]>=20'hba000)$fatal(1,"Source capture exceeded 768x496 stride bounds");
            if(awaddr[19:0]==0)slot_words[awaddr[21:20]]=0;
            write_word=(awaddr-32'h23000000)/8;write_beat=0;writing=1;
        end
        if(wvalid && wready)begin
            if(wstrb!=255 || wlast!=(write_beat==15))$fatal(1,"Malformed source write");
            for(lane=0;lane<4;lane=lane+1)begin
                index=((write_word+write_beat)%131072)*4+lane;x=index%768;y=index/768;
                if(y>=496 || wdata[lane*16+:16] !== (x<724 ? pattern(x,y) : 16'd0))
                    $fatal(1,"Source DDR pixel/padding mismatch %0d,%0d got%h",x,y,wdata[lane*16+:16]);
                if(x>=724)padding=padding+1;
            end
            slot_words[write_word/131072]=slot_words[write_word/131072]+1;
            memory[write_word+write_beat]<=wdata;write_beat=write_beat+1;
            if(write_beat==16)begin writing=0;bdelay=5;writes=writes+1;end
        end
        if(bdelay>0 && !stall)begin bdelay=bdelay-1;if(bdelay==0)bvalid<=1;end
        if(bvalid && bready)bvalid<=0;
        if(arvalid && arready)begin
            if(araddr<32'h23000000 || araddr>=32'h23400000 || araddr[6:0]!=0 || arsize!=3 || arburst!=1 ||
               araddr[19:0]>=20'hba000 || ((araddr[19:0]%1536)/8)+(arlen+1)>192)
                $fatal(1,"Source reader escaped bounded padded row");
            read_word=(araddr-32'h23000000)/8;read_left=arlen+1;reads=reads+1;
        end else if(rvalid && rready)begin read_word=read_word+1;read_left=read_left-1;end
    end
    always @(negedge control_clk)begin
        rvalid=read_left!=0 && !stall && ticks%5!=0;
        rlast=read_left==1;rdata=memory[read_word];
    end
    always @(negedge pixel_clk)if(machine_active && dut.pixel_use && !display_pause)begin
        if(scan_x<32 || scan_x>=756 || scan_y<8 || scan_y>=504 ||
           machine_rgb565!==pattern(scan_x-32,scan_y-8))
            $fatal(1,"Display pixel mismatch %0d,%0d got%h",scan_x,scan_y,machine_rgb565);
        index=(scan_y-8)*724+scan_x-32;
        if(!seen[index])begin seen[index]=1;unique_checked=unique_checked+1;end
        checked=checked+1;
    end
    task quiet;
        integer timeout;
        begin
            timeout=0;
            // Reader ownership is relinquished at the next output vblank;
            // this 816x542 model has a 6.2ms field, longer than Genesis's.
            while(!display_quiescent && timeout<2000000)begin @(negedge control_clk);timeout=timeout+1;end
            if(!display_quiescent)$fatal(1,"IIgs bridge failed to drain capture%0d reader%0d active%0d pending%0d",dut.capture_i.state,dut.reader_i.state,dut.active_valid,dut.reader_i.pending);
            repeat(100)begin @(negedge control_clk);if(awvalid || arvalid || wvalid || !display_quiescent)$fatal(1,"Traffic resumed after quiescence");end
        end
    endtask
    reg [31:0] published;
    initial begin
        if($test$plusargs("PAL"))begin pal=1;sy=232;end
        for(integer i=0;i<359104;i=i+1)seen[i]=0;
        for(integer i=0;i<4;i=i+1)slot_words[i]=0;
        repeat(10)@(negedge control_clk);resetn=1;repeat(100)@(negedge control_clk);source_ready=1;
        wait(dut.producer>=1);
        if(machine_active || reads || shared_pixel_running || dut.active_valid)
            $fatal(1,"Reader started while output was reset");
        @(negedge pixel_clk);pixel_resetn=1;
        wait(dut.producer>=2 && unique_checked==359104);
        if(capture_errors || read_errors || underruns)$fatal(1,"Errors during healthy source scanout");
        $display("PASS IIgs DDR complete frame PAL%0d: all359104 image pixels, every44-pixel row padding, full496rows, bounded four-slot storage and scanout",pal);
        wait(wvalid);@(negedge control_clk);stall=1;display_pause=1;
        repeat(100)@(negedge control_clk);
        if(display_quiescent)$fatal(1,"Quiescent while accepted AXI transaction was stalled");
        stall=0;quiet();
        @(negedge pixel_clk);pixel_resetn=0;
        repeat(32)@(negedge control_clk);
        if(shared_pixel_running)$fatal(1,"Pixel liveness survived pixel reset");
        @(negedge pixel_clk);pixel_clock_running=0;
        repeat(100)begin @(negedge control_clk);if(!display_quiescent || awvalid || arvalid || wvalid)$fatal(1,"Drain lost with stopped pixel clock");end
        pixel_clock_running=1;@(negedge pixel_clk);pixel_resetn=1;
        wait(shared_pixel_running);
        if(!display_quiescent || dut.active_valid)$fatal(1,"Paused startup claimed a frame");
        published=dut.producer;display_pause=0;wait(dut.producer>published);
        $display("PASS IIgs DDR pause: stalled transaction drain, stopped pixel clock and resumed publication PAL%0d",pal);
        wait(awvalid && awaddr[19:0]==0);wait(wvalid);published=dut.producer;source_ready=0;quiet();
        if(dut.active_valid || dut.producer!=published)$fatal(1,"Source reset retained/published incomplete frame");
        source_ready=1;wait(dut.producer>published);display_pause=1;quiet();
        if(read_errors!=0)$fatal(1,"Reader errors after recovery");
        $display("PASS IIgs source DDR PAL%0d: %0d unique/%0d displayed pixels, %0d zero padding samples, %0d complete publications, %0d writes/%0d reads; four-enable cadence, AXI stalls, reset and pause drain",pal,unique_checked,checked,padding,published_frames,writes,reads);
        $finish;
    end
    initial begin #120000000;$fatal(1,"IIgs DDR timeout producer%0d errors%0d checked%0d unique%0d capture%0d reader%0d",dut.producer,capture_errors,checked,unique_checked,dut.capture_i.state,dut.reader_i.state);end
endmodule
