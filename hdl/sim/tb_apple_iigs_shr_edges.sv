// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Real VGC, real one-pixel synchronous video RAM latency, and the production
// adapter/normalizer publication timing. Distinct palette entries expose a
// lost/duplicated edge pixel. Native C034/BORDERCOLOR palette values change by
// raw scanline, exposing cropped borders, wrong PAL rows and RGB blanking.
module tb_apple_iigs_shr_edges;
    reg clk=0;always #5 clk=~clk;
    reg [1:0] phase=0;
    wire ce=phase==3;
    always @(posedge clk)phase<=phase+1'b1;
    wire [10:0] native_h;
    wire [9:0] native_v;
    wire [9:0] h=native_h[9:0];
    wire [8:0] v=native_v[8:0];
    wire hblank,vblank;
    reg pal=0,mode640=0,fill_mode=0;
    reg [3:0] bordercolor=2;
    wire [22:0] address;
    reg [7:0] data=0;
    wire [7:0] r,g,b;
    reg ready=0;
    reg valid=0;
    reg [9:0] published_h=0;
    reg [8:0] published_v=0;
    wire [11:0] x,y;
    wire [15:0] rgb;
    wire [31:0] malformed;
    integer checked=0,total=0,frames=0,active_pixels=0,padding_pixels=0;
    integer left_pixels=0,right_pixels=0,top_pixels=0,bottom_pixels=0;
    integer native_wraps=0,native_frames_checked=0,native_lines=0;
    integer previous_h=0,previous_v=0;
    reg native_started=0,have_wrap=0,previous_pal=0,native_frame_pal=0;
    video_timing timing(.clk_vid(clk),.ce_pix(ce),.pal(pal),
        .hsync(),.vsync(),.hblank(hblank),.vblank(vblank),.mega2_vbl(),
        .hpos(native_h),.vpos(native_v),.hchar(),.m2_vpos(),
        .ph0_phase(),.ph0_stb(),.line_stb());
    // Observe the real guest-visible raster; normalization must not change it.
    always @(posedge clk)if(ce)begin
        if(native_started)begin
            if(int'(native_h)!=(previous_h==911 ? 0 : previous_h+1) ||
               int'(native_v)!=(previous_h==911 ?
                   (previous_v==511 ? (previous_pal ? 200 : 250) : previous_v+1) : previous_v))
                $fatal(1,"native raster changed h=%0d v=%0d",native_h,native_v);
        end
        if(native_h==911)begin
            native_lines=native_lines+1;
            if(native_v==511)begin
                if(have_wrap)begin
                    if(native_lines!=(native_frame_pal ? 312 : 262))
                        $fatal(1,"native vertical length changed: %0d",native_lines);
                    native_frames_checked=native_frames_checked+1;
                end
                native_lines=0;have_wrap=1;native_frame_pal=pal;
                native_wraps=native_wraps+1;
            end
        end
        previous_h=int'(native_h);previous_v=int'(native_v);
        previous_pal=pal;native_started=1;
    end
    function automatic [3:0] border_index(input integer raw_row);
        case(raw_row%3) 0:border_index=2;1:border_index=1;default:border_index=4;endcase
    endfunction
    function automatic [11:0] border_rgb(input integer raw_row);
        // The legacy palette selected by the low nibble of C034.
        case(raw_row%3) 0:border_rgb=12'h009;1:border_rgb=12'hd03;default:border_rgb=12'h070;endcase
    endfunction
    function automatic integer raw_row(input integer row);
        raw_row=pal ? row+232 : (row<18 ? row+494 : row+232);
    endfunction
    always @(negedge clk)if(ce && h==0)bordercolor=border_index(int'(v));
    function automatic [7:0] pixel_byte(input integer row,index);
        pixel_byte=8'((index*29+row*13+8'h37)^((index>>3)*7));
    endfunction
    function automatic [11:0] color(input integer index);
        color={4'(index),4'(15-index),4'(index^5)};
    endfunction
    function automatic [7:0] memory_byte(input [22:0] a);
        integer row,index;
        begin
            if(a>=23'h19d00 && a<23'h19dc8)memory_byte={mode640,1'b0,fill_mode,5'd0};
            else if(a>=23'h19e00 && a<23'h19e20)begin
                index=int'((a-23'h19e00)>>1);
                memory_byte=a[0] ? {4'd0,4'(index)} : {4'(15-index),4'(index^5)};
            end else if(a>=23'h12000 && a<23'h19d00)begin
                row=int'((a-23'h12000)/160);index=int'((a-23'h12000)%160);
                memory_byte=pixel_byte(row,index);
            end else memory_byte=0;
        end
    endfunction
    vgc machine(.load_rom_clk(clk),.load_rom_we(1'b0),.load_rom_addr(12'd0),.load_rom_data(8'd0),
        .CLK_28M(clk),.CLK_14M(clk),.clk_vid(clk),.ce_pix(ce),.H(h),.V(v),
        .R(r),.G(g),.B(b),.video_addr(address),.video_data(data),
        .TEXTCOLOR(8'hf0),.BORDERCOLOR(bordercolor),.HIRES_MODE(1'b0),.AN3(1'b0),
        .STORE80(1'b0),.ALTCHARSET(1'b0),.EIGHTYCOL(1'b0),.PAGE2(1'b0),
        .TEXTG(1'b0),.MIXG(1'b0),.SHRG(1'b1),.DHRG_MONO(1'b0),
        .scanline_irq(),.vbl_irq(),.shr_bus_byte());
    always @(posedge clk)begin
        if(ce)data<=memory_byte(address);
        valid<=ce;
        if(ce)begin published_h<=h;published_v<=v;end
    end
    multitini_apple_iigs_video_normalizer normalizer(.clk(clk),.resetn(ready),.ready(ready),
        .pal(pal),
        .pixel_ce(valid),.pixel_x(published_h),.pixel_y(published_v),.red(r),.green(g),.blue(b),
        .scan_x(x),.scan_y(y),.rgb565(rgb),.frame_start(),.malformed_frames(malformed));
    always @(negedge clk)if(ready && x<768 && y<496)begin:check
        integer px,row,index,last,n;
        reg [7:0] packed_pixels;
        reg [11:0] expected;
        reg [15:0] expected565;
        if(x!==12'(checked%768) || y!==12'(checked/768))
            $fatal(1,"border/stride coordinate mismatch pixel%0d at%0d,%0d",checked,x,y);
        px=int'(x)-42;row=int'(y)/2-24;packed_pixels=0;expected=0;
        if(x>=724)begin
            padding_pixels=padding_pixels+1;
        end else if(x<42 || x>=682 || y<48 || y>=448)begin
            expected=border_rgb(raw_row(int'(y)/2));
            if(y<48)top_pixels=top_pixels+1;
            else if(y>=448)bottom_pixels=bottom_pixels+1;
            else if(x<42)left_pixels=left_pixels+1;
            else right_pixels=right_pixels+1;
        end else begin
        active_pixels=active_pixels+1;packed_pixels=pixel_byte(row,px/4);
        if(mode640)begin
            case(px%4)
                0:index=8+int'(packed_pixels[7:6]);
                1:index=12+int'(packed_pixels[5:4]);
                2:index=int'(packed_pixels[3:2]);
                3:index=4+int'(packed_pixels[1:0]);
            endcase
        end else begin
            index=(px%4)<2 ? int'(packed_pixels[7:4]):int'(packed_pixels[3:0]);
            if(fill_mode && index==0)begin
                last=0;
                for(n=0;n<=px/2;n=n+1)begin
                    packed_pixels=pixel_byte(row,n/2);
                    index=(n%2)==0 ? int'(packed_pixels[7:4]):int'(packed_pixels[3:0]);
                    if(index!=0)last=index;
                end
                index=last;
            end
        end
        expected=color(index);
        end
        expected565={expected[11:8],expected[11],expected[7:4],expected[7:6],expected[3:0],expected[3]};
        if(rgb!==expected565)
            $fatal(1,"SHR PAL=%0d mode640=%0d fill=%0d pixel%0d (%0d,%0d) rawV=%0d got%h expected%h",pal,mode640,fill_mode,checked,x,y,raw_row(int'(y)/2),rgb,expected565);
        checked=checked+1;total=total+1;
    end
    task frame(input bit format_pal,wide,fill);
        integer wraps_before;
        begin
            @(negedge clk);#1;ready=0;pal=format_pal;mode640=wide;fill_mode=fill;
            checked=0;active_pixels=0;padding_pixels=0;
            left_pixels=0;right_pixels=0;top_pixels=0;bottom_pixels=0;
            // Let the actual counter load the requested NTSC/PAL preset,
            // then enable before the normalizer's H0 field-start sample.
            wraps_before=native_wraps;
            while(native_wraps==wraps_before)@(negedge clk);
            while(!(phase==3 && h==0 && v==(format_pal ? 232 : 494)))@(negedge clk);
            #1;ready=1;
            while(checked<768*496)@(negedge clk);
            #1;
            if(checked!=768*496 || malformed!=0 || active_pixels!=256000 ||
               padding_pixels!=44*496 || top_pixels!=724*48 || bottom_pixels!=724*48 ||
               left_pixels!=42*400 || right_pixels!=42*400)
                $fatal(1,"SHR border/active/stride counts wrong");
            frames=frames+1;
            $display("PASS SHR bordered frame PAL=%0d mode640=%0d fill=%0d real724x496 stride768 active640x400",pal,mode640,fill_mode);
            ready=0;
        end
    endtask
    initial begin
        frame(0,0,0);frame(0,1,0);frame(0,0,1);
        frame(1,0,0);frame(1,1,0);frame(1,0,1);
        if(frames!=6 || native_frames_checked<6)$fatal(1,"native frame matrix incomplete");
        $display("PASS IIgs SHR edges: %0d production RGB565 pixels,6 NTSC/PAL320/640/fill frames,42pixel sides,24source lines top/bottom,all640 imagecolumns,44stridepadding,native912x262/312 unchanged",total);
        $finish;
    end
    initial begin #250000000;$fatal(1,"SHR edge test timeout");end
endmodule
