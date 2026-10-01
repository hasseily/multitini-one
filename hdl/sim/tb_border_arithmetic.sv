// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

module tb_border_arithmetic;
    reg pixel_clk=0;
    always #5 pixel_clk=~pixel_clk;
    reg resetn=0;
    reg [11:0] display_h_active=4095,display_h_total=0;
    reg [11:0] display_v_active=4095,display_v_total=0;
    reg [11:0] scan_x=0,scan_y=0;
    reg [12:0] frame_config=0;
    reg [11:0] viewport_x=0,viewport_y=0,viewport_width=0,viewport_height=0;
    wire pixel_opaque;
    wire [15:0] pixel_rgb565;
    multitini_border_overlay #(.DYNAMIC_DISPLAY(1)) dut(.*);

    // Integer reference deliberately uses unbounded coordinate arithmetic,
    // explicit unsigned-underflow guards, and explicit signed-14 wrapping.
    function automatic integer signed14(input integer value);
        integer residue;
        begin
            residue=value & 16383;
            signed14=residue>=8192 ? residue-16384 : residue;
        end
    endfunction
    localparam [383:0] COLORS={24'hC7C8CA,24'hBBB184,24'hAE87B3,24'hBA8181,
        24'h7CACB0,24'h86AB83,24'h758EC2,24'h575C65,
        24'h9B9DA2,24'h8B7150,24'h815A86,24'h8C5050,
        24'h497C80,24'h4B7455,24'h424D88,24'h30343B};
    integer expected_x,expected_y,expected_dl,expected_dr,expected_dt,expected_db;
    integer pad,left_edge,right_edge,top_edge,bottom_edge,distance,shade;
    reg corner,dash,draw,active;
    reg [23:0] color;
    reg [7:0] red,green,blue;
    reg [16:0] expected_pixel,pipe0=0,pipe1=0,pipe2=0;
    integer checks=0,wrapped_edges=0;
    task automatic reference_pixel;
        begin
            expected_x=(integer'(scan_x)+3)&4095;
            expected_y=scan_y;
            if(display_h_total>=3 && integer'(scan_x)>=integer'(display_h_total)-3) begin
                expected_x=(integer'(scan_x)-integer'(display_h_total)+3)&4095;
                expected_y=(display_v_total!=0 &&
                    integer'(scan_y)==integer'(display_v_total)-1) ? 0 :
                    (integer'(scan_y)+1)&4095;
            end
            pad=frame_config[12:8];
            if(pad<1) pad=1;
            if(pad>20) pad=20;
            left_edge=signed14(integer'(viewport_x)-pad-1);
            right_edge=signed14(integer'(viewport_x)+integer'(viewport_width)+pad);
            top_edge=signed14(integer'(viewport_y)-pad-1);
            bottom_edge=signed14(integer'(viewport_y)+integer'(viewport_height)+pad);
            if(right_edge<0 || bottom_edge<0) wrapped_edges=wrapped_edges+1;
            expected_dl=signed14(left_edge-expected_x);
            expected_dr=signed14(expected_x-right_edge);
            expected_dt=signed14(top_edge-expected_y);
            expected_db=signed14(expected_y-bottom_edge);
            distance=expected_dl;
            if(expected_dr>distance)distance=expected_dr;
            if(expected_dt>distance)distance=expected_dt;
            if(expected_db>distance)distance=expected_db;
            corner=((expected_dt==0 || expected_db==0) &&
                (expected_dl>=-23 || expected_dr>=-23)) ||
                ((expected_dl==0 || expected_dr==0) &&
                (expected_dt>=-23 || expected_db>=-23));
            dash=((expected_dt==0 || expected_db==0) ?
                (expected_dl & 8) : (expected_dt & 8))==0;
            shade=0;
            case(frame_config[3:1])
                1:draw=distance==0 || distance==3;
                2:draw=distance==0 && corner;
                3:draw=distance==0 && dash;
                4:begin draw=distance>=0 && distance<=3;
                    if(distance>=1 && distance<=3)shade=distance;end
                default:draw=distance==0;
            endcase
            active=frame_config[0] && viewport_width!=0 && viewport_height!=0 &&
                expected_x<display_h_active && expected_y<display_v_active;
            color=COLORS[frame_config[7:4]*24+:24];
            red=color[23:16]>>shade;green=color[15:8]>>shade;blue=color[7:0]>>shade;
            expected_pixel={active && draw,red[7:3],green[7:2],blue[7:3]};
        end
    endtask
    task automatic check_current;
        begin
            reference_pixel;
            pipe2=pipe1;pipe1=pipe0;pipe0=expected_pixel;
            #1;
            if(dut.fetch_x!==12'(expected_x) || dut.fetch_y!==12'(expected_y))
                $fatal(1,"Lookahead total=%0d,%0d scan=%0d,%0d got=%0d,%0d expected=%0d,%0d",
                    display_h_total,display_v_total,scan_x,scan_y,
                    dut.fetch_x,dut.fetch_y,expected_x,expected_y);
            @(posedge pixel_clk);#1;
            if($signed(dut.dl_q)!==expected_dl || $signed(dut.dr_q)!==expected_dr ||
               $signed(dut.dt_q)!==expected_dt || $signed(dut.db_q)!==expected_db)
                $fatal(1,"Signed distance mismatch at case %0d",checks);
            if({pixel_opaque,pixel_rgb565}!==pipe2)
                $fatal(1,"Three-stage output mismatch at case %0d got=%h expected=%h",
                    checks,{pixel_opaque,pixel_rgb565},pipe2);
            checks=checks+1;
        end
    endtask
    task automatic set_case(input integer total,x,index);
        begin
            @(negedge pixel_clk);
            resetn=1;
            display_h_total=total;scan_x=x;
            display_v_total=index&4095;
            case(index%6)
                0:scan_y=0;
                1:scan_y=display_v_total-1;
                2:scan_y=display_v_total;
                3:scan_y=display_v_total+1;
                4:scan_y=4094;
                5:scan_y=4095;
            endcase
            display_h_active=(index%3)==0 ? 0 : (index%3)==1 ? 4095 : 1920;
            display_v_active=(index%3)==0 ? 4095 : (index%3)==1 ? 0 : 1080;
            frame_config=index&8191;
            case(index%10)
                0:begin viewport_x=0;viewport_y=0;viewport_width=0;viewport_height=0;end
                1:begin viewport_x=4095;viewport_y=4095;viewport_width=4095;viewport_height=4095;end
                2:begin viewport_x=4095;viewport_y=4095;viewport_width=0;viewport_height=0;end
                3:begin viewport_x=4080;viewport_y=4080;viewport_width=4095;viewport_height=4095;end
                4:begin viewport_x=4095;viewport_y=4095;viewport_width=4080;viewport_height=4080;end
                5:begin viewport_x=0;viewport_y=0;viewport_width=4095;viewport_height=4095;end
                6:begin viewport_x=1;viewport_y=1;viewport_width=1;viewport_height=1;end
                7:begin viewport_x=2048;viewport_y=2048;viewport_width=2048;viewport_height=2048;end
                8:begin viewport_x=555;viewport_y=0;viewport_width=810;viewport_height=1080;end
                9:begin viewport_x=index;viewport_y=index*17;viewport_width=index*31;viewport_height=index*43;end
            endcase
            check_current;
        end
    endtask
    integer total,lane,x,index=0;
    initial begin
        repeat(3)@(negedge pixel_clk);
        // Every 12-bit total, around its wrap threshold and scan endpoints.
        for(total=0;total<4096;total=total+1)
            for(lane=0;lane<16;lane=lane+1)begin
                case(lane)
                    0,1,2,3:x=lane;
                    4,5,6,7,8,9,10:x=(total+lane-8)&4095;
                    11,12,13:x=4082+lane;
                    14:x=2047;
                    15:x=2048;
                endcase
                set_case(total,x,index);index=index+1;
            end
        // Exhaust every scan x at both total underflow and upper boundaries.
        for(lane=0;lane<7;lane=lane+1)begin
            total=lane<5 ? lane : 4089+lane;
            for(x=0;x<4096;x=x+1)begin
                set_case(total,x,index);index=index+1;
            end
        end
        repeat(3)begin set_case(2200,0,index);index=index+1;end
        if(wrapped_edges<1000)$fatal(1,"Insufficient signed viewport overflow coverage");
        $display("PASS border arithmetic: %0d cases; all 4096 totals; %0d signed edge wraps; three-stage latency",checks,wrapped_edges);
        $finish;
    end
endmodule
