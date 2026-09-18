// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps
module tb_video_frame_reader;
    parameter integer SCALE_NUM=4, SCALE_DEN=1;
    parameter bit SHARED_PIXEL_RESET=0;
    localparam integer WIDTH=68, HEIGHT=9, VIEW_X=12, VIEW_Y=2;
    localparam integer OUT_WIDTH=WIDTH*SCALE_NUM/SCALE_DEN;
    localparam integer OUT_HEIGHT=HEIGHT*SCALE_NUM/SCALE_DEN;
    localparam integer STRIDE=((WIDTH+63)/64)*128;
    reg clk=0,pixel_clk=0;
    always #5 clk=!clk;
    always #3.5 pixel_clk=!pixel_clk;
    reg resetn=0,pixel_resetn=0,enable=0,submit=0;
    (* ASYNC_REG="TRUE" *) reg [2:0] shared_release;
    always @(posedge pixel_clk or negedge resetn)
        if(!resetn)shared_release<=0;
        else shared_release<={shared_release[1:0],1'b1};
    wire shared_pixel_resetn=shared_release[2];
    // Model the wrapper's capture-owned liveness crossing when exercising
    // the production shared reset path, including pixel-only reset recovery.
    reg [2:0] shared_pixel_release;
    reg [1:0] shared_running_sync;
    always @(posedge pixel_clk)
        if(!shared_pixel_resetn || !pixel_resetn)shared_pixel_release<=0;
        else shared_pixel_release<={shared_pixel_release[1:0],1'b1};
    always @(posedge clk)
        if(!resetn)shared_running_sync<=0;
        else shared_running_sync<={shared_running_sync[0],shared_pixel_release[2]};
    wire shared_pixel_running=shared_running_sync[1];
    reg [31:0] submit_value=0;
    wire submit_ready,active_valid,busy,quiescent,pixel_use;
    wire [31:0] active_value,error_count,underrun_count;
    reg [11:0] scan_x=0,scan_y=100;
    reg vblank_start=0;
    wire [15:0] pixel_rgb565;
    wire [31:0] araddr;
    wire [3:0] arlen;
    wire [2:0] arsize;
    wire [1:0] arburst;
    wire arvalid,rready;
    reg stall=0,inject_error=0;
    reg transaction=0;
    reg [31:0] read_base;
    integer cycle=0,beat=0,length=0,delay_count=0,reads=0;
    wire arready=!transaction && !stall && cycle%3!=0;
    wire rvalid=transaction && delay_count==0 && !stall && cycle%4!=0;
    wire rlast=beat==length;
    wire [1:0] rresp=inject_error ? 2'b10 : 2'b00;
    function automatic [15:0] memory_pixel(input [31:0] address);
        integer slot,row,x;
        begin
            slot=(address-32'h22400000)>>20;
            row=((address-32'h22400000)&32'h000fffff)/STRIDE;
            x=(((address-32'h22400000)&32'h000fffff)%STRIDE)/2;
            memory_pixel=16'h1000+slot*16'h2000+row*WIDTH+x;
        end
    endfunction
    wire [31:0] address_now=read_base+beat*8;
    wire [63:0] rdata={memory_pixel(address_now+6),memory_pixel(address_now+4),
                       memory_pixel(address_now+2),memory_pixel(address_now)};
    multitini_video_frame_reader #(.WIDTH(WIDTH),.HEIGHT(HEIGHT),
        .VIEW_X(VIEW_X),.VIEW_Y(VIEW_Y),.SCALE_NUM(SCALE_NUM),.SCALE_DEN(SCALE_DEN),
        .SHARED_PIXEL_RESET(SHARED_PIXEL_RESET),.SHARED_PIXEL_LIVENESS(SHARED_PIXEL_RESET)) dut(.*);
    always @(posedge clk) begin
        cycle<=cycle+1;
        if (arvalid && arready) begin
            if (arsize!=3 || arburst!=1 || araddr[2:0]!=0 ||
                araddr[11:0]+8*(arlen+1)>4096) $fatal(1,"invalid AXI burst");
            if (transaction) $fatal(1,"more than one outstanding burst");
            read_base<=araddr;length<=arlen;beat<=0;delay_count<=7;
            transaction<=1;reads<=reads+1;
        end
        if (transaction && delay_count>0) delay_count<=delay_count-1;
        if (rvalid && rready) begin
            if (rlast) transaction<=0;
            else beat<=beat+1;
        end
    end
    task publish(input [31:0] value);
        begin
            wait(submit_ready);
            @(negedge clk);submit_value=value;submit=1;
            @(negedge clk);submit=0;
            repeat(20) @(posedge clk);
            if(active_value==value && active_valid) $fatal(1,"publication before vblank value%h",value);
            wait(dut.publish_valid_pixel);
        end
    endtask
    task blank;
        begin
            @(negedge pixel_clk);scan_x=0;scan_y=100;vblank_start=1;
            @(negedge pixel_clk);vblank_start=0;
            repeat(600) @(posedge pixel_clk);
        end
    endtask
    integer checked=0;
    reg queue_midframe=0;
    task scan_frame(input integer slot,input integer want_processed);
        integer x,y,sx,sy;
        reg [15:0] expected;
        begin
            for(y=0;y<VIEW_Y+OUT_HEIGHT+2;y=y+1) begin
                for(x=0;x<VIEW_X+OUT_WIDTH+30;x=x+1) begin
                    @(negedge pixel_clk);scan_x=x;scan_y=y;#1;
                    if(queue_midframe && x==0 && y==VIEW_Y+OUT_HEIGHT/2) begin
                        fork begin
                            @(negedge clk);submit_value=9;submit=1;
                            @(negedge clk);submit=0;
                        end join_none
                        queue_midframe=0;
                    end
                    if(x>=VIEW_X && x<VIEW_X+OUT_WIDTH &&
                       y>=VIEW_Y && y<VIEW_Y+OUT_HEIGHT) begin
                        if(pixel_use!==want_processed[0])
                            $fatal(1,"pixel use x%0d y%0d wanted%0d got%0d nextrow%0d current%0d valid%0h",
                                x,y,want_processed,pixel_use,dut.next_row,dut.current_row,dut.line_valid);
                        if(want_processed) begin
                            sx=((x-VIEW_X)*SCALE_DEN)/SCALE_NUM;
                            sy=((y-VIEW_Y)*SCALE_DEN)/SCALE_NUM;
                            expected=memory_pixel(32'h22400000+(slot<<20)+sy*STRIDE+sx*2);
                            if(pixel_rgb565!==expected)
                                $fatal(1,"wrong pixel x%0d y%0d got%h expected%h",x,y,pixel_rgb565,expected);
                            checked=checked+1;
                        end
                    end else if(pixel_use) $fatal(1,"processed pixel outside viewport");
                end
            end
        end
    endtask
    integer before_reads;
    reg [31:0] before_reset_epoch;
    initial begin
        repeat(12) @(posedge clk);resetn=1;pixel_resetn=1;
        repeat(12) @(posedge clk);
        if(!quiescent) $fatal(1,"reset did not start quiescent");
        enable=1;
        // Restart scanout and publish a different slot while an old accepted
        // read remains stalled. Its eventual reply must neither validate old
        // slot data nor release the new frame's outstanding request.
        publish(32'h4);
        @(negedge pixel_clk);scan_x=0;scan_y=100;vblank_start=1;
        @(negedge pixel_clk);vblank_start=0;
        wait(transaction);stall=1;
        before_reset_epoch=dut.fill_epoch;
        @(negedge pixel_clk);pixel_resetn=0;
        repeat(20) @(posedge clk);
        if(!active_valid) $fatal(1,"overlapping reset released a stalled old slot");
        pixel_resetn=1;publish(32'h9);
        @(negedge pixel_clk);scan_x=0;scan_y=100;vblank_start=1;
        @(negedge pixel_clk);vblank_start=0;
        repeat(30) @(posedge pixel_clk);
        if(dut.epoch<=before_reset_epoch) $fatal(1,"pixel reset did not advance the outstanding epoch");
        stall=0;
        wait(dut.state==3 && dut.reply_control[44]);
        if(dut.fill_epoch!=before_reset_epoch) $fatal(1,"expected old read to finish first");
        stall=1;
        repeat(80) @(posedge clk);
        if(dut.line_valid!=0) $fatal(1,"old slot reply validated a restarted frame");
        if(!dut.row_outstanding) $fatal(1,"old reply released a new outstanding request");
        stall=0;repeat(600) @(posedge pixel_clk);
        wait(active_valid && active_value==9);
        scan_frame(1,1);
        publish(32'h4);blank;
        wait(active_valid && active_value==4);
        queue_midframe=1;scan_frame(0,1);
        if(error_count || underrun_count) $fatal(1,"normal scanout reported fault");
        if(active_value!=4) $fatal(1,"mid-scan publication changed active frame");
        wait(dut.publish_valid_pixel);blank;
        wait(active_valid && active_value==9);
        scan_frame(1,1);
        // No publication: the same immutable slot must repeat cleanly.
        blank;scan_frame(1,1);
        // An AXI error never validates even the partial row written to RAM.
        inject_error=1;blank;scan_frame(1,0);
        if(error_count==0 || underrun_count==0) $fatal(1,"fault diagnostics missing");
        inject_error=0;blank;scan_frame(1,1);
        // Disabling drains an accepted transaction before releasing the slot.
        @(negedge pixel_clk);scan_x=0;scan_y=100;vblank_start=1;
        @(negedge pixel_clk);vblank_start=0;
        wait(transaction);stall=1;
        @(negedge clk);enable=0;
        repeat(20) @(posedge clk);
        if(quiescent) $fatal(1,"disable ignored accepted outstanding read");
        stall=0;wait(dut.publish_valid_pixel);blank;
        wait(quiescent);
        before_reads=reads;
        scan_frame(1,0);
        if(reads!=before_reads) $fatal(1,"quiescent reader issued later AXI request");
        // Slot three is legal, and an unrelated pixel reset cannot cancel
        // the read channel or release the currently accessed DDR slot early.
        enable=1;publish(32'hf);blank;scan_frame(3,1);
        @(negedge pixel_clk);scan_x=0;scan_y=100;vblank_start=1;
        @(negedge pixel_clk);vblank_start=0;
        wait(transaction);stall=1;
        @(negedge clk);pixel_resetn=0;enable=0;
        repeat(20) @(posedge clk);
        if(quiescent || !active_valid) $fatal(1,"pixel reset cancelled outstanding AXI ownership");
        stall=0;wait(quiescent);
        before_reads=reads;
        repeat(100) @(posedge clk);
        if(reads!=before_reads) $fatal(1,"pixel reset did not stop new reads");
        pixel_resetn=1;enable=1;publish(32'h10);blank;scan_frame(0,1);
        $display("PASS video frame reader: scale %0d/%0d, %0d pixels, publication/error/drain passed",SCALE_NUM,SCALE_DEN,checked);
        $finish;
    end
    initial begin #10000000;$fatal(1,"timeout");end
endmodule
