// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_vectrex_compositor;
    reg clk=0,resetn=0,visible=0,artwork_valid=0;
    reg [7:0] intensity=0;
    reg [15:0] rgba=0;
    reg [4:0] video_config=0;
    wire [15:0] rgb565;
    reg [15:0] expected[0:3];
    integer valid_count=0,checks=0,word_number,gain_number,opacity_number;
    always #3.367 clk=~clk;
    multitini_vectrex_compositor dut(.*);
    function automatic [15:0] reference_pixel(input integer data,input integer level,
        input integer cfg,input integer show,input integer have_art);
        integer i,w,a,opacity,c,beam,bg,result[0:2],channel;
        begin
            i=level*(((cfg>>1)&3)+1);if(i>255)i=255;
            w=i*2-255;if(w<0)w=0;
            a=(data>>12)&15;opacity=((cfg>>3)&3)+1;
            for(channel=0;channel<3;channel=channel+1) begin
                c=((data>>(channel*4))&15)*17;
                if((data&4095)==0)begin c=255;a=0;end
                beam=i==255 ? 255 : (i*c+w*(255-c))/256;
                bg=c*a*opacity/64;
                result[channel]=((cfg&1)!=0 && have_art!=0) ? bg+beam : i;
                if(result[channel]>255)result[channel]=255;
            end
            reference_pixel=show==0 ? 0 : ((result[0]>>3)<<11)|((result[1]>>2)<<5)|(result[2]>>3);
        end
    endfunction
    always @(posedge clk) begin
        if(!resetn)begin valid_count=0;expected[0]=0;expected[1]=0;expected[2]=0;expected[3]=0;end
        else begin
            expected[3]=expected[2];
            expected[2]=expected[1];expected[1]=expected[0];
            expected[0]=reference_pixel(rgba,intensity,video_config,visible,artwork_valid);
            valid_count=valid_count+1;
            #1;
            if(valid_count>=4)begin
                if(rgb565!==expected[3])$fatal(1,"Compositor check%0d got%h expected%h",checks,rgb565,expected[3]);
                checks=checks+1;
            end
        end
    end
    initial begin
        repeat(4)@(negedge clk);resetn=1;visible=1;artwork_valid=1;
        // Every RGBA4444 value with every gain/opacity combination. Intensity
        // varies independently, exercising alpha-zero colored artwork too.
        for(word_number=0;word_number<65536;word_number=word_number+1)
            for(gain_number=0;gain_number<4;gain_number=gain_number+1)
                for(opacity_number=0;opacity_number<4;opacity_number=opacity_number+1)begin
                    rgba=word_number;intensity=(word_number*37+gain_number*53+opacity_number*91)&255;
                    video_config=1|(gain_number<<1)|(opacity_number<<3);
                    @(negedge clk);
                end
        // Every intensity at all gains, including explicitly absent/disabled
        // artwork and active-video blanking.
        for(word_number=0;word_number<256;word_number=word_number+1)
            for(gain_number=0;gain_number<4;gain_number=gain_number+1)begin
                intensity=word_number;rgba=16'hffff;video_config=gain_number<<1;
                artwork_valid=1;visible=1;@(negedge clk);
                video_config=video_config|1;artwork_valid=0;@(negedge clk);
                visible=0;@(negedge clk);
            end
        repeat(5)@(negedge clk);
        $display("PASS Vectrex compositor: %0d pipeline pixels, fullRGBA/allgain/allopacity andblanking",checks);
        $finish;
    end
endmodule
