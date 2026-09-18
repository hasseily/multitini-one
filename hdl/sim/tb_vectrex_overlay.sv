// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Bounded memory protocol model. The separate PHY bench tests actual pins.
// This model retains the measured89-clock read acceptance interval and
// streams32words at oneword per2clocks; writes remain busy for82clocks.
module multitini_vectrex_psram_burst (
    input wire clk,resetn,capture_clk,
    input wire request_valid,request_write,input wire [23:0] request_addr,
    input wire [511:0] request_wdata,output wire request_ready,
    output reg response_done=0,read_word_valid=0,output reg [4:0] read_word_index=0,
    output reg [15:0] read_word=0,output wire initialized,output reg fault=0,
    inout wire [3:0] psram_a_io,psram_b_io,output wire psram_ce_n,psram_clk
);
    reg [15:0] memory[0:391679];
    integer cycle=0,address_hold=0,w,read_bursts=0,write_bursts=0;
    reg busy=0,writing=0;
    assign request_ready=resetn&&!busy;assign initialized=resetn;
    assign psram_ce_n=1;assign psram_clk=0;
    always @(posedge clk)begin
        response_done<=0;read_word_valid<=0;
        if(!resetn)begin cycle<=0;busy<=0;fault<=0;end
        else if(request_valid&&request_ready)begin
            if(request_addr[5:0]!=0 || request_addr>=783360)$fatal(1,"Invalid burstaddress %h",request_addr);
            busy<=1;cycle<=1;writing<=request_write;address_hold<=request_addr/2;
            if(request_write)begin
                for(w=0;w<32;w=w+1)memory[request_addr/2+w]<=request_wdata[w*16+:16];
                write_bursts<=write_bursts+1;
            end else read_bursts<=read_bursts+1;
        end else if(busy)begin
            cycle<=cycle+1;
            if(!writing && cycle>=19 && cycle<=81 && cycle[0])begin
                read_word<=memory[address_hold+(cycle-19)/2];
                read_word_index<=(cycle-19)/2;read_word_valid<=1;
            end
            if(cycle==(writing?74:82))response_done<=1;
            if(cycle==89)busy<=0;
        end
    end
endmodule

module tb_vectrex_overlay;
    reg clk=0,pixel_clk=0,control_clk=0,resetn=0,pixel_resetn=0,control_resetn=0;
    reg [31:0] video_config=32'h19;
    reg [11:0] scan_x=0,scan_y=1080;
    wire vblank_start=scan_x==0&&scan_y==1080;
    wire [9:0] source_x,source_y;
    wire [15:0] pixel_rgba;
    wire pixel_valid;
    wire [4:0] frame_config;
    wire control_pixel_running;
    reg [7:0] load_data=0;
    reg load_valid=0,load_first=0,load_last=0,load_epoch=0,load_abort=0,load_abort_epoch=0,transport_abort=0;
    reg [31:0] load_offset=0,load_total=0;
    wire load_ready,loaded,error,psram_ce_n,psram_clk;
    wire [3:0] psram_a_io,psram_b_io;
    wire [18:0] beam_address;
    wire [15:0] rgb;
    integer offset,row,column,x,y,checks=0;
    reg [15:0] expected;
    always #5.208 clk=~clk;
    always #3.367 pixel_clk=~pixel_clk;
    always #3.75 control_clk=~control_clk;
    cdc_bit_sync pixel_running_sync(.clk(control_clk),.resetn(control_resetn),
        .din(pixel_resetn),.dout(control_pixel_running));
    multitini_vectrex_overlay dut(
        .clk(clk),.resetn(resetn),.control_clk(control_clk),.control_resetn(control_resetn),
        .control_pixel_running(control_pixel_running),
        .video_config(video_config),.pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),
        .vblank_start(vblank_start),.source_x(source_x),.source_y(source_y),
        .pixel_rgba(pixel_rgba),.pixel_valid(pixel_valid),.frame_config(frame_config),
        .load_data(load_data),.load_valid(load_valid),.load_first(load_first),.load_last(load_last),
        .load_epoch(load_epoch),.load_offset(load_offset),.load_total(load_total),
        .load_abort(load_abort),.load_abort_epoch(load_abort_epoch),.load_ready(load_ready),
        .transport_abort(transport_abort),
        .loaded(loaded),.error(error),.capture_clk(clk),
        .psram_a_io(psram_a_io),.psram_b_io(psram_b_io),.psram_ce_n(psram_ce_n),.psram_clk(psram_clk));
    multitini_vectrex_video_bridge bridge(
        .pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),.source_ready(1'b1),
        .scan_x(scan_x),.scan_y(scan_y),.vblank_start(vblank_start),.pixel_address(beam_address),
        .pixel_intensity(8'd0),.overlay_rgba(pixel_rgba),.overlay_valid(pixel_valid),.video_config(frame_config),
        .source_x(source_x),.source_y(source_y),.machine_active(),.machine_rgb565(rgb),
        .viewport_x(),.viewport_y(),.viewport_width(),.viewport_height());
    function automatic [15:0] artwork(input integer r,input integer c);
        artwork=16'hffff ^ (r*31+c*17);
    endfunction
    function automatic [15:0] background(input integer r,input integer c);
        integer word_value,a,red,green,blue;
        begin
            word_value=artwork(r,c);a=(word_value>>12)&15;
            red=(word_value&15)*17*a/16;green=((word_value>>4)&15)*17*a/16;blue=((word_value>>8)&15)*17*a/16;
            background=((red>>3)<<11)|((green>>2)<<5)|(blue>>3);
        end
    endfunction
    task automatic send_byte(input integer position,input integer total,input integer data,input bit epoch);
        begin
            @(negedge clk);
            while(!load_ready)@(negedge clk);
            load_offset=position;load_total=total;load_data=data;load_first=position==0;
            load_last=position+1==total;load_epoch=epoch;load_valid=1;
            @(negedge clk);load_valid=0;
        end
    endtask
    task automatic scan_line(input integer line_number,input bit check_picture);
        integer p;
        begin
            for(p=0;p<2200;p=p+1)begin
                scan_x=p;scan_y=line_number;
                #1;
                if(check_picture && p>=555 && p<1365)begin
                    expected=background(line_number*2/3,(p-555)*2/3);
                    if(rgb!==expected)$fatal(1,"Overlay pixel x%0d y%0d got%h expected%h cached=%b fetchrow=%0d consumer=%0d",p,line_number,rgb,expected,pixel_valid,dut.fetch_row,dut.consumer_row);
                    checks=checks+1;
                end else if(check_picture && p>0 && rgb!==0)$fatal(1,"Nonblack outer margin");
                @(negedge pixel_clk);
            end
        end
    endtask
    initial begin
        repeat(5)@(negedge clk);resetn=1;pixel_resetn=1;control_resetn=1;
        scan_x=1;
        send_byte(0,2,0,0);if(!error||loaded)$fatal(1,"Badlength accepted");
        send_byte(0,1,0,1);if(error||loaded)$fatal(1,"Clear failed");
        for(offset=0;offset<100;offset=offset+1)send_byte(offset,777600,offset,0);
        @(negedge clk);load_abort_epoch=0;load_abort=1;transport_abort=1;
        @(negedge clk);load_abort=0;transport_abort=0;
        if(!error||loaded)$fatal(1,"Abort did not invalidate");
        send_byte(100,777600,0,0);
        if(!error||loaded)$fatal(1,"Stalebyte changed status");
        send_byte(0,1,0,1);
        if(error||loaded)$fatal(1,"Clear after abort failed");
        if(dut.aborted_valid)$fatal(1,"Clear did not retire the old abort");
        // Epochs advance after aborts only. An empty other-kind stream must
        // retire the marker too, even though this loader sees no data bytes.
        @(negedge clk);load_abort_epoch=0;load_abort=1;transport_abort=1;
        @(negedge clk);load_abort=0;transport_abort=0;
        @(negedge clk);load_abort_epoch=1;transport_abort=1;
        @(negedge clk);transport_abort=0;
        for(offset=0;offset<777600;offset=offset+1)begin
            expected=artwork(offset/1080,(offset%1080)/2);
            send_byte(offset,777600,(offset&1)?expected[15:8]:expected[7:0],0);
        end
        wait(loaded||error);if(error)$fatal(1,"Completeupload failed");
        for(row=0;row<720;row=row+1)begin
            for(column=0;column<540;column=column+1)
                if(dut.psram_i.memory[row*544+column]!==artwork(row,column))$fatal(1,"Upload row%0d column%0d",row,column);
            for(column=540;column<544;column=column+1)
                if(dut.psram_i.memory[row*544+column]!==0)$fatal(1,"Row padding is notzero");
        end
        // Completed artwork becomes visible only after vblank and prefetch.
        repeat(100)@(negedge pixel_clk);
        for(y=1080;y<1125;y=y+1)scan_line(y,0);
        if(frame_config!==5'h19)$fatal(1,"Config not committed");
        for(y=0;y<1080;y=y+1)scan_line(y,1);
        for(y=1080;y<1125;y=y+1)scan_line(y,0);
        for(y=0;y<6;y=y+1)scan_line(y,1);
        // Source-only config reset must resend; updates wait for vblank.
        video_config=32'h0b;repeat(50)@(negedge pixel_clk);
        if(frame_config!==5'h19)$fatal(1,"Config changed midframe");
        scan_line(1080,0);if(frame_config!==5'h0b)$fatal(1,"Config vblank update failed");
        control_resetn=0;repeat(5)@(negedge control_clk);video_config=32'h19;control_resetn=1;
        repeat(70)@(negedge pixel_clk);scan_line(1080,0);
        if(frame_config!==5'h19)$fatal(1,"Control reset resend failed");
        pixel_resetn=0;repeat(6)@(negedge pixel_clk);pixel_resetn=1;
        repeat(70)@(negedge pixel_clk);scan_line(1080,0);
        if(frame_config!==5'h19)$fatal(1,"Pixel reset resend failed");
        @(negedge clk);load_abort_epoch=0;transport_abort=1;
        @(negedge clk);transport_abort=0;
        if(!loaded||error)$fatal(1,"Other-kind abort hid artwork");
        @(negedge clk);load_abort_epoch=0;load_abort=1;transport_abort=1;
        @(negedge clk);load_abort=0;transport_abort=0;
        repeat(12)@(negedge pixel_clk);
        if(loaded||!error||pixel_valid)$fatal(1,"Late abort left completed artwork visible");
        send_byte(0,1,0,1);repeat(12)@(negedge pixel_clk);
        if(loaded||error||pixel_valid)$fatal(1,"Clear leftartworkvisible");
        $display("PASS Vectrex overlay: %0d viewportpixels,777600-byte upload/padding,720rows/prefetch/frame restart,abort/stale/clear/config resets",checks);
        $finish;
    end
    initial begin #150000000;$fatal(1,"Overlay regression timeout");end
endmodule
