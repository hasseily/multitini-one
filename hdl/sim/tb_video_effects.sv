// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_video_effects;
    parameter integer SCALE_NUM=4, SCALE_DEN=1;
    parameter bit DYNAMIC_DISPLAY=0;
    parameter bit SHARED_PIXEL_RESET=0;
    parameter bit SHARED_PIXEL_LIVENESS=SHARED_PIXEL_RESET;
    reg control_clk=0,pixel_clk=0,control_resetn=0,pixel_resetn=0;
    always #5 control_clk=~control_clk;
    always #3.367 pixel_clk=~pixel_clk;
    (* ASYNC_REG="TRUE" *) reg [2:0] shared_release;
    always @(posedge pixel_clk or negedge control_resetn)
        if(!control_resetn)shared_release<=0;
        else shared_release<={shared_release[1:0],1'b1};
    wire shared_pixel_resetn=shared_release[2];
    reg [2:0] shared_local_release;
    (* ASYNC_REG="TRUE" *) reg [1:0] shared_live;
    always @(posedge pixel_clk)
        if(!shared_pixel_resetn || !pixel_resetn)shared_local_release<=0;
        else shared_local_release<={shared_local_release[1:0],1'b1};
    always @(posedge control_clk)
        if(!control_resetn)shared_live<=0;
        else shared_live<={shared_live[0],shared_local_release[2]};
    wire shared_pixel_running=shared_live[1];
    reg write=0,commit=0,vblank=0;
    reg [9:0] address=0;
    reg [31:0] data=0,commit_seq=0;
    wire busy,valid,rejected;
    wire [31:0] active_sequence;
    reg [15:0] rgb=0,neighbor_rgb=0,opposite_rgb=0;
    reg [7:0] neighbor_weight=0;
    reg enable=0,de=0,hs=0,vs=0;
    reg [11:0] x=0,y=0;
    wire [15:0] output_rgb;
    wire output_de,output_hs,output_vs;
    multitini_video_effects #(.DYNAMIC_DISPLAY(DYNAMIC_DISPLAY),.SOURCE_SCALE_NUM(SCALE_NUM),.SOURCE_SCALE_DEN(SCALE_DEN),
        .SHARED_PIXEL_RESET(SHARED_PIXEL_RESET),.SHARED_PIXEL_LIVENESS(SHARED_PIXEL_LIVENESS)) dut(
        .display_scale_num(4'(SCALE_NUM)),.display_scale_den(4'(SCALE_DEN)),
        .control_clk(control_clk),.control_resetn(control_resetn),
        .effect_write(write),.effect_address(address),.effect_data(data),
        .effect_commit(commit),.effect_commit_sequence(commit_seq),
        .effect_busy(busy),.effect_active_valid(valid),.effect_rejected(rejected),
        .effect_active_sequence(active_sequence),.pixel_clk(pixel_clk),.pixel_resetn(pixel_resetn),
        .shared_pixel_resetn(shared_pixel_resetn),.shared_pixel_running(shared_pixel_running),
        .vblank_start(vblank),.pixel_rgb565(rgb),.pixel_neighbor_rgb565(neighbor_rgb),.pixel_opposite_rgb565(opposite_rgb),.pixel_dot_valid(1'b1),.pixel_neighbor_weight(neighbor_weight),.pixel_effect_enable(enable),
        .pixel_x(x),.pixel_y(y),.pixel_de(de),.pixel_hsync(hs),.pixel_vsync(vs),
        .output_rgb565(output_rgb),.output_de(output_de),.output_hsync(output_hs),.output_vsync(output_vs)
    );
    integer matrix[0:8],lut[0:2][0:255];
    integer config_value=0,checks=0,queue_count=0;
    reg [18:0] expected_queue[0:8];
    integer row,column,mode,channel,index,k;
    integer cval,value,scan_phase,selected,sx,nx,distance,base_weight,mix_weight;
    reg [15:0] current_pixel,expected_pixel;
    reg [23:0] center_tone;
    reg [15:0] mixed_pixel;
    reg [7:0] result_channel[0:2];
    reg current_enable;

    task wr(input integer a,input [31:0] d);
        begin
            @(negedge control_clk);address=a;data=d;write=1;
            @(negedge control_clk);write=0;
        end
    endtask
    task submit(input [31:0] seq);
        begin
            @(negedge control_clk);commit_seq=seq;commit=1;
            @(negedge control_clk);commit=0;
        end
    endtask
    task activate;
        begin
            repeat(15) @(negedge pixel_clk);
            vblank=1;
            @(negedge pixel_clk);vblank=0;
            repeat(30) @(negedge control_clk);
            if(busy || !valid) $fatal(1,"Commit did not finish at blank");
        end
    endtask
    task table_write;
        begin
            for(k=0;k<9;k=k+1) wr(k,matrix[k]&65535);
            for(k=9;k<12;k=k+1) wr(k,0);
            wr(12,config_value);
            for(k=0;k<256;k=k+1) wr(256+k,(lut[2][k]<<16)|(lut[1][k]<<8)|lut[0][k]);
        end
    endtask
    task identity;
        begin
            for(k=0;k<9;k=k+1) matrix[k]=(k==0 || k==4 || k==8)?1024:0;
            for(k=0;k<256;k=k+1) begin lut[0][k]=k;lut[1][k]=k;lut[2][k]=k;end
        end
    endtask
    task pixel(input [15:0] input_pixel,input enable_pixel,input integer px,py,
               input input_de,input input_hs,input input_vs,input [15:0] expected);
        integer q;
        begin
            @(negedge pixel_clk);
            rgb=input_pixel;enable=enable_pixel;x=px;y=py;de=input_de;hs=input_hs;vs=input_vs;
            @(posedge pixel_clk);
            for(q=8;q>0;q=q-1)expected_queue[q]=expected_queue[q-1];
            expected_queue[0]={input_vs,input_hs,input_de,expected};
            queue_count=queue_count+1;
            #1;
            if(queue_count>=9)begin
                if({output_vs,output_hs,output_de,output_rgb}!==expected_queue[8])
                    $fatal(1,"Pixel mismatch check=%0d x=%0d y=%0d config=%h expected=%h got=%h",checks,px,py,config_value,expected_queue[8],{output_vs,output_hs,output_de,output_rgb});
                checks=checks+1;
            end
        end
    endtask
    function automatic [23:0] tone(input [15:0] p);
        integer r,g,b,c,v;
        reg [23:0] result;
        begin
            r=((p>>11)&31)*8+((p>>13)&7);
            g=((p>>5)&63)*4+((p>>9)&3);
            b=(p&31)*8+((p>>2)&7);
            for(c=0;c<3;c=c+1) begin
                v=(matrix[c*3]*r+matrix[c*3+1]*g+matrix[c*3+2]*b+512)>>>10;
                if(v<0)v=0;if(v>255)v=255;
                result[c*8+:8]=lut[c][v];
            end
            tone=result;
        end
    endfunction
    function automatic [15:0] source(input integer px,py);
        source=(px*1091+py*4567)^16'hae73;
    endfunction
    function automatic allowed(input integer px);
        allowed=px>=3 && px<61 && !(px>=19 && px<24) && px!=38;
    endfunction
    task check_lines;
        begin
            queue_count=0;
            for(row=0;row<12;row=row+1)begin
                for(column=0;column<66;column=column+1)begin
                    sx=column*SCALE_DEN/SCALE_NUM;
                    distance=2*((column*SCALE_DEN)%SCALE_NUM)+SCALE_DEN-SCALE_NUM;
                    nx=sx+(distance<0 ? -1 : 1);
                    if(nx<0)nx=0;if(nx>65*SCALE_DEN/SCALE_NUM)nx=65*SCALE_DEN/SCALE_NUM;
                    base_weight=((distance<0 ? -distance : distance)*128+SCALE_NUM/2)/SCALE_NUM;
                    current_pixel=source(sx,row);current_enable=allowed(column);
                    neighbor_rgb=source(nx,row);neighbor_weight=base_weight;
                    nx=sx+(distance<0 ? 1:-1);
                    if(nx<0)nx=0;if(nx>65*SCALE_DEN/SCALE_NUM)nx=65*SCALE_DEN/SCALE_NUM;
                    opposite_rgb=source(nx,row);
                    mix_weight=((config_value>>4)&3)!=0 ? base_weight : 0;
                    mixed_pixel=dot_pixel(current_pixel,neighbor_rgb,opposite_rgb,(config_value>>4)&3,SCALE_NUM==SCALE_DEN);
                    center_tone=tone(mixed_pixel);
                    for(channel=0;channel<3;channel=channel+1)begin
                        value=center_tone[channel*8+:8];
                        scan_phase=row%SCALE_NUM;
                        if(SCALE_DEN==2 ? scan_phase==1 : scan_phase>=(SCALE_NUM+1)/2)
                            case(config_value&3)
                                1:value=value*3/4;2:value=value/2;3:value=value/4;
                            endcase
                        selected=(column%3+((((config_value>>2)&3)==2)?row%2:0))%3;
                        case((config_value>>2)&3)
                            1,2:if(selected!=channel)value=value-value/4;
                            3:if(column%3==2 || row%3==2)value=value-value/4;
                        endcase
                        result_channel[channel]=value;
                    end
                    expected_pixel={result_channel[0][7:3],result_channel[1][7:2],result_channel[2][7:3]};
                    if(!current_enable || !(config_value&64))expected_pixel=current_pixel;
                    pixel(current_pixel,current_enable,column,row,column<64,column>=62,row>=10,expected_pixel);
                end
            end
            neighbor_weight=0;
            for(column=0;column<9;column=column+1)pixel(0,0,column,99,0,0,0,0);
        end
    endtask
    function automatic [15:0] dot_pixel(input [15:0] center,near,far,input integer mode,input avg);
        integer ch,shift,bits,mask,a,b,c,m,n,f,v,w;
        reg [15:0] result;
        begin
            result=center;
            if(mode!=0)begin
                result=0;
                case(mode)1:begin m=24;n=8;f=0;end 2:begin m=20;n=12;f=0;end default:begin m=17;n=14;f=1;end endcase
                for(ch=0;ch<3;ch=ch+1)begin
                    shift=ch==0 ? 11:ch==1 ? 5:0;bits=ch==1 ? 6:5;mask=(1<<bits)-1;
                    a=(center>>shift)&mask;b=(near>>shift)&mask;c=(far>>shift)&mask;
                    a=(a<<(8-bits))|(a>>(2*bits-8));b=(b<<(8-bits))|(b>>(2*bits-8));c=(c<<(8-bits))|(c>>(2*bits-8));
                    v=((m*a+n*b+f*c+16)>>5)>>(8-bits);
                    w=((m*a+n*c+f*b+16)>>5)>>(8-bits);
                    if(avg)v=(v+w)>>1;
                    result=result|(v<<shift);
                end
            end
            dot_pixel=result;
        end
    endfunction
    task check_horizontal_arithmetic;
        integer center_value,neighbor_value,weight,reference_value,arithmetic_checks;
        reg [7:0] actual_value;
        reg signed [17:0] product;
        begin
            arithmetic_checks=0;
            // Full signed-difference and rounding range, including every Q8
            // weight used by the supported integer/fractional scaler phases.
            for(center_value=0;center_value<256;center_value=center_value+1)
                for(neighbor_value=0;neighbor_value<256;neighbor_value=neighbor_value+1)
                    for(weight=0;weight<=128;weight=weight+1)begin
                        reference_value=(center_value*(256-weight)+neighbor_value*weight+128)/256;
                        product=(neighbor_value-center_value)*weight;
                        actual_value=dut.blend_round(center_value[7:0],product);
                        if(actual_value!==reference_value[7:0])
                            $fatal(1,"Interpolation arithmetic c=%0d n=%0d w=%0d expected=%0d got=%0d",
                                   center_value,neighbor_value,weight,reference_value,actual_value);
                        arithmetic_checks=arithmetic_checks+1;
                    end
            $display("PASS horizontal interpolation arithmetic: %0d RGB8/weight cases",arithmetic_checks);
        end
    endtask
    initial begin
        if(SCALE_NUM==4 && SCALE_DEN==1 && !SHARED_PIXEL_RESET && !DYNAMIC_DISPLAY)check_horizontal_arithmetic();
        for(k=0;k<9;k=k+1)expected_queue[k]=0;
        identity();
        repeat(5)@(negedge control_clk);control_resetn=1;pixel_resetn=1;
        repeat(20)@(negedge control_clk);
        // Reset bypass cannot read uninitialized LUT data.
        for(index=0;index<80;index=index+1)pixel(index*991,1,index,0,index%2,index%3==0,index%5==0,index*991);
        submit(1);repeat(4)@(negedge control_clk);
        if(!rejected || valid || busy)$fatal(1,"Incomplete table was accepted");
        config_value=64;table_write();submit(2);
        repeat(20)@(negedge control_clk);
        if(!busy || valid)$fatal(1,"Table applied before vertical blank");
        wr(256,0); // Busy writes must not mutate the pending bank.
        activate();if(active_sequence!=2)$fatal(1,"Wrong committed sequence");
        queue_count=0;
        // Exhaustive neutral transform is exactly RGB565 identity.
        for(index=0;index<65536;index=index+1)pixel(index,1,index%128,index/128,1,index%17==0,index%23==0,index);
        for(mode=0;mode<64;mode=mode+1)begin
            config_value=64|mode;table_write();submit(3+mode);activate();check_lines();
        end
        // Cross-channel signed transform, clipping and nonlinear LUTs.
        matrix[0]=2048;matrix[1]=-512;matrix[2]=256;
        matrix[3]=0;matrix[4]=0;matrix[5]=1024;
        matrix[6]=-32768;matrix[7]=32767;matrix[8]=32767;
        for(k=0;k<256;k=k+1)begin lut[0][k]=255-k;lut[1][k]=(k*k)/255;lut[2][k]=k/2;end
        config_value=127;table_write();submit(100);activate();check_lines();
        // Master bypass also bypasses nonidentity matrix/LUT/spatial settings.
        config_value=63;table_write();submit(101);activate();check_lines();
        // A reset while the cross-clock commit is waiting for vblank must
        // drain the old handshake without applying/reusing its stale bank.
        identity();config_value=64;table_write();submit(1010);
        repeat(12)@(negedge control_clk);
        if(!busy)$fatal(1,"Pending-reset test had no outstanding publication");
        @(negedge pixel_clk);pixel_resetn=0;
        repeat(8)@(negedge control_clk);pixel_resetn=1;
        repeat(40)@(negedge control_clk);
        if(valid || active_sequence!=0 || busy)$fatal(1,"Pending pixel reset retained publication");
        table_write();submit(1011);activate();check_lines();
        // Reset either domain independently invalidates ownership and requires
        // a fresh complete table; old LUT contents must remain unreachable.
        @(negedge pixel_clk);pixel_resetn=0;
        repeat(8)@(negedge control_clk);pixel_resetn=1;
        repeat(30)@(negedge control_clk);
        if(valid || active_sequence!=0 || busy)$fatal(1,"Pixel reset left stale ownership");
        identity();config_value=64;table_write();submit(102);activate();check_lines();
        table_write();submit(1020);
        repeat(12)@(negedge control_clk);
        if(!busy)$fatal(1,"Control-reset test had no outstanding publication");
        @(negedge control_clk);control_resetn=0;
        repeat(8)@(negedge control_clk);control_resetn=1;
        repeat(30)@(negedge control_clk);
        if(valid || active_sequence!=0 || busy)$fatal(1,"Control reset left stale ownership");
        table_write();submit(1021);activate();check_lines();
        wr(12,128);if(!rejected)$fatal(1,"Reserved control bits accepted");
        $display("PASS video effects: %0d pixels, all spatial combinations, identity565, signed matrix/LUT, atomic banks, busy writes, independent resets, scale=%0d/%0d",checks,SCALE_NUM,SCALE_DEN);
        $finish;
    end
    initial begin #20000000;$fatal(1,"Video effects test timed out");end
endmodule

// Feed tables and expected pixels produced by the portable CPU1 C compiler.
module tb_video_effects_oracle;
    parameter integer SCALE_NUM=4,SCALE_DEN=1;
    reg control_clk=0,pixel_clk=0,resetn=0,write=0,commit=0,vblank=0;
    always #5 control_clk=~control_clk;
    always #3.367 pixel_clk=~pixel_clk;
    reg [9:0] address=0;
    reg [31:0] data=0,commit_seq=0;
    wire busy,valid,rejected;
    wire [31:0] active_seq;
    reg [15:0] rgb=0,neighbor_rgb=0,opposite_rgb=0;
    reg [7:0] neighbor_weight=0;
    reg enable=0;
    reg [11:0] x=0,y=0;
    wire [15:0] out_rgb;
    multitini_video_effects #(.DYNAMIC_DISPLAY(1),.SOURCE_SCALE_NUM(SCALE_NUM),.SOURCE_SCALE_DEN(SCALE_DEN)) dut(
        .display_scale_num(4'(SCALE_NUM)),.display_scale_den(4'(SCALE_DEN)),
        .control_clk(control_clk),.control_resetn(resetn),.effect_write(write),
        .effect_address(address),.effect_data(data),.effect_commit(commit),.effect_commit_sequence(commit_seq),
        .effect_busy(busy),.effect_active_valid(valid),.effect_rejected(rejected),.effect_active_sequence(active_seq),
        .pixel_clk(pixel_clk),.pixel_resetn(resetn),.vblank_start(vblank),
        .pixel_rgb565(rgb),.pixel_neighbor_rgb565(neighbor_rgb),.pixel_opposite_rgb565(opposite_rgb),.pixel_dot_valid(1'b1),.pixel_neighbor_weight(neighbor_weight),.pixel_effect_enable(enable),.pixel_x(x),.pixel_y(y),
        .pixel_de(1'b1),.pixel_hsync(1'b0),.pixel_vsync(1'b0),.output_rgb565(out_rgb),
        .output_de(),.output_hsync(),.output_vsync());
    reg [15:0] expected_queue[0:8];
    integer fd,result,a,d,p,e,px,py,expected,n,w,o,i,q,count=0,checks=0,scenes=0;
    reg [2047:0] filename;
    reg [15:0] operation;
    reg pixels_pending=0;
    task pixel(input [15:0] color,input on,input integer column,row,input [15:0] target);
        begin
            @(negedge pixel_clk);rgb=color;enable=on;x=column;y=row;
            @(posedge pixel_clk);
            for(q=8;q>0;q=q-1)expected_queue[q]=expected_queue[q-1];
            expected_queue[0]=target;count=count+1;
            #1;
            if(count>=9)begin
                if(out_rgb!==expected_queue[8])
                    $fatal(1,"C oracle mismatch scene=%0d check=%0d expected=%h got=%h",scenes,checks,expected_queue[8],out_rgb);
                checks=checks+1;
            end
        end
    endtask
    initial begin
        if(!$value$plusargs("vectors=%s",filename))$fatal(1,"Missing C oracle vectors");
        fd=$fopen(filename,"r");if(fd==0)$fatal(1,"Cannot open C oracle vectors");
        repeat(8)@(negedge control_clk);resetn=1;
        repeat(30)@(negedge control_clk);
        while(!$feof(fd))begin
            result=$fscanf(fd,"%s",operation);
            if(result==1)case(operation)
                "W":begin
                    if(pixels_pending)begin
                        for(i=0;i<9;i=i+1)pixel(0,0,i,4095,0);
                        pixels_pending=0;count=0;
                    end
                    result=$fscanf(fd,"%h %h",a,d);if(result!=2)$fatal(1,"Malformed oracle W");
                    @(negedge control_clk);address=a;data=d;write=1;
                    @(negedge control_clk);write=0;
                end
                "C":begin
                    result=$fscanf(fd,"%h",d);if(result!=1)$fatal(1,"Malformed oracle C");
                    @(negedge control_clk);commit_seq=d;commit=1;
                    @(negedge control_clk);commit=0;
                    repeat(20)@(negedge pixel_clk);vblank=1;
                    @(negedge pixel_clk);vblank=0;
                    repeat(30)@(negedge control_clk);
                    if(busy || !valid || rejected || active_seq!=commit_seq)$fatal(1,"C table publication failed");
                    scenes=scenes+1;count=0;
                end
                "P":begin
                    result=$fscanf(fd,"%h %d %d %d %h %h %d %h",p,e,px,py,expected,n,w,o);
                    neighbor_rgb=n;neighbor_weight=w;opposite_rgb=o;
                    if(result!=8)$fatal(1,"Malformed oracle P");
                    pixel(p,e!=0,px,py,expected);pixels_pending=1;
                end
                default:$fatal(1,"Unknown C oracle command %s",operation);
            endcase
        end
        for(i=0;i<9;i=i+1)pixel(0,0,i,4095,0);
        $fclose(fd);
        if(scenes!=24 || checks<6336)$fatal(1,"Incomplete C oracle coverage");
        $display("PASS C/HDL video effects agreement: %0d tables, %0d pixels",scenes,checks);
        $finish;
    end
    initial begin #10000000;$fatal(1,"C oracle timeout");end
endmodule

// End-to-end comparison against the existing direct output. Effects may touch
// only unobscured machine pixels inside the viewport; every layer and all DVI
// timing signals must retain exactly the same relative alignment.
module tb_video_effects_output;
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
    reg [31:0] data=0;
    wire busy,valid,rejected;
    wire [31:0] active_sequence;
    wire [11:0] x,y;
    wire [15:0] rgb_direct,rgb_effect;
    wire de_direct,hs_direct,vs_direct,de_effect,hs_effect,vs_effect;
    wire [15:0] machine_rgb=(x*97+y*341)^16'hb6ea;
    wire ui_opaque=(x>=19 && x<=24) || y==8;
    wire border_opaque=x==2 || x==61 || y==10;
    reg checking=0;
    integer k,q,count=0,checks=0;
    reg do_effect;
    reg [18:0] expected[0:9];
    multitini_video_test #(.H_ACTIVE(64),.H_FRONT(2),.H_SYNC(2),.H_TOTAL(70),
        .V_ACTIVE(12),.V_FRONT(1),.V_SYNC(1),.V_TOTAL(15)) reference_i(
        .pixel_clk(clk),.resetn(resetn),.enable(1'b1),.machine_enable(1'b1),
        .machine_rgb565(machine_rgb),.machine_neighbor_rgb565(machine_rgb),.machine_neighbor_weight(8'd0),.ui_opaque(ui_opaque),.ui_rgb565(16'h5b6a),
        .border_opaque(border_opaque),.border_rgb565(16'he234),.scan_x(x),.scan_y(y),
        .vblank_start(),.dvi_red(rgb_direct[15:11]),.dvi_grn(rgb_direct[10:5]),.dvi_blu(rgb_direct[4:0]),
        .dvi_clk(),.dvi_de(de_direct),.dvi_hsync(hs_direct),.dvi_vsync(vs_direct),
        .control_clk(control_clk),.control_resetn(resetn),.effect_write(1'b0),.effect_address(10'd0),
        .effect_data(32'd0),.effect_commit(1'b0),.effect_commit_sequence(32'd0),
        .viewport_x(12'd3),.viewport_y(12'd1),.viewport_width(12'd58),.viewport_height(12'd9)
    );
    multitini_video_test #(.ENABLE_EFFECTS(1),.SHARED_PIXEL_RESET(1),.SHARED_PIXEL_LIVENESS(1),
        .H_ACTIVE(64),.H_FRONT(2),.H_SYNC(2),.H_TOTAL(70),
        .V_ACTIVE(12),.V_FRONT(1),.V_SYNC(1),.V_TOTAL(15)) effects_i(
        .pixel_clk(clk),.resetn(resetn),.enable(1'b1),.machine_enable(1'b1),
        .machine_rgb565(machine_rgb),.machine_neighbor_rgb565(machine_rgb),.machine_neighbor_weight(8'd0),.ui_opaque(ui_opaque),.ui_rgb565(16'h5b6a),
        .border_opaque(border_opaque),.border_rgb565(16'he234),.scan_x(),.scan_y(),
        .vblank_start(),.dvi_red(rgb_effect[15:11]),.dvi_grn(rgb_effect[10:5]),.dvi_blu(rgb_effect[4:0]),
        .dvi_clk(),.dvi_de(de_effect),.dvi_hsync(hs_effect),.dvi_vsync(vs_effect),
        .control_clk(control_clk),.control_resetn(resetn),.effect_write(write),.effect_address(address),
        .shared_pixel_resetn(shared_reset[2]),.shared_pixel_running(shared_live[1]),
        .effect_data(data),.effect_commit(commit),.effect_commit_sequence(32'd42),
        .effect_busy(busy),.effect_active_valid(valid),.effect_rejected(rejected),.effect_active_sequence(active_sequence),
        .viewport_x(12'd3),.viewport_y(12'd1),.viewport_width(12'd58),.viewport_height(12'd9)
    );
    task wr(input integer a,input [31:0] d);
        begin
            @(negedge control_clk);address=a;data=d;write=1;
            @(negedge control_clk);write=0;
        end
    endtask
    always @(posedge clk) begin
        if(checking) begin
            do_effect=reference_i.use_machine && !ui_opaque && !border_opaque &&
                      x>=3 && x<61 && y>=1 && y<10;
            #1;
            for(q=9;q>0;q=q-1)expected[q]=expected[q-1];
            expected[0]={vs_direct,hs_direct,de_direct,
                (do_effect && de_direct)?(rgb_direct^16'hffff):rgb_direct};
            count=count+1;
            if(count>10) begin
                if({vs_effect,hs_effect,de_effect,rgb_effect}!==expected[9])
                    $fatal(1,"Composed output alignment mismatch x=%0d y=%0d expected=%h got=%h",x,y,expected[9],{vs_effect,hs_effect,de_effect,rgb_effect});
                checks=checks+1;
            end
        end
    end
    initial begin
        repeat(8)@(negedge control_clk);resetn=1;
        repeat(30)@(negedge control_clk);
        for(k=0;k<9;k=k+1)wr(k,(k==0 || k==4 || k==8)?1024:0);
        for(k=9;k<12;k=k+1)wr(k,0);
        wr(12,64);
        for(k=0;k<256;k=k+1)wr(256+k,{8'd0,{3{8'(255-k)}}});
        @(negedge control_clk);commit=1;
        @(negedge control_clk);commit=0;
        wait(valid && !busy);
        if(active_sequence!=42 || rejected)$fatal(1,"Composition table commit failed");
        repeat(20)@(negedge clk);checking=1;
        repeat(3500)@(negedge clk);
        $display("PASS video effects composition: %0d pixels, UI/border/viewport isolation and matched DVI sync/DE",checks);
        $finish;
    end
    initial begin #1000000;$fatal(1,"Composition test timeout");end
endmodule
