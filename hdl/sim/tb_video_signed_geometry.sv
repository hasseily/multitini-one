// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps

// Compare signed/cropped composition with an independent per-pixel oracle.
// Coordinates passed to effects retain the hidden full-image prefix.
module tb_video_signed_geometry;
    parameter integer SCALE_NUM=4;
    parameter integer ORIGIN_X=-5,ORIGIN_Y=-3,VIEW_WIDTH=74,VIEW_HEIGHT=18;
    localparam integer HA=64,VA=12,HT=70,VT=15;
    reg clk=0,control_clk=0,resetn=0,write=0,commit=0;
    always #3.367 clk=~clk;
    always #5 control_clk=~control_clk;
    (* ASYNC_REG="TRUE" *) reg [2:0] shared_reset;
    reg [2:0] shared_local;
    (* ASYNC_REG="TRUE" *) reg [1:0] shared_live;
    always @(posedge clk or negedge resetn)
        if(!resetn)shared_reset<=0;else shared_reset<={shared_reset[1:0],1'b1};
    always @(posedge clk)
        if(!shared_reset[2] || !resetn)shared_local<=0;else shared_local<={shared_local[1:0],1'b1};
    always @(posedge control_clk)
        if(!resetn)shared_live<=0;else shared_live<={shared_live[0],shared_local[2]};
    reg [9:0] address=0;
    reg [31:0] data=0,commit_sequence=0;
    wire busy,valid,rejected;
    wire [31:0] active_sequence;
    wire [11:0] x,y;
    wire [15:0] rgb_direct,rgb_effect;
    wire de_direct,hs_direct,vs_direct,de_effect,hs_effect,vs_effect;
    wire [15:0] machine_rgb=(x*97+y*341)^16'hb6ea;
    wire [15:0] left_rgb=machine_rgb^16'h54ac,right_rgb=machine_rgb^16'hb19a;
    wire left_half=2*((integer'(x)-ORIGIN_X)%SCALE_NUM)+1<SCALE_NUM;
    wire [15:0] neighbor=left_half ? left_rgb:right_rgb;
    wire [15:0] opposite=left_half ? right_rgb:left_rgb;
    // Menu rows/columns span the physical screen, independently of the image.
    wire ui_opaque=(x>=19 && x<=24) || y==8;
    wire border_opaque=x==2 || x==61 || y==10;
    reg checking=0,do_effect;
    integer q,k,scene,configuration=64,count=0,checks=0,px,py;
    integer affected=0,protected_ui=0,protected_border=0,protected_outside=0;
    reg [18:0] expected[0:9];
    reg [15:0] expected_effect;
    multitini_video_test #(.DYNAMIC_DISPLAY(1),.SIGNED_VIEWPORT(1),
        .H_ACTIVE(HA),.H_FRONT(2),.H_SYNC(2),.H_TOTAL(HT),
        .V_ACTIVE(VA),.V_FRONT(1),.V_SYNC(1),.V_TOTAL(VT)) reference_i(
        .display_h_active(12'(HA)),.display_h_front(12'd2),.display_h_sync(12'd2),.display_h_total(12'(HT)),
        .display_v_active(12'(VA)),.display_v_front(12'd1),.display_v_sync(12'd1),.display_v_total(12'(VT)),
        .display_h_positive(1'b1),.display_v_positive(1'b1),
        .display_scale_num(4'(SCALE_NUM)),.display_scale_den(4'd1),
        .pixel_clk(clk),.resetn(resetn),.enable(1'b1),.machine_enable(1'b1),
        .machine_rgb565(machine_rgb),.ui_opaque(ui_opaque),.ui_rgb565(16'h5b6a),
        .border_opaque(border_opaque),.border_rgb565(16'he234),.scan_x(x),.scan_y(y),
        .vblank_start(),.dvi_red(rgb_direct[15:11]),.dvi_grn(rgb_direct[10:5]),.dvi_blu(rgb_direct[4:0]),
        .dvi_clk(),.dvi_de(de_direct),.dvi_hsync(hs_direct),.dvi_vsync(vs_direct),
        .control_clk(control_clk),.control_resetn(resetn),.effect_write(1'b0),.effect_address(10'd0),
        .effect_data(32'd0),.effect_commit(1'b0),.effect_commit_sequence(32'd0),
        .viewport_x(12'(ORIGIN_X)),.viewport_y(12'(ORIGIN_Y)),
        .viewport_width(12'(VIEW_WIDTH)),.viewport_height(12'(VIEW_HEIGHT))
    );
    multitini_video_test #(.DYNAMIC_DISPLAY(1),.SIGNED_VIEWPORT(1),.ENABLE_EFFECTS(1),
        .SHARED_PIXEL_RESET(1),.SHARED_PIXEL_LIVENESS(1),
        .H_ACTIVE(HA),.H_FRONT(2),.H_SYNC(2),.H_TOTAL(HT),
        .V_ACTIVE(VA),.V_FRONT(1),.V_SYNC(1),.V_TOTAL(VT)) effects_i(
        .display_h_active(12'(HA)),.display_h_front(12'd2),.display_h_sync(12'd2),.display_h_total(12'(HT)),
        .display_v_active(12'(VA)),.display_v_front(12'd1),.display_v_sync(12'd1),.display_v_total(12'(VT)),
        .display_h_positive(1'b1),.display_v_positive(1'b1),
        .display_scale_num(4'(SCALE_NUM)),.display_scale_den(4'd1),
        .pixel_clk(clk),.resetn(resetn),.enable(1'b1),.machine_enable(1'b1),
        .machine_rgb565(machine_rgb),.machine_neighbor_rgb565(neighbor),.machine_opposite_rgb565(opposite),
        .machine_dot_valid(1'b1),.machine_neighbor_weight(8'd0),.ui_opaque(ui_opaque),.ui_rgb565(16'h5b6a),
        .border_opaque(border_opaque),.border_rgb565(16'he234),.scan_x(),.scan_y(),
        .vblank_start(),.dvi_red(rgb_effect[15:11]),.dvi_grn(rgb_effect[10:5]),.dvi_blu(rgb_effect[4:0]),
        .dvi_clk(),.dvi_de(de_effect),.dvi_hsync(hs_effect),.dvi_vsync(vs_effect),
        .control_clk(control_clk),.control_resetn(resetn),.effect_write(write),.effect_address(address),
        .shared_pixel_resetn(shared_reset[2]),.shared_pixel_running(shared_live[1]),
        .effect_data(data),.effect_commit(commit),.effect_commit_sequence(commit_sequence),
        .effect_busy(busy),.effect_active_valid(valid),.effect_rejected(rejected),.effect_active_sequence(active_sequence),
        .viewport_x(12'(ORIGIN_X)),.viewport_y(12'(ORIGIN_Y)),
        .viewport_width(12'(VIEW_WIDTH)),.viewport_height(12'(VIEW_HEIGHT))
    );
    function automatic integer channel8(input [15:0] p,input integer channel);
        case(channel)
            0:channel8=(((p>>11)&31)<<3)|((p>>13)&7);
            1:channel8=(((p>>5)&63)<<2)|((p>>9)&3);
            default:channel8=((p&31)<<3)|((p>>2)&7);
        endcase
    endfunction
    function automatic integer dot_weight(input integer center,near,far,level);
        case(level)
            1:dot_weight=24*center+8*near+16;
            2:dot_weight=20*center+12*near+16;
            3:dot_weight=17*center+14*near+far+16;
            default:dot_weight=32*center;
        endcase
    endfunction
    function automatic [15:0] oracle(input [15:0] center,near,far,input integer xx,yy,cfg);
        integer channel,c,n,f,code,opposite_code,quantization,value,selected,phase,row_phase;
        reg [15:0] bleed;
        begin
            bleed=0;
            for(channel=0;channel<3;channel=channel+1)begin
                c=channel8(center,channel);n=channel8(near,channel);f=channel8(far,channel);
                quantization=channel==1 ? 7:8;
                code=dot_weight(c,n,f,(cfg>>4)&3)>>quantization;
                opposite_code=dot_weight(c,f,n,(cfg>>4)&3)>>quantization;
                if(SCALE_NUM==1)code=(code+opposite_code)/2;
                case(channel)0:bleed=bleed|(code<<11);1:bleed=bleed|(code<<5);default:bleed=bleed|code;endcase
            end
            oracle=0;phase=xx%3;row_phase=yy%3;
            for(channel=0;channel<3;channel=channel+1)begin
                value=255-channel8(bleed,channel);
                if(SCALE_NUM>1 && yy%SCALE_NUM>=(SCALE_NUM+1)/2)begin
                    case(cfg&3)1:value=value*3/4;2:value=value/2;3:value=value/4;default:;endcase
                end
                selected=phase;
                if(((cfg>>2)&3)==2 && (yy&1))selected=(phase+1)%3;
                if(((((cfg>>2)&3)==1 || ((cfg>>2)&3)==2) && selected!=channel) ||
                    (((cfg>>2)&3)==3 && (phase==2 || row_phase==2)))value=value-value/4;
                case(channel)0:oracle=oracle|((value>>3)<<11);1:oracle=oracle|((value>>2)<<5);default:oracle=oracle|(value>>3);endcase
            end
        end
    endfunction
    task wr(input integer a,input [31:0] d);
        begin
            @(negedge control_clk);address=a;data=d;write=1;
            @(negedge control_clk);write=0;
        end
    endtask
    always @(posedge clk)if(checking)begin
        px=integer'(x)-ORIGIN_X;py=integer'(y)-ORIGIN_Y;
        do_effect=reference_i.use_machine && !ui_opaque && !border_opaque &&
            px>=0 && px<VIEW_WIDTH && py>=0 && py<VIEW_HEIGHT;
        expected_effect=oracle(machine_rgb,neighbor,opposite,px,py,configuration);
        if(x<HA && y<VA)begin
            if(ui_opaque)protected_ui=protected_ui+1;
            else if(border_opaque)protected_border=protected_border+1;
            else if(do_effect)affected=affected+1;
            else protected_outside=protected_outside+1;
        end
        #1;
        for(q=9;q>0;q=q-1)expected[q]=expected[q-1];
        expected[0]={vs_direct,hs_direct,de_direct,
            do_effect && de_direct ? expected_effect:rgb_direct};
        count=count+1;
        if(count>10)begin
            if({vs_effect,hs_effect,de_effect,rgb_effect}!==expected[9])
                $fatal(1,"Signed geometry x=%0d y=%0d scale=%0d origin=%0d,%0d config=%h expected=%h got=%h",
                    x,y,SCALE_NUM,ORIGIN_X,ORIGIN_Y,configuration,expected[9],{vs_effect,hs_effect,de_effect,rgb_effect});
            checks=checks+1;
        end
    end
    initial begin
        repeat(8)@(negedge control_clk);resetn=1;
        repeat(30)@(negedge control_clk);
        for(scene=0;scene<4;scene=scene+1)begin
            checking=0;
            case(scene)0:configuration=64;1:configuration=85;2:configuration=106;default:configuration=127;endcase
            for(k=0;k<9;k=k+1)wr(k,(k==0 || k==4 || k==8)?1024:0);
            for(k=9;k<12;k=k+1)wr(k,0);
            wr(12,configuration);
            for(k=0;k<256;k=k+1)wr(256+k,{8'd0,{3{8'(255-k)}}});
            @(negedge control_clk);commit_sequence=scene+1;commit=1;
            @(negedge control_clk);commit=0;
            wait(valid && !busy && active_sequence==commit_sequence);
            if(rejected)$fatal(1,"Signed geometry table commit rejected");
            repeat(20)@(negedge clk);count=0;checking=1;
            repeat(3500)@(negedge clk);
        end
        if(affected<1000 || protected_ui<100 || protected_border<100 ||
           ((ORIGIN_X>=0 || ORIGIN_Y>=0) && protected_outside<100))$fatal(1,"Insufficient signed geometry coverage");
        $display("PASS signed video geometry: %0d pixels scale=%0d origin=%0d,%0d; all Dot bleed levels, scanline/mask phase, full-screen UI/border isolation, nine-stage effects and DVI timing",checks,SCALE_NUM,ORIGIN_X,ORIGIN_Y);
        $finish;
    end
    initial begin #10000000;$fatal(1,"Signed video geometry timeout");end
endmodule
