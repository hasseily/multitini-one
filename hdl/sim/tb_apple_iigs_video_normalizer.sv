// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_video_normalizer;
    reg clk=0;always #5 clk=~clk;
    reg resetn=0,ready=0,pixel_ce=0,pal=0;
    reg [9:0] x=0;
    reg [8:0] y=0;
    reg [7:0] r=0,g=0,b=0;
    wire [11:0] out_x,out_y;
    wire [15:0] rgb;
    wire start_frame;
    wire [31:0] malformed;
    localparam integer WIDTH=724,STRIDE=768,HEIGHT=496,FRAME_SAMPLES=380928;
    integer emitted=0,frames=0,total_checked=0,image_checked=0,padding_checked=0;
    integer active_checked=0,border_checked=0,source_row,source_x,source_y;
    reg checking=0,expected_pal=0;
    reg [23:0] expected_color;
    reg [15:0] expected_rgb;
    multitini_apple_iigs_video_normalizer dut(
        .clk(clk),.resetn(resetn),.ready(ready),.pal(pal),
        .pixel_ce(pixel_ce),.pixel_x(x),.pixel_y(y),
        .red(r),.green(g),.blue(b),.scan_x(out_x),.scan_y(out_y),.rgb565(rgb),
        .frame_start(start_frame),.malformed_frames(malformed));

    // A different native color on every source line catches copied/padded
    // border rows. The active image is independently address-coded.
    function automatic [23:0] native_color(input integer sx,sy);
        if(sx>=44 && sx<684 && sy>=256 && sy<456)
            native_color={8'(sx-44),8'(sy-256),8'((sx-44)^(sy-256)^8'ha5)};
        else native_color={8'(sy^8'hd5),8'(sy+8'h3b),8'(8'hf0^(sy<<1))};
    endfunction
    always @(negedge clk)begin
        if(checking && start_frame)begin emitted=0;frames=frames+1;end
        if(checking && ready && out_x<STRIDE && out_y<HEIGHT)begin
            if(out_x!==12'(emitted%STRIDE) || out_y!==12'(emitted/STRIDE))
                $fatal(1,"coordinate mismatch sample %0d got %0d,%0d",emitted,out_x,out_y);
            if(out_x<WIDTH)begin
                source_x=2+out_x;
                source_row=out_y/2;
                source_y=expected_pal ? 232+source_row : source_row<18 ? 494+source_row : 232+source_row;
                expected_color=native_color(source_x,source_y);
                expected_rgb={expected_color[23:19],expected_color[15:10],expected_color[7:3]};
                image_checked=image_checked+1;
                if(out_x>=42 && out_x<682 && out_y>=48 && out_y<448)active_checked=active_checked+1;
                else border_checked=border_checked+1;
            end else begin expected_rgb=0;padding_checked=padding_checked+1;end
            if(rgb!==expected_rgb)
                $fatal(1,"pixel mismatch PAL%0d at %0d,%0d got%h expected%h",expected_pal,out_x,out_y,rgb,expected_rgb);
            emitted=emitted+1;total_checked=total_checked+1;
        end
    end
    task sample(input integer sx,sy,input bit skip);
        reg [23:0] color;
        begin
            @(negedge clk);#1;x=10'(sx);y=9'(sy);color=native_color(sx,sy);
            {r,g,b}=color;pixel_ce=!skip;
            @(negedge clk);#1;pixel_ce=0;
            repeat(2)@(negedge clk);
        end
    endtask
    task rows(input integer first,last,fault);
        begin
            for(integer sy=first;sy<=last;sy=sy+1)
                for(integer sx=0;sx<912;sx=sx+1)
                    sample(sx,sy,(fault==1 && sy==260 && sx==160) || (fault==2 && sy==260));
        end
    endtask
    task field(input bit mode,input integer fault);
        begin
            pal=mode;
            if(mode)begin rows(232,511,fault);rows(200,231,fault);end
            else begin rows(494,511,fault);rows(250,493,fault);end
            repeat(1600)@(negedge clk);
        end
    endtask
    task valid_field(input bit mode,input integer errors);
        begin
            checking=1;expected_pal=mode;emitted=0;
            field(mode,0);
            if(emitted!=FRAME_SAMPLES || malformed!=errors)
                $fatal(1,"PAL%0d incomplete/invalid field samples%0d errors%0d expected%0d",mode,emitted,malformed,errors);
            checking=0;
        end
    endtask
    task reset_state;
        begin
            checking=0;pixel_ce=0;ready=0;resetn=0;
            repeat(5)@(negedge clk);#1;resetn=1;ready=1;
        end
    endtask
    task stopped;
        begin
            repeat(8)@(negedge clk);
            if(out_x!=12'hfff || out_y!=12'hfff)$fatal(1,"aborted field continued emitting");
        end
    endtask
    initial begin
        reset_state();
        valid_field(0,0);valid_field(1,0);valid_field(0,0);
        $display("PASS IIgs border raster: NTSC/PAL 724x496, all640x200 active pixels, 42px/24row native line colors, row duplication, 44 zero stride samples");

        // One missing pixel or an entire missing line must invalidate the
        // field rather than shift subsequent rows into a publishable frame.
        reset_state();field(0,1);sample(0,494,0);stopped();
        if(malformed!=1)$fatal(1,"missing pixel was not rejected");
        reset_state();field(1,2);sample(0,232,0);stopped();
        if(malformed!=1)$fatal(1,"missing line was not rejected");
        $display("PASS IIgs malformed raster: missing pixel and missing line reject complete fields");

        // Lost readiness and reset invalidate both queued line banks and
        // require a fresh field boundary before any data can be emitted.
        reset_state();pal=0;rows(494,497,0);ready=0;stopped();
        ready=1;rows(498,499,0);stopped();
        valid_field(0,0);
        reset_state();pal=1;rows(232,235,0);resetn=0;stopped();
        resetn=1;rows(236,237,0);stopped();
        valid_field(1,0);
        $display("PASS IIgs raster abort: readiness/reset discard queued pixels and recover at field start");

        // A format change cannot splice NTSC's wrapped top border together
        // with PAL's contiguous one; each direction restarts independently.
        reset_state();pal=0;rows(494,497,0);pal=1;stopped();
        valid_field(1,1);
        pal=1;rows(232,235,0);pal=0;stopped();
        valid_field(0,2);
        $display("PASS IIgs format changes: NTSC to PAL and PAL to NTSC abort/restart without mixed rows");
        if(frames!=7 || total_checked!=7*FRAME_SAMPLES || active_checked!=7*256000 ||
           border_checked!=7*103104 || padding_checked!=7*21824 || image_checked!=7*359104)
            $fatal(1,"coverage totals frames%0d samples%0d active%0d border%0d padding%0d",frames,total_checked,active_checked,border_checked,padding_checked);
        $display("PASS IIgs native video: %0d RGB565 samples, %0d active pixels, %0d border pixels, %0d stride padding",total_checked,active_checked,border_checked,padding_checked);
        $finish;
    end
    initial begin #150000000;$fatal(1,"IIgs video timeout");end
endmodule
