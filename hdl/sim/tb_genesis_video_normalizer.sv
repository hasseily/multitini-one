// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_genesis_video_normalizer;
    reg clk=0,resetn=0,ready=0,pixel_ce=0,de=0,vsync=0,mode_240=0;
    always #5 clk=~clk;
    reg [7:0] red=0,green=0,blue=0;
    wire [11:0] scan_x,scan_y;
    wire [15:0] rgb565;
    wire frame_start;
    wire [31:0] dropped_fields;
    integer width=320,height=224,count=0,fields=0,expected_x,expected_y,sx,sy;
    reg checking=0;
    reg [15:0] expected;
    multitini_genesis_video_normalizer dut(.*);
    function automatic [15:0] pattern(input integer x,y);
        pattern={5'(x),6'(y),5'(x^y)};
    endfunction
    always @(negedge clk)if(checking && scan_x!=12'hfff)begin
        expected_x=count%320;expected_y=count/320;
        if(scan_x!==12'(expected_x) || scan_y!==12'(expected_y))
            $fatal(1,"Coordinate %0d got %0d,%0d",count,scan_x,scan_y);
        sy=expected_y-(height==224 ? 8 : 0);
        sx=width<=256 ? expected_x*4/5 : expected_x;
        if(width==248 || width==312)sx=sx-8;
        expected=sy<0 || sy>=height ? 0 : pattern(sx,sy);
        if(sx<0)expected=0;
        if(rgb565!==expected)$fatal(1,"Pixel %0d,%0d got %h expected %h",scan_x,scan_y,rgb565,expected);
        count=count+1;
    end
    task pulse;
        begin @(negedge clk);pixel_ce=1;@(negedge clk);pixel_ce=0;repeat(6)@(negedge clk);end
    endtask
    task field(input integer w,h);
        integer x,y;
        reg [15:0] color;
        begin
            width=w;height=h;mode_240=h==240;count=0;checking=1;
            @(negedge clk);vsync=1;repeat(8)@(negedge clk);vsync=0;
            repeat(4000)@(negedge clk);
            for(y=0;y<h;y=y+1)begin
                de=1;
                for(x=0;x<w;x=x+1)begin
                    color=pattern(x,y);red={color[15:11],3'd0};green={color[10:5],2'd0};blue={color[4:0],3'd0};pulse();
                end
                de=0;pulse();repeat(360)@(negedge clk);
            end
            repeat(4000)@(negedge clk);
            if(count!=320*240)$fatal(1,"Incomplete normalized field %0dx%0d: %0d",w,h,count);
            if(dropped_fields!=0)$fatal(1,"Legal field dropped");
            checking=0;fields=fields+1;
        end
    endtask
    initial begin
        repeat(5)@(negedge clk);resetn=1;ready=1;
        field(256,224);field(320,224);field(256,240);field(320,240);
        field(248,224);field(312,240);
        // A malformed active line must stop the field and be counted at the next sync.
        vsync=1;repeat(3)@(negedge clk);vsync=0;de=1;
        repeat(17)pulse();de=0;pulse();repeat(400)@(negedge clk);
        if(scan_x!=12'hfff)$fatal(1,"Malformed field kept producing pixels");
        vsync=1;repeat(3)@(negedge clk);vsync=0;
        if(dropped_fields!=1)$fatal(1,"Malformed field was not counted");
        ready=0;repeat(3)@(negedge clk);
        if(scan_x!=12'hfff || scan_y!=12'hfff)$fatal(1,"Not-ready output remained active");
        $display("PASS Genesis normalization: %0d complete H32/H40, V224/V240 fields, exact colors/coordinates, malformed rejection",fields);
        $finish;
    end
    initial begin #50000000;$fatal(1,"Normalizer timeout");end
endmodule
