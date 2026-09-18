// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_video_capture;
    parameter integer SCALE_NUM=1,SCALE_DEN=1;
    parameter bit SHARED_PIXEL_RESET=0;
    localparam integer VIEW_WIDTH=6*SCALE_NUM/SCALE_DEN;
    localparam integer VIEW_HEIGHT=20*SCALE_NUM/SCALE_DEN;
    localparam integer LINE_TOTAL=VIEW_WIDTH+80;
    reg clk=0,pixel_clk=0,resetn=0,pixel_resetn=0,enable=0;
    always #5 clk=~clk;
    always #3.5 pixel_clk=~pixel_clk;
    (* ASYNC_REG="TRUE" *) reg [2:0] shared_release;
    always @(posedge pixel_clk or negedge resetn)
        if(!resetn)shared_release<=0;
        else shared_release<={shared_release[1:0],1'b1};
    wire shared_pixel_resetn=shared_release[2];
    reg [31:0] consumer=0;
    wire [31:0] producer,drops,errors;
    wire busy,quiescent;
    reg [11:0] x=0,y=0;
    wire [15:0] rgb=((x-2)*SCALE_DEN/SCALE_NUM)+((y*SCALE_DEN/SCALE_NUM)<<8);
    wire [31:0] awaddr;
    wire [3:0] awlen;
    wire [2:0] awsize;
    wire [1:0] awburst;
    wire awvalid,wvalid,wlast,bready;
    wire [63:0] wdata;
    wire [7:0] wstrb;
    reg awready=0,wready=0,bvalid=0;
    reg [1:0] bresp=0;
    integer tick=0,beat=0,bdelay=0,address=0,bursts=0,checks=0;
    integer word_index,pixel_index,expected;
    reg transfer=0,stall=0,inject_error=0;
    multitini_video_capture #(.WIDTH(6),.HEIGHT(20),.VIEW_X(2),.VIEW_Y(0),
        .SCALE_NUM(SCALE_NUM),.SCALE_DEN(SCALE_DEN),.SHARED_PIXEL_RESET(SHARED_PIXEL_RESET)) dut(
        .clk(clk),.resetn(resetn),.enable(enable),.consumer(consumer),
        .producer(producer),.drops(drops),.errors(errors),.busy(busy),.quiescent(quiescent),
        .pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.vblank_start(x==0 && y==VIEW_HEIGHT),
        .shared_pixel_resetn(shared_pixel_resetn),
        .machine_ready(1'b1),.scan_x(x),.scan_y(y),.rgb565(rgb),
        .awaddr(awaddr),.awlen(awlen),.awsize(awsize),.awburst(awburst),.awvalid(awvalid),.awready(awready),
        .wdata(wdata),.wstrb(wstrb),.wlast(wlast),.wvalid(wvalid),.wready(wready),
        .bresp(bresp),.bvalid(bvalid),.bready(bready));
    always @(posedge pixel_clk)begin
        if(!pixel_resetn)begin x<=0;y<=0;end
        else if(x==LINE_TOTAL-1)begin x<=0;y<=y==VIEW_HEIGHT+3?0:y+1;end
        else x<=x+1;
    end
    always @(negedge clk)begin
        tick=tick+1;awready=!stall && tick%3!=0;wready=!stall && tick%4!=0;
        if(bdelay>0)begin bdelay=bdelay-1;if(bdelay==0)bvalid=1;end
    end
    always @(posedge clk)if(resetn)begin
        if(awvalid && awready)begin
            if(transfer || bvalid || bdelay!=0)$fatal(1,"More than one capture AXI burst outstanding");
            if(awaddr<32'h22000000 || awaddr>=32'h22400000 || awaddr[6:0]!=0 || awlen!=15 || awsize!=3 || awburst!=1)
                $fatal(1,"Capture AXI address/length out of bounds");
            address=awaddr;beat=0;transfer=1;
        end
        if(wvalid && wready)begin
            if(!transfer || wstrb!=255 || wlast!=(beat==15))$fatal(1,"Malformed capture data burst");
            word_index=((address&20'hfffff)>>3)+beat;
            for(pixel_index=0;pixel_index<4;pixel_index=pixel_index+1)begin
                expected=((word_index*4+pixel_index)%64)<6 ?
                    ((word_index*4+pixel_index)%64)+(((word_index*4+pixel_index)/64)<<8):0;
                if(wdata[pixel_index*16+:16]!==16'(expected))
                    $fatal(1,"Capture pixel mismatch word=%0d lane=%0d got=%h expected=%h",word_index,pixel_index,wdata[pixel_index*16+:16],16'(expected));
                checks=checks+1;
            end
            beat=beat+1;
            if(beat==16)begin transfer=0;bdelay=5;bresp=inject_error?2:0;bursts=bursts+1;end
        end
        if(bvalid && bready)bvalid<=0;
    end
    task wait_quiet;
        integer timeout;
        begin
            timeout=0;
            while(!quiescent && timeout<20000)begin @(negedge clk);timeout=timeout+1;end
            if(!quiescent)$fatal(1,"Capture did not quiesce after disable state=%0d fifo_empty=%0d arm_ready=%0d pixel_run=%0d ended=%0d failed=%0d",dut.state,dut.fifo_empty,dut.arm_ready,dut.pixel_run,dut.ended,dut.failed);
            repeat(50)begin
                @(negedge clk);
                if(awvalid || wvalid || bready || !dut.fifo_empty || !quiescent)
                    $fatal(1,"Capture traffic/FIFO word reappeared after quiescing");
            end
        end
    endtask
    initial begin
        repeat(8)@(negedge clk);resetn=1;pixel_resetn=1;
        repeat(20)@(negedge clk);enable=1;
        wait(producer==4);repeat(3000)@(negedge clk);
        if(producer!=4)$fatal(1,"Full capture ring overwrote unreleased frame");
        if(drops==0)$fatal(1,"Full capture ring drop not reported");
        consumer=4;wait(producer==5);
        enable=0;wait_quiet();
        // Overflow aborts the entire frame. A wedged accepted AW still drains
        // after the memory bus resumes; disable cannot cancel AXI transactions.
        consumer=producer;stall=1;enable=1;
        repeat(60000)@(negedge clk);enable=0;stall=0;wait_quiet();
        if(producer!=5)$fatal(1,"Overflowed capture was published");
        // AXI errors likewise never publish damaged buffers.
        inject_error=1;enable=1;wait(errors!=0);enable=0;wait_quiet();
        if(producer!=5)$fatal(1,"AXI error capture was published");
        inject_error=0;enable=1;wait(producer==6);enable=0;wait_quiet();
        consumer=producer;enable=1;stall=1;
        wait(awvalid);
        @(negedge pixel_clk);pixel_resetn=0;enable=0;
        repeat(30)@(negedge clk);
        if(quiescent || !awvalid)$fatal(1,"Pixel reset cancelled pending AXI address");
        stall=0;repeat(60)@(negedge clk);pixel_resetn=1;
        wait_quiet();
        if(producer!=6)$fatal(1,"Pixel reset frame was published");
        enable=1;wait(producer==7);enable=0;wait_quiet();
        // Assert reset immediately before a packed FIFO word would be written.
        // No final pixel-domain beat may arrive after control observes reset.
        consumer=producer;enable=1;
        wait(dut.sample && dut.pack_index==3);
        @(negedge pixel_clk);pixel_resetn=0;enable=0;
        @(posedge pixel_clk);#1;
        if(dut.pixel_run || dut.fifo_write)$fatal(1,"Reset allowed a trailing packed FIFO write");
        repeat(30)@(negedge clk);wait_quiet();
        pixel_resetn=1;repeat(30)@(negedge clk);
        if(producer!=7)$fatal(1,"Packed-beat reset published a partial frame");
        enable=1;wait(producer==8);enable=0;wait_quiet();
        $display("PASS video capture: %0d pixel checks, bounded bursts/ring, stalls, overflow/error rejection, quiescence",checks);
        $finish;
    end
    initial begin #20000000;$fatal(1,"Capture test timeout producer=%0d state=%0d ended=%0d staged=%0d completed=%0d",producer,dut.state,dut.ended,dut.staged,dut.completed_bursts);end
endmodule
