// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_genesis_video_bridge;
    reg source_clk=0,control_clk=0,pixel_clk=0,resetn=0;
    reg pixel_resetn=0,pixel_clock_running=1;
    always #5 source_clk=~source_clk;
    always #3 control_clk=~control_clk;
    always #7 if(pixel_clock_running)pixel_clk=~pixel_clk;
    reg source_ready=0,display_pause=0,stall=0,inject_error=0,truncate=0;
    integer sx=0,sy=0,phase=0;
    reg [11:0] scan_x=0,scan_y=0;
    wire source_valid=phase==0;
    wire source_de=sx<320 && sy<240 && (!truncate || sy<200);
    // Deliberately begin vsync between pixel enables. The ingress register
    // must transport field boundaries even while no native pixel is accepted.
    wire source_vsync=(sy==250 && phase>=3) || sy==251;
    wire [7:0] source_r={5'(sx),3'd0},source_g={6'(sy),2'd0},source_b={5'(sx^sy),3'd0};
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
    reg [1:0] bresp=0;
    reg [63:0] memory[0:524287];
    integer ticks=0,write_word=0,write_beat=0,read_word=0,read_left=0,bdelay=0;
    integer checked=0,writes=0,reads=0;
    reg writing=0;
    wire shared_pixel_running;
    // This is the same independent liveness producer used by the common
    // framebuffer. Its disabled capture must not make startup depend on the
    // Genesis reader first publishing a frame or machine_active becoming true.
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
    // Match the shell's one readiness crossing, shared by status and video.
    reg source_ready_launch=0;
    wire source_ready_control;
    always @(posedge source_clk)begin
        if(!resetn)source_ready_launch<=0;
        else source_ready_launch<=source_ready;
    end
    cdc_bit_sync source_ready_sync(.clk(control_clk),.resetn(resetn),
        .din(source_ready_launch),.dout(source_ready_control));
    multitini_genesis_video_bridge dut(
        .source_clk(source_clk),.source_resetn(resetn),.source_ready(source_ready),
        .source_ready_control(source_ready_control),
        .source_valid(source_valid),.source_de(source_de),.source_vsync(source_vsync),.source_mode_240(1'b1),
        .source_r(source_r),.source_g(source_g),.source_b(source_b),
        .control_clk(control_clk),.control_resetn(resetn),.pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.shared_pixel_resetn(resetn),
        .shared_pixel_running(shared_pixel_running),
        .diagnostic_request(1'b0),.scan_x(scan_x),.scan_y(scan_y),.vblank_start(scan_x==0 && scan_y==250),
        .display_view_x(12'd32),.display_view_y(12'd8),.display_view_width(12'd320),.display_view_height(12'd240),
        .display_scale_num(4'd1),.display_scale_den(4'd1),.display_pause(display_pause),.display_quiescent(display_quiescent),
        .machine_active(machine_active),.machine_rgb565(machine_rgb565),.viewport_x(),.viewport_y(),.viewport_width(),.viewport_height(),
        .capture_drops(capture_drops),.capture_errors(capture_errors),.read_errors(read_errors),.underruns(underruns),
        .araddr(araddr),.arlen(arlen),.arsize(arsize),.arburst(arburst),.arvalid(arvalid),.arready(arready),
        .rdata(rdata),.rresp(2'd0),.rlast(rlast),.rvalid(rvalid),.rready(rready),
        .awaddr(awaddr),.awlen(awlen),.awsize(awsize),.awburst(awburst),.awvalid(awvalid),.awready(awready),
        .wdata(wdata),.wstrb(wstrb),.wlast(wlast),.wvalid(wvalid),.wready(wready),.bresp(bresp),.bvalid(bvalid),.bready(bready));
    wire [11:0] reference_x,reference_y;
    wire [15:0] reference_rgb;
    wire reference_start;
    wire [31:0] reference_drops;
    multitini_genesis_video_normalizer unbuffered_reference(
        .clk(source_clk),.resetn(resetn),.ready(source_ready),
        .pixel_ce(source_valid),.de(source_de),.vsync(source_vsync),
        .red(source_r),.green(source_g),.blue(source_b),.mode_240(1'b1),
        .scan_x(reference_x),.scan_y(reference_y),.rgb565(reference_rgb),
        .frame_start(reference_start),.dropped_fields(reference_drops));
    reg reference_live=0;
    reg [11:0] expected_x,expected_y;
    reg [15:0] expected_rgb;
    reg expected_start;
    reg [31:0] expected_drops;
    integer ingress_checks=0;
    // The timing register changes only ingress latency: compare its complete
    // normalized stream against the original sampler one source edge later.
    always @(posedge source_clk)begin
        expected_x=reference_x;expected_y=reference_y;expected_rgb=reference_rgb;
        expected_start=reference_start;expected_drops=reference_drops;
        #1;
        if(resetn && source_ready && reference_live)begin
            if({dut.normalized_x,dut.normalized_y,dut.normalized_start,dut.malformed_fields} !==
               {expected_x,expected_y,expected_start,expected_drops})
                $fatal(1,"Ingress pipeline changed normalized coordinates, field boundary, or rejection count");
            if(expected_x<320 && expected_y<240 && dut.normalized_rgb!==expected_rgb)
                $fatal(1,"Ingress pipeline changed normalized pixel");
            ingress_checks=ingress_checks+1;
        end
        reference_live=resetn && source_ready;
    end
    always @(posedge source_clk)if(resetn)begin
        if(phase==7)begin
            phase<=0;
            if(sx==399)begin sx<=0;sy<=sy==259 ? 0 : sy+1;end else sx<=sx+1;
        end else phase<=phase+1;
    end
    always @(posedge pixel_clk)if(resetn && pixel_resetn)begin
        if(scan_x==399)begin scan_x<=0;scan_y<=scan_y==269 ? 0 : scan_y+1'b1;end
        else scan_x<=scan_x+1'b1;
    end else begin scan_x<=0;scan_y<=0;end
    function automatic [15:0] pattern(input integer x,y);
        pattern={5'(x),6'(y),5'(x^y)};
    endfunction
    integer lane,index,x,y;
    always @(posedge control_clk)if(resetn)begin
        if(dut.reader_i.pixel_live!==shared_pixel_running)$fatal(1,"Reader did not use shared liveness");
        if(!shared_pixel_running && dut.submit_ready)$fatal(1,"Publication allowed before pixel startup");
        ticks<=ticks+1;
        if(awvalid && awready)begin
            if(awaddr<32'h23000000 || awaddr>=32'h23400000 || awaddr[6:0]!=0 || awlen!=15 || awsize!=3 || awburst!=1)
                $fatal(1,"Source capture escaped bounded DDR ring");
            if(dut.active_valid && awaddr[21:20]==dut.active_value[1:0])$fatal(1,"Capture overwrote owned display slot");
            write_word=(awaddr-32'h23000000)/8;write_beat=0;writing=1;
        end
        if(wvalid && wready)begin
            if(wstrb!=255 || wlast!=(write_beat==15))$fatal(1,"Malformed source write");
            for(lane=0;lane<4;lane=lane+1)begin
                index=((write_word+write_beat)%131072)*4+lane;x=index%320;y=index/320;
                if(y>=240 || wdata[lane*16+:16]!==pattern(x,y))
                    $fatal(1,"Source DDR pixel mismatch %0d,%0d got%h",x,y,wdata[lane*16+:16]);
            end
            memory[write_word+write_beat]<=wdata;write_beat=write_beat+1;
            if(write_beat==16)begin writing=0;bdelay=5;bresp<=inject_error ? 2 : 0;writes=writes+1;end
        end
        if(bdelay>0 && !stall)begin bdelay=bdelay-1;if(bdelay==0)bvalid<=1;end
        if(bvalid && bready)bvalid<=0;
        if(arvalid && arready)begin
            if(araddr<32'h23000000 || araddr>=32'h23400000 || araddr[6:0]!=0 || arsize!=3 || arburst!=1)
                $fatal(1,"Source reader escaped bounded DDR ring");
            read_word=(araddr-32'h23000000)/8;read_left=arlen+1;reads=reads+1;
        end else if(rvalid && rready)begin read_word=read_word+1;read_left=read_left-1;end
    end
    always @(negedge control_clk)begin
        rvalid=read_left!=0 && !stall && ticks%5!=0;
        rlast=read_left==1;rdata=memory[read_word];
    end
    always @(negedge pixel_clk)if(machine_active && dut.pixel_use && !display_pause)begin
        if(machine_rgb565!==pattern(scan_x-32,scan_y-8))
            $fatal(1,"Display pixel mismatch %0d,%0d got%h",scan_x,scan_y,machine_rgb565);
        checked=checked+1;
    end
    task quiet;
        integer timeout;
        begin
            timeout=0;
            while(!display_quiescent && timeout<600000)begin @(negedge control_clk);timeout=timeout+1;end
            if(!display_quiescent)$fatal(1,"Source DDR bridge failed to drain capture=%0d reader=%0d active=%0d pending=%0d",dut.capture_i.state,dut.reader_i.state,dut.active_valid,dut.reader_i.pending);
            repeat(100)begin @(negedge control_clk);if(awvalid || arvalid || wvalid || !display_quiescent)$fatal(1,"Traffic resumed after quiescence");end
        end
    endtask
    reg [31:0] published;
    initial begin
        repeat(10)@(negedge control_clk);resetn=1;repeat(100)@(negedge control_clk);source_ready=1;
        // Capture can publish source fields while the output clock is held.
        // The reader must wait for the shared pixel-running return crossing.
        wait(dut.producer>=1);
        $display("Genesis CDC startup source publication at %0t",$time);
        if(machine_active || reads || shared_pixel_running || dut.active_valid)
            $fatal(1,"Reader started while output was reset");
        @(negedge pixel_clk);pixel_resetn=1;
        wait(shared_pixel_running);
        wait(dut.producer>=2 && checked>320*240);
        $display("Genesis CDC healthy scanout at %0t",$time);
        if(capture_errors || read_errors || underruns)$fatal(1,"Errors during healthy source scanout");
        // A live output switch drains source writes and frame reads together.
        wait(wvalid);@(negedge control_clk);stall=1;display_pause=1;
        repeat(100)@(negedge control_clk);
        if(display_quiescent)$fatal(1,"Source quiescent with accepted transaction stalled");
        stall=0;quiet();published=dut.producer;
        $display("Genesis CDC paused drain at %0t",$time);
        // Model the stopped MMCM only after drain. The shared liveness source
        // drops under pixel reset and returns even while capture stays paused.
        @(negedge pixel_clk);pixel_resetn=0;
        repeat(32)@(negedge control_clk);
        if(shared_pixel_running)$fatal(1,"Shared liveness survived pixel reset");
        @(negedge pixel_clk);pixel_clock_running=0;
        repeat(100)begin @(negedge control_clk);if(!display_quiescent || awvalid || arvalid || wvalid)$fatal(1,"Drain lost with pixel clock stopped");end
        pixel_clock_running=1;@(negedge pixel_clk);pixel_resetn=1;
        wait(shared_pixel_running);
        if(!display_quiescent || dut.active_valid)$fatal(1,"Paused startup claimed a frame");
        $display("Genesis CDC pixel restart at %0t",$time);
        inject_error=1;display_pause=0;wait(capture_errors!=0);display_pause=1;quiet();
        $display("Genesis CDC error drain at %0t",$time);
        if(dut.producer!=published)$fatal(1,"Failed AXI frame was published");
        inject_error=0;display_pause=0;wait(dut.producer>published);display_pause=1;quiet();
        $display("Genesis CDC error recovery at %0t",$time);
        // No terminating pixel arrives when a machine is reset mid-field.
        display_pause=0;wait(wvalid);source_ready=0;quiet();
        $display("Genesis CDC source reset drain at %0t",$time);
        published=dut.producer;
        if(dut.active_valid)$fatal(1,"Reset retained a display ownership claim");
        // Truncated source fields must not splice together into a publication.
        truncate=1;source_ready=1;
        repeat(3)begin wait(sy==252);wait(sy==253);end
        display_pause=1;quiet();
        if(dut.producer!=published)$fatal(1,"Truncated fields spliced into a published frame");
        truncate=0;display_pause=0;wait(dut.producer>published);display_pause=1;quiet();
        if(read_errors!=0)$fatal(1,"Reader errors after recovery");
        $display("PASS Genesis source DDR: %0d displayed pixels, %0d writes/%0d reads, %0d ingress comparisons, shared-liveness startup, stopped-clock drain, ring ownership, AXI backpressure, error rejection/recovery",checked,writes,reads,ingress_checks);
        $finish;
    end
    initial begin #150000000;$fatal(1,"Source DDR timeout producer%0d errors%0d checked%0d pause%0d enabled%0d source%0d capture%0d reader%0d pending%0d active%0d shared%0d",dut.producer,capture_errors,checked,display_pause,dut.enabled,source_ready,dut.capture_i.state,dut.reader_i.state,dut.reader_i.pending,dut.active_valid,shared_pixel_running);end
endmodule
