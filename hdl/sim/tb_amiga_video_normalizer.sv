// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_amiga_video_normalizer;
    // Independent expected display geometry, calibrated by actual native
    // DIW/bitplane-DMA/Denise RGB markers after the recognized VSYNC.
    localparam integer WIDTH=640,HEIGHT=512,STRIDE=640,HSYNC_TO_ACTIVE=206;
    localparam integer PAL_FIRST_LINE=41,NTSC_FIRST_LINE=12,LINE_TICKS=1816;
    reg clk=0,reset=1,ready=0,chip_tick=0,hsync_n=1,vsync_n=1,ntsc=0,hblank=0,vblank=0,field_short=0;
    reg [23:0] rgb=0;
    reg [1:0] capture_mode=0;
    reg native_lace=0;
    reg [8:0] diw_hstart=129;
    reg [10:0] diw_vstart=44;
    wire frame_alt,frame_weave;wire[1:0]frame_profile;
    always #5 clk=~clk;
    wire [11:0] scan_x,scan_y;
    wire [15:0] rgb565;
    wire frame_start,valid;
    wire [31:0] malformed_frames;
    multitini_amiga_video_normalizer dut(.*);
    integer expected=0,pixels=0,frames=0,mode=0,starts=0;
    integer ex,ey,source_y;
    integer pal_reference_first_line=PAL_FIRST_LINE;
    reg [15:0] expected_rgb;
    function automatic [15:0] pattern(input integer x,y);
        pattern={5'((x/8)^y),6'((x>>5)^(y*3)),5'(x^(y*7))};
    endfunction
    always @(negedge clk)begin
        if(frame_start)begin
            starts=starts+1;
            if(mode==1)begin
                if(expected!=0 && expected!=STRIDE*HEIGHT)$fatal(1,"next field discarded undrained pixels:%0d",expected);
                expected=0;frames=frames+1;
            end
        end
        if(mode==1 && valid)begin
            ex=expected%STRIDE;ey=expected/STRIDE;
            if(scan_x!=ex || scan_y!=ey)$fatal(1,"output ordering expected%0d,%0d got%0d,%0d",ex,ey,scan_x,scan_y);
            if(ey>=HEIGHT)$fatal(1,"output escaped fixed canvas");
            source_y=ntsc ? (ey-16)/2 : ey/2;
            expected_rgb=((ntsc && (ey<16 || ey>=496)) ||
                          (ex>=42 && ex<48) || source_y==7) ? 16'd0 : pattern(ex,source_y);
            if(rgb565!==expected_rgb)$fatal(1,"RGB placement at%0d,%0d expected%h got%h",ex,ey,expected_rgb,rgb565);
            expected=expected+1;pixels=pixels+1;
        end
    end
    task chip(input integer spacing);
        begin
            chip_tick=1;@(negedge clk);chip_tick=0;
            repeat(spacing-1)@(negedge clk);
        end
    endtask
    task source_line(input integer y,rate);
        integer q,x,row,spacing;
        reg [15:0] color;
        begin
            hsync_n=0;vsync_n=(y!=0);rgb=0;hblank=0;vblank=0;
            chip(rate==0 ? 4 : 1);
            hsync_n=1;vsync_n=1;
            for(q=0;q<LINE_TICKS-1;q=q+1)begin
                x=(q/2)-HSYNC_TO_ACTIVE;row=y-(ntsc?NTSC_FIRST_LINE:pal_reference_first_line);color=pattern(x,row);
                rgb={color[15:11],3'd0,color[10:5],2'd0,color[4:0],3'd0};
                hblank=(x>=42 && x<48);vblank=(row==7);
                // Uneven chip enables model memory pauses while the output
                // producer retains the complete field and fixed padding.
                spacing=rate==1 ? 1 :
                    (rate==2 ? 1+((q%31)==0 ? 5 : 0)+((q%7)==0 ? 1 : 0) :
                               4+((q%31)==0 ? 5 : 0)+((q%7)==0 ? 1 : 0));
                chip(spacing);
            end
        end
    endtask
    task reset_canvas(input integer standard);
        begin
            @(negedge clk);reset=1;ready=0;chip_tick=0;hsync_n=1;vsync_n=1;ntsc=standard;mode=0;expected=0;
            field_short=0;pal_reference_first_line=PAL_FIRST_LINE;
            repeat(5)@(negedge clk);reset=0;ready=1;repeat(3)@(negedge clk);
        end
    endtask
    task interlace_field_boundaries;
        integer y,q,saved_pixels,saved_frames;
        begin
            reset_canvas(0);saved_pixels=pixels;saved_frames=frames;mode=1;
            pal_reference_first_line=PAL_FIRST_LINE+1;field_short=1;
            // Native short interlace fields assert VSYNC midway through the
            // line. The next HSYNC is native_y1; the active window starts42.
            hsync_n=0;vsync_n=1;rgb=0;hblank=0;vblank=0;chip(1);hsync_n=1;
            for(q=0;q<LINE_TICKS/2-1;q=q+1)chip(1);
            vsync_n=0;chip(1);vsync_n=1;
            // Change toward the following long field well before its VSYNC.
            // The current frame must continue using the latched short flag.
            field_short=0;
            for(q=0;q<LINE_TICKS/2-1;q=q+1)chip(1);
            for(y=1;y<313;y=y+1)source_line(y,1);
            pal_reference_first_line=PAL_FIRST_LINE;
            source_line(0,1);
            // The long-field selection also remains latched until VSYNC.
            field_short=1;
            for(y=1;y<313;y=y+1)source_line(y,1);
            repeat(2000)@(negedge clk);
            if(pixels-saved_pixels!=2*STRIDE*HEIGHT || expected!=STRIDE*HEIGHT || frames-saved_frames!=2 || malformed_frames)
                $fatal(1,"interlace short/long crop lost pixels%0d frames%0d malformed%0d",pixels-saved_pixels,frames-saved_frames,malformed_frames);
            mode=0;
            $display("PASS AMIGA shortmidlineVSYNC/longfield41and42roworigins latchuntilnextVSYNC");
        end
    endtask
    task adjacent_pal_fields;
        integer field,y,saved_pixels,saved_frames;
        begin
            reset_canvas(0);saved_pixels=pixels;saved_frames=frames;mode=1;
            for(field=0;field<2;field=field+1)
                for(y=0;y<312;y=y+1)source_line(y,1);
            repeat(3000)@(negedge clk);
            if(pixels-saved_pixels!=2*STRIDE*HEIGHT || expected!=STRIDE*HEIGHT || frames-saved_frames!=2 || malformed_frames)
                $fatal(1,"adjacent312-line PAL fields lost final-row drain pixels%0d frames%0d malformed%0d",pixels-saved_pixels,frames-saved_frames,malformed_frames);
            mode=0;
            $display("PASS AMIGA full-rate1:1 adjacent312-line PAL fields preserve full640x512 activearea");
        end
    endtask
    task early_vsync_fields;
        integer field,y,saved_pixels,saved_frames;
        begin
            reset_canvas(0);saved_pixels=pixels;saved_frames=frames;mode=1;
            // End the field immediately after the last captured source row.
            // Its doubled output is still draining when the next VSYNC hits.
            for(field=0;field<2;field=field+1)
                for(y=0;y<PAL_FIRST_LINE+256;y=y+1)source_line(y,1);
            repeat(2000)@(negedge clk);
            if(pixels-saved_pixels!=2*STRIDE*HEIGHT || expected!=STRIDE*HEIGHT || frames-saved_frames!=2 || malformed_frames)
                $fatal(1,"earlyVSYNC lost final-row drain pixels%0d frames%0d malformed%0d",pixels-saved_pixels,frames-saved_frames,malformed_frames);
            mode=0;
            $display("PASS AMIGA earlyVSYNC waits for final512rows todrain before nextfield publication");
        end
    endtask
    task full_field(input integer standard);
        integer y,saved_pixels;
        begin
            reset_canvas(standard);saved_pixels=pixels;mode=1;
            for(y=0;y<(standard?263:313);y=y+1)source_line(y,standard?1:2);
            repeat(3000)@(negedge clk);
            if(expected!=STRIDE*HEIGHT || pixels-saved_pixels!=STRIDE*HEIGHT)$fatal(1,"incomplete canvas standard%0d pixels%0d",standard,expected);
            if(malformed_frames)$fatal(1,"well-timed field marked malformed");
            mode=0;
            $display("PASS AMIGA %s field:640x%0d source,640x512 canvas,RGB565,active crop,bob doubling,native ticks",standard?"NTSC":"PAL",standard?480:512);
        end
    endtask
    integer y,saved_malformed,saved_starts;
    initial begin
        $display("AMIGA VIDEO referenceX=%0d,PALfirstline=%0d,lineTicks=%0d,canvas=%0dx%0d,stride=%0d",
                 HSYNC_TO_ACTIVE,PAL_FIRST_LINE,LINE_TICKS,WIDTH,HEIGHT,STRIDE);
        full_field(0);
        full_field(1);
        adjacent_pal_fields;
        early_vsync_fields;
        interlace_field_boundaries;
        // Explicit defensive fault injection: legal PAL/NTSC source timing
        // fits the banks. A forced occupied-bank condition must suppress a
        // corrupt field and recover cleanly after readiness is interrupted.
        reset_canvas(0);
        for(y=0;y<PAL_FIRST_LINE;y=y+1)source_line(y,1);
        force dut.full=2'b11;
        source_line(PAL_FIRST_LINE,1);
        release dut.full;
        repeat(20)@(negedge clk);
        if(!malformed_frames)$fatal(1,"line bank overrun was not counted");
        if(valid!==0)$fatal(1,"line bank overrun kept emitting a corrupt field");
        saved_malformed=malformed_frames;saved_starts=starts;
        ready=0;repeat(5)@(negedge clk);ready=1;
        // A readiness interruption discards partial banks, and the next
        // native VSYNC is the only event allowed to start a fresh field.
        repeat(20)@(negedge clk);
        if(valid || starts!=saved_starts)$fatal(1,"readiness restart emitted a partial field");
        source_line(0,1);repeat(20)@(negedge clk);
        if(starts!=saved_starts+1 || malformed_frames!=saved_malformed)$fatal(1,"frame recovery did not restart cleanly");
        $display("PASS AMIGA injectedoccupiedbank overrun suppression andreadiness recovery");
        $display("AMIGA VIDEO verifiedpixels=%0d",pixels);
        $finish;
    end
    initial begin #100000000;$fatal(1,"video watchdog");end
endmodule
