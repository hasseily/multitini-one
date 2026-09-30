// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_border_overlay;
    parameter bit DYNAMIC_DISPLAY=0;
    localparam integer HA=DYNAMIC_DISPLAY ? 1024 : 1920;
    localparam integer VA=DYNAMIC_DISPLAY ? 768 : 1080;
    localparam integer HT=DYNAMIC_DISPLAY ? 1344 : 2200;
    localparam integer VT=DYNAMIC_DISPLAY ? 806 : 1125;
    reg clk=0, resetn=0;
    always #5 clk=~clk;
    reg [11:0] scan_x=0, scan_y=0;
    reg [12:0] config_word=13'h670;
    reg [11:0] vx=555,vy=0,vw=810,vh=1080;
    wire opaque;
    wire [15:0] rgb;
    integer checks=0, lit=0;
    multitini_border_overlay #(.DYNAMIC_DISPLAY(DYNAMIC_DISPLAY)) dut(
        .display_h_active(12'(HA)),.display_h_total(12'(HT)),
        .display_v_active(12'(VA)),.display_v_total(12'(VT)),
        .pixel_clk(clk),.resetn(resetn),.scan_x(scan_x),.scan_y(scan_y),
        .frame_config(config_word),.viewport_x(vx),.viewport_y(vy),
        .viewport_width(vw),.viewport_height(vh),
        .pixel_opaque(opaque),.pixel_rgb565(rgb));

    function automatic [23:0] color(input integer index);
        case(index)
        0:color=24'h30343b; 1:color=24'h424d88; 2:color=24'h4b7455;
        3:color=24'h497c80; 4:color=24'h8c5050; 5:color=24'h815a86;
        6:color=24'h8b7150; 7:color=24'h9b9da2; 8:color=24'h575c65;
        9:color=24'h758ec2; 10:color=24'h86ab83; 11:color=24'h7cacb0;
        12:color=24'hba8181; 13:color=24'hae87b3; 14:color=24'hbbb184;
        15:color=24'hc7c8ca;
        endcase
    endfunction
    function automatic integer perimeter(input integer x,y,l,t,r,b);
        perimeter=((x==l || x==r) && y>=t && y<=b) ||
                  ((y==t || y==b) && x>=l && x<=r);
    endfunction
    // Reference builds explicit rectangles, independently of the DUT's
    // signed-distance pipeline. Return opaque + RGB565.
    function automatic [16:0] expected(input integer x,y);
        integer pad,l,r,t,b,style,layer,shade;
        reg [23:0] c;
        begin
            expected=0;
            pad=config_word[12:8];
            if(pad<1) pad=1;
            if(pad>20) pad=20;
            l=vx-pad-1; r=vx+vw+pad; t=vy-pad-1; b=vy+vh+pad;
            style=config_word[3:1]; shade=-1;
            if(style==1) begin
                if(perimeter(x,y,l,t,r,b) || perimeter(x,y,l-3,t-3,r+3,b+3)) shade=0;
            end else if(style==2) begin
                if(perimeter(x,y,l,t,r,b) &&
                   (((y==t || y==b) && (x-l<24 || r-x<24)) ||
                    ((x==l || x==r) && (y-t<24 || b-y<24)))) shade=0;
            end else if(style==3) begin
                if(perimeter(x,y,l,t,r,b) &&
                   (((y==t || y==b) ? (l-x)&8 : (t-y)&8)==0)) shade=0;
            end else if(style==4) begin
                for(layer=0;layer<4;layer=layer+1)
                    if(perimeter(x,y,l-layer,t-layer,r+layer,b+layer)) shade=layer;
            end else if(perimeter(x,y,l,t,r,b)) shade=0;
            if(config_word[0] && vw!=0 && vh!=0 && x<HA && y<VA && shade>=0) begin
                c=color(config_word[7:4]);
                c={c[23:16]>>shade,c[15:8]>>shade,c[7:0]>>shade};
                expected={1'b1,c[23:19],c[15:10],c[7:3]};
            end
        end
    endfunction
    task automatic check_pixel(input integer x,y);
        reg [16:0] wanted;
        begin
            if(x>=0 && x<HT && y>=0 && y<VT) begin
                @(negedge clk);
                scan_x=x>=3 ? x-3 : (HT-3)+x;
                scan_y=x>=3 ? y : (y==0 ? (VT-1) : y-1);
                repeat(3) @(posedge clk);
                #1;
                wanted=expected(x,y);
                if(opaque !== wanted[16] || (opaque && rgb !== wanted[15:0]))
                    $fatal(1,"Border (%0d,%0d) cfg=%h view=%0d,%0d,%0d,%0d got=%b:%h expected=%h",
                           x,y,config_word,vx,vy,vw,vh,opaque,rgb,wanted);
                checks=checks+1;
                if(opaque) lit=lit+1;
            end
        end
    endtask

    integer geometry,style,palette_index,padding_index,pad,d,s,x,y,fd;
    reg [16:0] wanted;
    reg [23:0] out_rgb;
    string capture;
    initial begin
        repeat(5) @(posedge clk);
        @(negedge clk); resetn=1;
        // Full-sized games, scaled windows, and a viewport touching all edges.
        for(geometry=0;geometry<6;geometry=geometry+1) begin
            case(geometry)
            0:begin vx=555;vy=0;vw=810;vh=VA;end
            1:begin vx=196;vy=0;vw=1528;vh=VA;end
            2:begin vx=152;vy=40;vw=1616;vh=1000;end
            3:begin vx=480;vy=108;vw=960;vh=864;end
            4:begin vx=825;vy=360;vw=270;vh=360;end
            5:begin vx=0;vy=0;vw=HA;vh=VA;end
            endcase
            for(style=0;style<5;style=style+1)
            for(palette_index=0;palette_index<16;palette_index=palette_index+1)
            for(padding_index=0;padding_index<3;padding_index=padding_index+1) begin
                pad=padding_index==0 ? 1 : padding_index==1 ? 6 : 20;
                config_word=1|(style<<1)|(palette_index<<4)|(pad<<8);
                for(d=-4;d<=4;d=d+1) begin
                    // Edges, gaps, style thickness and each clipped endpoint.
                    check_pixel(vx-pad-1+d,0);
                    check_pixel(vx-pad-1+d,(VA-1));
                    check_pixel(vx+vw+pad+d,540);
                    check_pixel(960,vy-pad-1+d);
                    check_pixel(960,vy+vh+pad+d);
                    for(s=0;s<5;s=s+1) begin
                        check_pixel(vx-pad-1+d,vy-pad-1+s*8);
                        check_pixel(vx-pad-1+s*8,vy-pad-1+d);
                        check_pixel(vx+vw+pad+d,vy+vh+pad-s*8);
                        check_pixel(vx+vw+pad-s*8,vy+vh+pad+d);
                    end
                end
                check_pixel(vx,vy); check_pixel(vx+vw-1,vy+vh-1);
                check_pixel(HA,540); check_pixel(960,VA);
            end
        end
        // Disabled and empty-window guards.
        vx=480;vy=108;vw=960;vh=864;config_word=13'h674;
        check_pixel(473,108); check_pixel(960,101);
        vw=0;config_word=13'h679;check_pixel(473,108);

        // One continuous VAp timing raster checks the three-stage lookahead
        // including line/frame boundaries, and optionally emits actual pixels.
        vx=555;vy=0;vw=810;vh=VA;config_word=13'h699;
        fd=0;
        if($value$plusargs("capture=%s",capture)) begin
            fd=$fopen(capture,"wb");
            if(!fd) $fatal(1,"Cannot open capture");
            $fwrite(fd,"P6\nHA VA\n255\n");
        end
        // Prime with the last clocks of the preceding frame.
        for(x=(HT-4);x<HT;x=x+1) begin
            @(negedge clk);scan_x=x;scan_y=(VT-1);
        end
        for(y=0;y<VT;y=y+1)
        for(x=0;x<HT;x=x+1) begin
            @(posedge clk); #1;
            wanted=expected(x,y);
            if(opaque !== wanted[16] || (opaque && rgb !== wanted[15:0]))
                $fatal(1,"Raster alignment (%0d,%0d) got=%b:%h expected=%h",x,y,opaque,rgb,wanted);
            checks=checks+1;
            if(fd && x<HA && y<VA) begin
                // Original calibration pattern inside the game rectangle.
                out_rgb=24'h000000;
                if(x>=vx && x<vx+vw && y>=vy && y<vy+vh)
                    out_rgb=(x==vx || x==vx+vw-1 || y==0 || y==(VA-1)) ?
                            24'h707070 : ((x%90==15 || y%90==15) ? 24'h233025 : 24'h060907);
                if(opaque) out_rgb={{rgb[15:11],rgb[15:13]},
                                   {rgb[10:5],rgb[10:9]}, {rgb[4:0],rgb[4:2]}};
                $fwrite(fd,"%c%c%c",out_rgb[23:16],out_rgb[15:8],out_rgb[7:0]);
            end
            @(negedge clk);scan_x=x;scan_y=y;
        end
        if(fd) $fclose(fd);
        if(lit<1000) $fatal(1,"Insufficient lit pixel coverage");
        $display("PASS border overlay: %0d geometry/color/pipeline checks, %0d lit samples",checks,lit);
        $finish;
    end
endmodule
