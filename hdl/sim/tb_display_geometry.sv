// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Independent numeric oracle for the geometry preceding the constant decode.
// Do not use video_mode() or the DUT's geometry function in this reference.
module display_geometry_case #(
    parameter integer MACHINE=0, WIDTH=382, HEIGHT=270
)(output reg done=0);
    reg [3:0] mode=15;
    reg [2:0] multiplier=0;
    wire [9:0] source_x0,source_y0;
    wire [3:0] phase_x0,phase_y0;
    wire [11:0] ha,hf,hs,ht,va,vf,vs,vt;
    wire hp,vp;
    wire [3:0] sn,sd;
    wire [11:0] vx,vy,vw,vh;
    multitini_display_geometry #(.ENABLE_MULTIPLIER(1'b1),.MACHINE(MACHINE),.WIDTH(WIDTH),.HEIGHT(HEIGHT)) dut(
        .mode(mode),.multiplier(multiplier),.h_active(ha),.h_front(hf),.h_sync(hs),.h_total(ht),
        .v_active(va),.v_front(vf),.v_sync(vs),.v_total(vt),
        .h_positive(hp),.v_positive(vp),.scale_num(sn),.scale_den(sd),
        .view_x(vx),.view_y(vy),.view_width(vw),.view_height(vh),
        .source_x0(source_x0),.source_y0(source_y0),.phase_x0(phase_x0),.phase_y0(phase_y0));

    integer aw,ah,front_h,sync_h,total_h,front_v,sync_v,total_v;
    integer polarity_h,polarity_v,numerator,denominator;
    integer width12,height12,x12,y12;
    integer manual_w,manual_h,manual_x,manual_y,crop_x,crop_y;
    reg [97:0] expected_timing;
    reg [55:0] expected_geometry;
    initial begin
        for(integer m=0;m<16;m=m+1)begin
            // Undefined IDs retain 1080p timing, but their original scale
            // follows the nonzero mode rules rather than mode 0's scale.
            aw=1920;ah=1080;front_h=88;sync_h=44;total_h=2200;
            front_v=4;sync_v=5;total_v=1125;polarity_h=1;polarity_v=1;
            case(m)
                1:begin aw=1024;ah=768;front_h=24;sync_h=136;total_h=1344;
                    front_v=3;sync_v=6;total_v=806;polarity_h=0;polarity_v=0;end
                2:begin aw=1200;ah=800;front_h=48;sync_h=32;total_h=1360;
                    front_v=3;sync_v=10;total_v=828;polarity_h=1;polarity_v=0;end
                3:begin aw=1280;ah=1024;front_h=48;sync_h=112;total_h=1688;
                    front_v=1;sync_v=3;total_v=1066;polarity_h=1;polarity_v=1;end
                4:begin aw=1360;ah=768;front_h=64;sync_h=112;total_h=1792;
                    front_v=3;sync_v=6;total_v=795;polarity_h=1;polarity_v=1;end
                5:begin aw=1680;ah=1050;front_h=48;sync_h=32;total_h=1840;
                    front_v=3;sync_v=6;total_v=1080;polarity_h=1;polarity_v=0;end
            endcase
            denominator=1;
            case(MACHINE)
                1:numerator=(m==1 || m==2 || m==4) ? 5 : 6;
                3:begin numerator=m==0 ? 3 : 1;denominator=m==0 ? 2 : 1;end
                4:numerator=(m==0 || m==5) ? 4 : 3;
                default:numerator=m==0 ? 4 : (m==1 || m==2 || m==4) ? 2 : 3;
            endcase
            // The old arithmetic first truncates the scaled dimensions to
            // 12 bits, then wraps centering subtraction at 12 bits before
            // shifting. Keep the oracle in integers with explicit modulo.
            width12=((WIDTH*numerator)/denominator)%4096;
            height12=((HEIGHT*numerator)/denominator)%4096;
            x12=((aw+4096-width12)%4096)/2;
            y12=((ah+4096-height12)%4096)/2;
            expected_timing={12'(aw),12'(front_h),12'(sync_h),12'(total_h),
                12'(ah),12'(front_v),12'(sync_v),12'(total_v),1'(polarity_h),1'(polarity_v)};
            expected_geometry={4'(numerator),4'(denominator),12'(x12),12'(y12),12'(width12),12'(height12)};
            mode=4'(m);
            multiplier=0;
            #1;
            if({ha,hf,hs,ht,va,vf,vs,vt,hp,vp} !== expected_timing)
                $fatal(1,"timing mismatch machine=%0d source=%0dx%0d mode=%0d got=%h expected=%h",
                    MACHINE,WIDTH,HEIGHT,m,{ha,hf,hs,ht,va,vf,vs,vt,hp,vp},expected_timing);
            if({sn,sd,vx,vy,vw,vh} !== expected_geometry)
                $fatal(1,"geometry mismatch machine=%0d source=%0dx%0d mode=%0d got=%0d/%0d %0d,%0d %0dx%0d expected=%0d/%0d %0d,%0d %0dx%0d",
                    MACHINE,WIDTH,HEIGHT,m,sn,sd,vx,vy,vw,vh,
                    numerator,denominator,x12,y12,width12,height12);
            if(source_x0!==0 || source_y0!==0 || phase_x0!==0 || phase_y0!==0)
                $fatal(1,"Auto source crop must stay zero");
            if(m<6 && WIDTH>0 && WIDTH<=640 && HEIGHT>0 && HEIGHT<=800)begin
                for(integer size=1;size<=4;size=size+1)begin
                    manual_w=WIDTH*size;manual_h=HEIGHT*size;
                    manual_x=(aw-manual_w)>>>1;manual_y=(ah-manual_h)>>>1;
                    crop_x=manual_x<0 ? -manual_x : 0;crop_y=manual_y<0 ? -manual_y : 0;
                    multiplier=3'(size);#1;
                    if({sn,sd,vx,vy,vw,vh}!=={4'(size),4'd1,12'(manual_x),12'(manual_y),12'(manual_w),12'(manual_h)} ||
                        source_x0!==10'(crop_x/size) || source_y0!==10'(crop_y/size) ||
                        phase_x0!==4'(crop_x%size) || phase_y0!==4'(crop_y%size))
                        $fatal(1,"manual scale/crop mismatch machine=%0d native=%0dx%0d mode=%0d size=%0d",MACHINE,WIDTH,HEIGHT,m,size);
                    if({ha,hf,hs,ht,va,vf,vs,vt,hp,vp}!==expected_timing)
                        $fatal(1,"multiplier changed HDMI timing");
                end
            end
        end
        done=1;
    end
endmodule

module tb_display_geometry;
    localparam integer MACHINES=5, DIMENSIONS=13;
    wire [MACHINES*DIMENSIONS-1:0] done;
    for(genvar machine=0;machine<MACHINES;machine=machine+1)begin:machines
        for(genvar pair=0;pair<DIMENSIONS;pair=pair+1)begin:dimensions
            // Actual shell/default geometries, then zero, odd-size, subtract
            // underflow, multiply overflow, and input truncation stress cases.
            localparam integer W=pair==0 ? 382 : pair==1 ? 404 : pair==2 ? 160 :
                pair==3 ? 540 : pair==4 ? 320 : pair==5 ? 0 : pair==6 ? 1 :
                pair==7 ? 541 : pair==8 ? 1919 : pair==9 ? 1365 :
                pair==10 ? 4095 : pair==11 ? 4096 : 8191;
            localparam integer H=pair==0 ? 270 : pair==1 ? 250 : pair==2 ? 144 :
                pair==3 ? 720 : pair==4 ? 240 : pair==5 ? 0 : pair==6 ? 1 :
                pair==7 ? 721 : pair==8 ? 1079 : pair==9 ? 2731 :
                pair==10 ? 4095 : pair==11 ? 4097 : 8193;
            display_geometry_case #(.MACHINE(machine),.WIDTH(W),.HEIGHT(H)) check_case(
                .done(done[machine*DIMENSIONS+pair]));
        end
    end
    initial begin
        wait(&done);
        $display("PASS display geometry: %0d cases; 16 mode IDs, 5 machines, 13 dimension pairs",MACHINES*DIMENSIONS*16);
        $finish;
    end
    initial begin #100;$fatal(1,"display geometry timeout");end
endmodule
