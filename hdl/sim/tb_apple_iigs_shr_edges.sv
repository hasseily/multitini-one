// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Real VGC, real one-pixel synchronous video RAM latency, and the production
// adapter/normalizer publication timing. Distinct palette entries expose a
// lost/duplicated edge pixel; a blue border exposes accidental border capture.
module tb_apple_iigs_shr_edges;
    reg clk=0;always #5 clk=~clk;
    reg [1:0] phase=0;
    wire ce=phase==3;
    always @(posedge clk)phase<=phase+1'b1;
    reg [9:0] h=0;
    reg [8:0] v=255;
    reg mode640=0,fill_mode=0;
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
    integer checked=0,total=0;
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
        .TEXTCOLOR(8'hf0),.BORDERCOLOR(4'h2),.HIRES_MODE(1'b0),.AN3(1'b0),
        .STORE80(1'b0),.ALTCHARSET(1'b0),.EIGHTYCOL(1'b0),.PAGE2(1'b0),
        .TEXTG(1'b0),.MIXG(1'b0),.SHRG(1'b1),.DHRG_MONO(1'b0),
        .scanline_irq(),.vbl_irq(),.shr_bus_byte());
    always @(posedge clk)begin
        if(ce)data<=memory_byte(address);
        valid<=ce;
        if(ce)begin published_h<=h;published_v<=v;end
    end
    multitini_apple_iigs_video_normalizer normalizer(.clk(clk),.resetn(ready),.ready(ready),
        .pixel_ce(valid),.pixel_x(published_h),.pixel_y(published_v),.red(r),.green(g),.blue(b),
        .scan_x(x),.scan_y(y),.rgb565(rgb),.frame_start(),.malformed_frames(malformed));
    always @(negedge clk)if(ready && x<640 && y<400)begin:check
        integer px,row,index,last,n;
        reg [7:0] packed_pixels;
        reg [11:0] expected;
        reg [15:0] expected565;
        px=int'(x);row=int'(y)/2;packed_pixels=pixel_byte(row,px/4);
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
        expected565={expected[11:8],expected[11],expected[7:4],expected[7:6],expected[3:0],expected[3]};
        if(x!==12'(checked%640) || y!==12'(checked/640) || rgb!==expected565)
            $fatal(1,"SHR mode640=%0d fill=%0d pixel%0d (%0d,%0d) got%h expected%h",mode640,fill_mode,checked,x,y,rgb,expected565);
        checked=checked+1;total=total+1;
    end
    task frame(input bit wide,fill);
        begin
            ready=0;mode640=wide;fill_mode=fill;repeat(8)@(negedge clk);
            checked=0;ready=1;
            for(integer row=254;row<=456;row=row+1)
                for(integer col=0;col<912;col=col+1)begin
                    while(phase!=3)@(negedge clk);
                    h=10'(col);v=9'(row);@(negedge clk);
                end
            repeat(1500)@(negedge clk);
            if(checked!=256000 || malformed!=0)$fatal(1,"SHR incomplete or malformed frame");
        end
    endtask
    initial begin
        frame(0,0);frame(1,0);frame(0,1);
        $display("PASS IIgs SHR edges: %0d production RGB565 pixels, 320/640/fill, all640 columns, blue border excluded",total);
        $finish;
    end
    initial begin #50000000;$fatal(1,"SHR edge test timeout");end
endmodule
