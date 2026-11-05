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
                4,6:numerator=(m==0 || m==5) ? 4 : 3;
                5,7:numerator=(m==0 || m==3 || m==5) && WIDTH*2<=aw && HEIGHT*2<=ah ? 2 : 1;
                8:begin
                    numerator=aw/WIDTH<ah/HEIGHT ? aw/WIDTH : ah/HEIGHT;
                    if(numerator>4)numerator=4;
                end
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
            if(MACHINE==5 && WIDTH==724 && HEIGHT==496 && m<6 &&
               (vw>ha || vh>va || vx+vw>ha || vy+vh>va))
                $fatal(1,"IIgs Auto clipped the native border in mode %0d",m);
            if(m<6 && WIDTH>0 && WIDTH<=724 && HEIGHT>0 && HEIGHT<=800)begin
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

// The direct tables above are checked against independent numeric geometry.
// This second proof checks every bit of the new selector's public bundle,
// including timing, polarity, crop seeds and unsupported multiplier values.
module display_geometry_pair_case #(
    parameter integer MACHINE=7,WIDE_WIDTH=1280,WIDE_HEIGHT=512
)(output reg done=0);
    reg [3:0] mode=15;reg [2:0] multiplier=7;reg [1:0] source_profile=3;
    wire [181:0] selected,reference[0:2];
    wire [11:0] ha,hf,hs,ht,va,vf,vs,vt,vx,vy,vw,vh;
    wire hp,vp;wire [3:0] sn,sd,px,py;wire [9:0] sx,sy;
    multitini_display_geometry_pair #(.ENABLE_MULTIPLIER(1),.MACHINE(MACHINE),
        .WIDTH(640),.HEIGHT(512),.ALT_WIDTH(724),.ALT_HEIGHT(566),
        .WIDE_WIDTH(WIDE_WIDTH),.WIDE_HEIGHT(WIDE_HEIGHT)) dut(
        .mode(mode),.multiplier(multiplier),.source_profile(source_profile),
        .h_active(ha),.h_front(hf),.h_sync(hs),.h_total(ht),
        .v_active(va),.v_front(vf),.v_sync(vs),.v_total(vt),.h_positive(hp),.v_positive(vp),
        .scale_num(sn),.scale_den(sd),.view_x(vx),.view_y(vy),.view_width(vw),.view_height(vh),
        .source_x0(sx),.source_y0(sy),.phase_x0(px),.phase_y0(py));
    assign selected={ha,hf,hs,ht,va,vf,vs,vt,hp,vp,sn,sd,vx,vy,vw,vh,sx,sy,px,py};
    for(genvar p=0;p<3;p=p+1)begin:refs
        wire [11:0] rha,rhf,rhs,rht,rva,rvf,rvs,rvt,rvx,rvy,rvw,rvh;
        wire rhp,rvp;wire [3:0] rsn,rsd,rpx,rpy;wire [9:0] rsx,rsy;
        localparam integer RW=p==2 && MACHINE==7 ? WIDE_WIDTH : p==1 ? 724:640;
        localparam integer RH=p==2 && MACHINE==7 ? WIDE_HEIGHT : p==1 ? 566:512;
        multitini_display_geometry #(.ENABLE_MULTIPLIER(1),.MACHINE(MACHINE),
            .CLIP_OVERSIZE_CANVAS(p==2 && MACHINE==7),.WIDTH(RW),.HEIGHT(RH)) fixed_i(
            .mode(mode),.multiplier(multiplier),
            .h_active(rha),.h_front(rhf),.h_sync(rhs),.h_total(rht),
            .v_active(rva),.v_front(rvf),.v_sync(rvs),.v_total(rvt),.h_positive(rhp),.v_positive(rvp),
            .scale_num(rsn),.scale_den(rsd),.view_x(rvx),.view_y(rvy),.view_width(rvw),.view_height(rvh),
            .source_x0(rsx),.source_y0(rsy),.phase_x0(rpx),.phase_y0(rpy));
        assign reference[p]={rha,rhf,rhs,rht,rva,rvf,rvs,rvt,rhp,rvp,rsn,rsd,rvx,rvy,rvw,rvh,rsx,rsy,rpx,rpy};
    end
    initial begin
        for(integer m=0;m<16;m=m+1)for(integer scale=0;scale<8;scale=scale+1)for(integer p=0;p<4;p=p+1)begin
            mode=4'(m);multiplier=3'(scale);source_profile=2'(p);#1;
            if(selected!==reference[p==3 || (p==2 && (MACHINE!=7 || (WIDE_WIDTH==640 && WIDE_HEIGHT==512))) ? 0:p])
                $fatal(1,"geometry profile bundle mismatch machine%0d mode%0d multiplier%0d profile%0d",MACHINE,m,scale,p);
            if(MACHINE==7 && m==0 && scale==0)begin
                if(p==0 && {sn,sd,vx,vy,vw,vh}!=={4'd2,4'd1,12'd320,12'd28,12'd1280,12'd1024})
                    $fatal(1,"Amiga standard Auto2 changed:%0d/%0d %0d,%0d %0dx%0d",sn,sd,vx,vy,vw,vh);
                if(p==1 && {sn,sd,vx,vy,vw,vh}!=={4'd1,4'd1,12'd598,12'd257,12'd724,12'd566})
                    $fatal(1,"Amiga maximum Auto1 changed");
            end
        end
        $display("PASS geometry profiles:512 selections,all182bits,machine%0d,wide%0dx%0d",MACHINE,WIDE_WIDTH,WIDE_HEIGHT);done=1;
    end
endmodule

// Independent full-integer canvas oracle. This table intentionally does not
// instantiate a second geometry DUT or call its mode/geometry functions.
module display_geometry_wide_case(output reg done=0);
    reg [3:0] mode=0;reg [2:0] multiplier=0;
    wire [11:0] ha,va,vx,vy,vw,vh;
    wire [3:0] sn,sd,px,py;wire [9:0] sx,sy;
    multitini_display_geometry #(.ENABLE_MULTIPLIER(1),.MACHINE(7),
        .WIDTH(1280),.HEIGHT(512),.CLIP_OVERSIZE_CANVAS(1)) dut(
        .mode(mode),.multiplier(multiplier),.h_active(ha),.v_active(va),
        .h_front(),.h_sync(),.h_total(),.v_front(),.v_sync(),.v_total(),
        .h_positive(),.v_positive(),.scale_num(sn),.scale_den(sd),
        .view_x(vx),.view_y(vy),.view_width(vw),.view_height(vh),
        .source_x0(sx),.source_y0(sy),.phase_x0(px),.phase_y0(py));
    integer aw,ah,n,full_w,full_h,left,top,x,y,w,h,crop_x,crop_y,expected,seeded;
    integer checked_pixels=0;
    initial begin
        for(integer m=0;m<6;m=m+1)begin
            case(m)
                0:begin aw=1920;ah=1080;end
                1:begin aw=1024;ah=768;end
                2:begin aw=1200;ah=800;end
                3:begin aw=1280;ah=1024;end
                4:begin aw=1360;ah=768;end
                5:begin aw=1680;ah=1050;end
            endcase
            for(integer size=0;size<5;size=size+1)begin
                // Auto can never fit 2560-wide, so all six outputs use1x.
                n=size==0 ? 1:size;full_w=1280*n;full_h=512*n;
                left=(aw-full_w)>>>1;top=(ah-full_h)>>>1;
                x=left<0 ? 0:left;y=top<0 ? 0:top;
                w=full_w>aw ? aw:full_w;h=full_h>ah ? ah:full_h;
                crop_x=left<0 ? -left:0;crop_y=top<0 ? -top:0;
                mode=4'(m);multiplier=3'(size);#1;
                if({ha,va}!={12'(aw),12'(ah)} || {sn,sd}!={4'(n),4'd1} ||
                   {vx,vy,vw,vh}!={12'(x),12'(y),12'(w),12'(h)} ||
                   sx!=crop_x/n || sy!=crop_y/n || px!=crop_x%n || py!=crop_y%n)
                    $fatal(1,"wide canvas/origin/seed mismatch mode%0d size%0d full%0dx%0d actual%0d,%0d %0dx%0d seed%0d/%0d,%0d/%0d",
                        m,size,full_w,full_h,vx,vy,vw,vh,sx,px,sy,py);
                if(sx>512 || vx+vw>ha || vy+vh>va)
                    $fatal(1,"wide visible bounds/crop seed exceed public fields");
                // Compare every visible x/y seed step with direct sampling
                // from the original signed full-canvas origin, not clipped0.
                for(integer dx=0;dx<w;dx=dx+1)begin
                    expected=(x+dx-left)/n;seeded=sx+(px+dx)/n;
                    if(seeded!=expected || expected<0 || expected>=1280)
                        $fatal(1,"wide x crop shifted/wrapped mode%0d size%0d pixel%0d",m,size,dx);
                    checked_pixels=checked_pixels+1;
                end
                for(integer dy=0;dy<h;dy=dy+1)begin
                    expected=(y+dy-top)/n;seeded=sy+(py+dy)/n;
                    if(seeded!=expected || expected<0 || expected>=512)
                        $fatal(1,"wide y crop shifted/wrapped mode%0d size%0d pixel%0d",m,size,dy);
                    checked_pixels=checked_pixels+1;
                end
                if(m==1 && size==4 && (sx!=512 || vw!=1024 || vx!=0))
                    $fatal(1,"5120-wide canvas was truncated before centering");
            end
        end
        $display("PASS wide geometry:30 full-canvas cases,all6HDMI/Auto+1..4,%0d pixel-seed checks",checked_pixels);done=1;
    end
endmodule

// Virtual recapture changes only the wide Auto ROI on1024/1200 outputs.
// HDMI timing and all other profile/multiplier bundles stay bit-identical.
module display_geometry_virtual_case(output reg done=0);
    reg [3:0] mode=15;reg [2:0] multiplier=7;reg [1:0] profile=3;
    wire [181:0] bundles[0:1];
    for(genvar v=0;v<2;v=v+1)begin:versions
        wire [11:0] ha,hf,hs,ht,va,vf,vs,vt,vx,vy,vw,vh;
        wire hp,vp;wire [3:0] sn,sd,px,py;wire [9:0] sx,sy;
        multitini_display_geometry_pair #(.ENABLE_MULTIPLIER(1),.MACHINE(7),
            .WIDTH(640),.HEIGHT(512),.ALT_WIDTH(724),.ALT_HEIGHT(566),
            .WIDE_WIDTH(1280),.WIDE_HEIGHT(512),.VIRTUAL_CAPTURE(v)) dut(
            .mode(mode),.multiplier(multiplier),.source_profile(profile),
            .h_active(ha),.h_front(hf),.h_sync(hs),.h_total(ht),
            .v_active(va),.v_front(vf),.v_sync(vs),.v_total(vt),.h_positive(hp),.v_positive(vp),
            .scale_num(sn),.scale_den(sd),.view_x(vx),.view_y(vy),.view_width(vw),.view_height(vh),
            .source_x0(sx),.source_y0(sy),.phase_x0(px),.phase_y0(py));
        assign bundles[v]={ha,hf,hs,ht,va,vf,vs,vt,hp,vp,sn,sd,vx,vy,vw,vh,sx,sy,px,py};
    end
    integer aw,ah,htotal,auto_x,unchanged=0;
    initial begin
        for(integer m=0;m<6;m=m+1)begin
            case(m)
                0:begin aw=1920;ah=1080;htotal=2200;end
                1:begin aw=1024;ah=768;htotal=1344;end
                2:begin aw=1200;ah=800;htotal=1360;end
                3:begin aw=1280;ah=1024;htotal=1688;end
                4:begin aw=1360;ah=768;htotal=1792;end
                5:begin aw=1680;ah=1050;htotal=1840;end
            endcase
            for(integer size=0;size<5;size=size+1)for(integer p=0;p<4;p=p+1)begin
                mode=4'(m);multiplier=3'(size);profile=2'(p);#1;
                if(p==2 && size==0)begin
                    auto_x=aw>1280 ? (aw-1280)/2:0;
                    if({versions[1].ha,versions[1].va,versions[1].ht}!=={12'(aw),12'(ah),12'(htotal)} ||
                       {versions[1].vx,versions[1].vy,versions[1].vw,versions[1].vh}!==
                           {12'(auto_x),12'((ah-512)/2),12'd1280,12'd512} ||
                       {versions[1].sn,versions[1].sd,versions[1].sx,versions[1].sy,versions[1].px,versions[1].py}!==
                           {4'd1,4'd1,10'd0,10'd0,4'd0,4'd0} ||
                       versions[1].vx+versions[1].vw>versions[1].ht)
                        $fatal(1,"Virtual capture clipped/shifted1280Auto or altered physical timing mode%0d timing%0d/%0d/%0d view%0d,%0d %0dx%0d scale%0d/%0d seeds%0d/%0d,%0d/%0d",
                            m,versions[1].ha,versions[1].va,versions[1].ht,versions[1].vx,versions[1].vy,versions[1].vw,versions[1].vh,
                            versions[1].sn,versions[1].sd,versions[1].sx,versions[1].px,versions[1].sy,versions[1].py);
                end
                if(!(p==2 && size==0 && (m==1 || m==2)))begin
                    if(bundles[0]!==bundles[1])$fatal(1,"Virtual capture changed physical profile%0d mode%0d size%0d",p,m,size);
                    unchanged=unchanged+1;
                end else if(bundles[0]===bundles[1])
                    $fatal(1,"Virtual capture failed to restore full wide image mode%0d",m);
            end
        end
        if(unchanged!=118)$fatal(1,"Virtual capture legacy bundle coverage changed");
        $display("PASS virtual capture:6 fullwideAuto modes,120 selections,118 bit-identical physical bundles,fullROI fitsHtotal");done=1;
    end
endmodule

module tb_display_geometry;
    localparam integer MACHINES=8, DIMENSIONS=18;
    wire [MACHINES*DIMENSIONS-1:0] done;
    wire pair_done,other_pair_done,default_pair_done,wide_done,virtual_done,spectrum_done;
    display_geometry_pair_case pair_check(.done(pair_done));
    display_geometry_pair_case #(.MACHINE(5)) other_pair_check(.done(other_pair_done));
    display_geometry_pair_case #(.WIDE_WIDTH(640),.WIDE_HEIGHT(512)) default_pair_check(.done(default_pair_done));
    display_geometry_wide_case wide_check(.done(wide_done));
    display_geometry_virtual_case virtual_check(.done(virtual_done));
    display_geometry_case #(.MACHINE(8),.WIDTH(344),.HEIGHT(280)) spectrum_check(.done(spectrum_done));
    for(genvar machine=0;machine<MACHINES;machine=machine+1)begin:machines
        for(genvar pair=0;pair<DIMENSIONS;pair=pair+1)begin:dimensions
            // Actual shell/default geometries, then zero, odd-size, subtract
            // underflow, multiply overflow, and input truncation stress cases.
            localparam integer W=pair==0 ? 382 : pair==1 ? 404 : pair==2 ? 160 :
                pair==3 ? 540 : pair==4 ? 320 : pair==5 ? 0 : pair==6 ? 1 :
                pair==7 ? 541 : pair==8 ? 1919 : pair==9 ? 1365 :
                pair==10 ? 4095 : pair==11 ? 4096 : pair==12 ? 8191 : pair==13 ? 640 : pair==14 ? 256 : pair==15 ? 724 : pair==16 ? 640 : 724;
            localparam integer H=pair==0 ? 270 : pair==1 ? 250 : pair==2 ? 144 :
                pair==3 ? 720 : pair==4 ? 240 : pair==5 ? 0 : pair==6 ? 1 :
                pair==7 ? 721 : pair==8 ? 1079 : pair==9 ? 2731 :
                pair==10 ? 4095 : pair==11 ? 4097 : pair==12 ? 8193 : pair==13 ? 400 : pair==14 ? 240 : pair==15 ? 496 : pair==16 ? 512 : 566;
            display_geometry_case #(.MACHINE(machine),.WIDTH(W),.HEIGHT(H)) check_case(
                .done(done[machine*DIMENSIONS+pair]));
        end
    end
    initial begin
        wait(&done && pair_done && other_pair_done && default_pair_done && wide_done && virtual_done && spectrum_done);
        $display("PASS display geometry: %0d cases; 16 mode IDs, 8 machines, 18 dimension pairs",MACHINES*DIMENSIONS*16);
        $display("PASS Spectrum geometry: 344x280 raster, 16 mode IDs, centered Auto and manual crop");
        $finish;
    end
    initial begin #1000;$fatal(1,"display geometry timeout");end
endmodule
