// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Exercise the actual autonomous capture -> DDR -> engine -> DDR -> scanout
// path. Both AXI ports see the same bounded byte-addressed memory. The host
// writes enable/options only; it never advances either ownership ring.
module tb_video_frame_pipeline;
    parameter integer SCALE_NUM=4,SCALE_DEN=1;
    parameter bit DYNAMIC_DISPLAY=0;
    parameter bit MANUAL_CROP_TEST=0;
    reg display_pause=0,display_pixel_hold=0;
    wire display_quiescent;
    parameter bit SOURCE_PAUSE_TEST=0,EXPECT_SOURCE_FAILURE=0;
    // PHO2 sums normalized Blur taps before rounding, without scale retuning.
    initial begin
        #1;
        if(dut.engine_i.filter.blur_vertical_tent(8'd7,8'd11,8'd19)!==8'd12)
            $fatal(1,"Blur vertical tent did not round the normalized sum");
    end
    localparam integer WIDTH=13,HEIGHT=8,VIEW_X=16,VIEW_Y=2;
    localparam integer OUT_WIDTH=WIDTH*SCALE_NUM/SCALE_DEN;
    localparam integer OUT_HEIGHT=HEIGHT*SCALE_NUM/SCALE_DEN;
    localparam integer STRIDE=128,SLOT_BYTES=STRIDE*HEIGHT,PIXELS=WIDTH*HEIGHT;
    localparam integer H_TOTAL=256,V_TOTAL=96;
    localparam integer READER_NUM=MANUAL_CROP_TEST ? 3:SCALE_NUM;
    localparam integer READER_DEN=MANUAL_CROP_TEST ? 1:SCALE_DEN;
    localparam integer READER_X=MANUAL_CROP_TEST ? -4:VIEW_X;
    localparam integer READER_Y=MANUAL_CROP_TEST ? -5:VIEW_Y;
    localparam integer READER_WIDTH=WIDTH*READER_NUM/READER_DEN;
    localparam integer READER_HEIGHT=HEIGHT*READER_NUM/READER_DEN;
    localparam integer DISPLAY_X0=READER_X<0 ? 0:READER_X;
    localparam integer DISPLAY_Y0=READER_Y<0 ? 0:READER_Y;
    localparam integer DISPLAY_X1=READER_X+READER_WIDTH;
    localparam integer DISPLAY_Y1=READER_Y+READER_HEIGHT;
    localparam integer DISPLAY_PIXELS=(DISPLAY_X1-DISPLAY_X0)*(DISPLAY_Y1-DISPLAY_Y0);
    localparam integer BLANK_Y=MANUAL_CROP_TEST && DISPLAY_Y1>VIEW_Y+OUT_HEIGHT ? DISPLAY_Y1:VIEW_Y+OUT_HEIGHT;
    reg clk=0,pixel_clk=0,resetn=0;
    reg [7:0] accepted_options;
    always @(posedge clk)if(resetn && dut.engine_i.filter.start && !dut.engine_i.filter.busy)begin
        accepted_options=dut.engine_options;
        if(dut.engine_i.filter.options!==accepted_options)
            $fatal(1,"Native filter lost horizontal option bits");
        #0.001;
        if(dut.engine_i.job_options!==accepted_options)
            $fatal(1,"Frame engine did not capture all eight option bits");
    end
    // Match the production 133 MHz HP and 148.5 MHz pixel clock ratio, with
    // distinct edges. The small synthetic raster keeps ample vertical blank.
    always #3.759 clk=!clk;
    always #3.367 pixel_clk=!pixel_clk;
    (* ASYNC_REG="TRUE" *) reg [2:0] shared_release=0;
    always @(posedge pixel_clk or negedge resetn)
        if(!resetn)shared_release<=0;
        else shared_release<={shared_release[1:0],1'b1};
    wire shared_pixel_resetn=shared_release[2];
    reg wr_en=0;
    reg [11:0] wr_addr=0,rd_addr=0;
    reg [31:0] wr_data=0;
    wire [31:0] rd_data;
    reg [11:0] scan_x=0,scan_y=0;
    integer frame_number=0,epoch=0,epoch_start_frame=0;
    reg machine_ready=1;
    reg [15:0] expected_source_word[0:255];
    wire [4:0] level=epoch==0 ? 8+frame_number : 1+frame_number-epoch_start_frame;
    wire [15:0] machine_base_rgb565={level,level,1'b0,level};
    function automatic [15:0] manual_source(input [15:0] base,input integer x,y);
        integer r,g,b;
        begin
            r=(base[15:11]+x*3+y*5)&31;g=(base[10:5]+x*5+y*7)&63;b=(base[4:0]+x*7+y*3)&31;
            manual_source=base==0 ? 16'd0:{r[4:0],g[5:0],b[4:0]};
        end
    endfunction
    wire [15:0] machine_rgb565=MANUAL_CROP_TEST ?
        manual_source(machine_base_rgb565,((scan_x-VIEW_X)*SCALE_DEN)/SCALE_NUM,
            ((scan_y-VIEW_Y)*SCALE_DEN)/SCALE_NUM):machine_base_rgb565;
    wire vblank_start=scan_x==0 && scan_y==BLANK_Y;
    wire pixel_use;
    wire [15:0] pixel_rgb565;
    wire [31:0] awaddr[0:1],araddr[0:1];
    wire [3:0] awlen[0:1],arlen[0:1];
    wire [2:0] awsize[0:1],arsize[0:1];
    wire [1:0] awburst[0:1],arburst[0:1];
    wire [1:0] awvalid,awready,wlast,wvalid,wready,bready,arvalid,arready,rready;
    wire [63:0] wdata[0:1];
    wire [7:0] wstrb[0:1];
    reg [63:0] rdata[0:1];
    reg [1:0] bvalid=0,rvalid=0,rlast=0;
    multitini_video_framebuffer #(.DYNAMIC_DISPLAY(DYNAMIC_DISPLAY),.WIDTH(WIDTH),.HEIGHT(HEIGHT),.VIEW_X(VIEW_X),.VIEW_Y(VIEW_Y),
        .SCALE_NUM(SCALE_NUM),.SCALE_DEN(SCALE_DEN),.SHARED_PIXEL_RESET(1)) dut(
        .display_filter_scale_num(4'(SCALE_NUM)),.display_filter_scale_den(4'(SCALE_DEN)),
        .display_pause(display_pause),.display_quiescent(display_quiescent),
        .display_view_x(12'(READER_X)),.display_view_y(12'(READER_Y)),
        .display_view_width(12'(READER_WIDTH)),.display_view_height(12'(READER_HEIGHT)),
        .display_scale_num(4'(READER_NUM)),.display_scale_den(4'(READER_DEN)),
        .capture_display_view_x(12'(VIEW_X)),.capture_display_view_y(12'(VIEW_Y)),
        .capture_display_view_width(12'(OUT_WIDTH)),.capture_display_view_height(12'(OUT_HEIGHT)),
        .capture_display_scale_num(4'(SCALE_NUM)),.capture_display_scale_den(4'(SCALE_DEN)),
        .display_h_active(12'(H_TOTAL)),.display_v_active(12'(V_TOTAL)),
        .display_h_total(12'(H_TOTAL)),.display_v_total(12'(V_TOTAL)),
        .display_source_x0(10'(READER_X<0 ? -READER_X*READER_DEN/READER_NUM:0)),
        .display_source_y0(10'(READER_Y<0 ? -READER_Y*READER_DEN/READER_NUM:0)),
        .display_phase_x0(4'(READER_X<0 ? -READER_X*READER_DEN%READER_NUM:0)),
        .display_phase_y0(4'(READER_Y<0 ? -READER_Y*READER_DEN%READER_NUM:0)),
        .clk(clk),.resetn(resetn),.wr_en(wr_en),.wr_addr(wr_addr),.rd_addr(rd_addr),
        .wr_data(wr_data),.wr_strb(4'hf),.rd_data(rd_data),
        .pixel_clk(pixel_clk),.pixel_resetn(shared_pixel_resetn && !display_pixel_hold),.shared_pixel_resetn(shared_pixel_resetn),
        .vblank_start(vblank_start),.machine_ready(machine_ready),.scan_x(scan_x),.scan_y(scan_y),
        .machine_rgb565(machine_rgb565),.pixel_use(pixel_use),.pixel_rgb565(pixel_rgb565),
        .awaddr(awaddr[0]),.awlen(awlen[0]),.awsize(awsize[0]),.awburst(awburst[0]),.awvalid(awvalid[0]),.awready(awready[0]),
        .wdata(wdata[0]),.wstrb(wstrb[0]),.wlast(wlast[0]),.wvalid(wvalid[0]),.wready(wready[0]),
        .bresp(2'b0),.bvalid(bvalid[0]),.bready(bready[0]),
        .araddr(araddr[0]),.arlen(arlen[0]),.arsize(arsize[0]),.arburst(arburst[0]),.arvalid(arvalid[0]),.arready(arready[0]),
        .rdata(rdata[0]),.rresp(2'b0),.rlast(rlast[0]),.rvalid(rvalid[0]),.rready(rready[0]),
        .fx_awaddr(awaddr[1]),.fx_awlen(awlen[1]),.fx_awsize(awsize[1]),.fx_awburst(awburst[1]),.fx_awvalid(awvalid[1]),.fx_awready(awready[1]),
        .fx_wdata(wdata[1]),.fx_wstrb(wstrb[1]),.fx_wlast(wlast[1]),.fx_wvalid(wvalid[1]),.fx_wready(wready[1]),
        .fx_bresp(2'b0),.fx_bvalid(bvalid[1]),.fx_bready(bready[1]),
        .fx_araddr(araddr[1]),.fx_arlen(arlen[1]),.fx_arsize(arsize[1]),.fx_arburst(arburst[1]),.fx_arvalid(arvalid[1]),.fx_arready(arready[1]),
        .fx_rdata(rdata[1]),.fx_rresp(2'b0),.fx_rlast(rlast[1]),.fx_rvalid(rvalid[1]),.fx_rready(rready[1]));
    always @(posedge pixel_clk)begin
        if(!shared_pixel_resetn)begin scan_x<=0;scan_y<=0;frame_number<=0;end
        else if(scan_x==H_TOTAL-1)begin
            scan_x<=0;
            if(scan_y==V_TOTAL-1)begin scan_y<=0;frame_number<=frame_number+1;end
            else scan_y<=scan_y+1'b1;
        end else scan_x<=scan_x+1'b1;
    end
    always @(posedge pixel_clk)begin
        #1;
        if(shared_pixel_resetn && scan_x==0 && scan_y==0)
            expected_source_word[frame_number]=machine_ready ? machine_base_rgb565 : 16'b0;
    end

    reg [7:0] memory[0:8*SLOT_BYTES+PIXELS*4-1];
    integer capture_frame[0:3];
    reg history_read_seen[0:PIXELS-1];
    integer ticks=0,port,lane,idx,slot,beat_count=0;
    reg [1:0] reading=0,writing=0,response_pending=0;
    reg [31:0] read_address[0:1],write_address[0:1];
    integer reads_left[0:1],writes_left[0:1],response_delay[0:1];
    reg hold_engine_response=0;
    reg [31:0] previous_drops=0,previous_underruns=0;
    always @(posedge clk)if(resetn)begin
        if(dut.capture_drops!=previous_drops || dut.underruns!=previous_underruns)
            $display("Pipeline diagnostic at %0t frame%0d xy%0d,%0d control%0d drops%0d underruns%0d capture_state%0d reader_enabled%0d",
                $time,frame_number,scan_x,scan_y,dut.control,dut.capture_drops,dut.underruns,dut.capture_i.state,dut.reader_i.enabled_pixel);
        previous_drops<=dut.capture_drops;previous_underruns<=dut.underruns;
    end
    function automatic integer memory_index(input [31:0] address);
        integer region,offset;
        begin
            if(address>=32'h22000000 && address<32'h22800000)begin
                region=(address-32'h22000000)>>20;offset=address&32'h000fffff;
                if(offset>=SLOT_BYTES)$fatal(1,"DDR slot bounds: %h",address);
                memory_index=region*SLOT_BYTES+offset;
            end else if(address>=32'h22800000 && address<32'h22800000+PIXELS*4)
                memory_index=8*SLOT_BYTES+address-32'h22800000;
            else begin $fatal(1,"DDR address outside video ownership: %h",address);memory_index=0;end
        end
    endfunction
    function automatic [63:0] read_memory(input [31:0] address);
        integer k;
        begin for(k=0;k<8;k=k+1)read_memory[k*8+:8]=memory[memory_index(address+k)];end
    endfunction
    genvar p;
    generate for(p=0;p<2;p=p+1)begin: memory_ports
        assign arready[p]=resetn && !reading[p] && !rvalid[p] && ticks%11!=p;
        assign awready[p]=resetn && !writing[p] && !response_pending[p] && !bvalid[p] && ticks%7!=p;
        assign wready[p]=resetn && writing[p] && ticks%4!=p;
    end endgenerate
    reg [1:0] held_ar=0,held_aw=0,held_w=0;
    reg [35:0] previous_ar[0:1],previous_aw[0:1];
    reg [72:0] previous_w[0:1];
    always @(posedge clk)begin
        ticks=ticks+1;
        if(resetn)for(port=0;port<2;port=port+1)begin
            if(held_ar[port] && (!arvalid[port] || {araddr[port],arlen[port]}!==previous_ar[port]))$fatal(1,"Stalled AR changed");
            if(held_aw[port] && (!awvalid[port] || {awaddr[port],awlen[port]}!==previous_aw[port]))$fatal(1,"Stalled AW changed");
            if(held_w[port] && (!wvalid[port] || {wlast[port],wstrb[port],wdata[port]}!==previous_w[port]))$fatal(1,"Stalled W changed");
            held_ar[port]=arvalid[port] && !arready[port];previous_ar[port]={araddr[port],arlen[port]};
            held_aw[port]=awvalid[port] && !awready[port];previous_aw[port]={awaddr[port],awlen[port]};
            held_w[port]=wvalid[port] && !wready[port];previous_w[port]={wlast[port],wstrb[port],wdata[port]};
            if(arvalid[port] && arready[port])begin
                if(arsize[port]!=3 || arburst[port]!=1 || araddr[port][2:0]!=0 ||
                   araddr[port][11:0]+(arlen[port]+1)*8>4096)$fatal(1,"Invalid AR burst");
                if(port==0 && (araddr[port]<32'h22400000 || araddr[port]>=32'h22800000))$fatal(1,"Reader outside output slots");
                if(port==1 && araddr[port]>=32'h22400000 && araddr[port]<32'h22800000)$fatal(1,"Engine reading output");
                read_address[port]<=araddr[port];reads_left[port]<=arlen[port]+1;reading[port]<=1;
            end
            if(rvalid[port] && rready[port])begin
                rvalid[port]<=0;read_address[port]<=read_address[port]+8;reads_left[port]<=reads_left[port]-1;
                if(rlast[port])reading[port]<=0;
            end
            if(reading[port] && !rvalid[port] && ticks%5!=port)begin
                rdata[port]<=read_memory(read_address[port]);rvalid[port]<=1;rlast[port]<=reads_left[port]==1;
                if(port==1 && read_address[port]>=32'h22800000)begin
                    idx=(read_address[port]-32'h22800000)/4;
                    history_read_seen[idx]=1;history_read_seen[idx+1]=1;
                end
            end
            if(awvalid[port] && awready[port])begin
                if(awsize[port]!=3 || awburst[port]!=1 || awaddr[port][2:0]!=0 ||
                   awaddr[port][11:0]+(awlen[port]+1)*8>4096)$fatal(1,"Invalid AW burst");
                if(port==0 && (awaddr[port]<32'h22000000 || awaddr[port]>=32'h22400000))$fatal(1,"Capture outside input slots");
                if(port==1 && awaddr[port]<32'h22400000)$fatal(1,"Engine writing capture slots");
                if(port==1 && awaddr[port]<32'h22800000 && dut.active_valid &&
                   ((awaddr[port]-32'h22400000)>>20)==dut.active_value[1:0])$fatal(1,"Engine overwrote displayed slot");
                write_address[port]<=awaddr[port];writes_left[port]<=awlen[port]+1;writing[port]<=1;
            end
            if(wvalid[port] && wready[port])begin
                if(wlast[port] !== (writes_left[port]==1))$fatal(1,"Incorrect WLAST");
                if(port==0 && write_address[port][19:0]==0)begin
                    slot=(write_address[port]-32'h22000000)>>20;capture_frame[slot]=frame_number;
                end
                for(lane=0;lane<8;lane=lane+1)if(wstrb[port][lane])begin
                    if(port==1 && write_address[port]>=32'h22800000 && !dut.engine_history_reset && dut.engine_options[1:0]!=0 &&
                       !history_read_seen[(write_address[port]-32'h22800000+lane)/4])$fatal(1,"History overwritten before read");
                    memory[memory_index(write_address[port]+lane)]=wdata[port][lane*8+:8];
                end
                beat_count=beat_count+1;write_address[port]<=write_address[port]+8;writes_left[port]<=writes_left[port]-1;
                if(wlast[port])begin writing[port]<=0;response_pending[port]<=1;response_delay[port]<=3;end
            end
            if(response_pending[port] && !(port==1 && hold_engine_response))begin
                if(response_delay[port]!=0)response_delay[port]<=response_delay[port]-1;
                else begin response_pending[port]<=0;bvalid[port]<=1;end
            end
            if(bvalid[port] && bready[port])bvalid[port]<=0;
            if(dut.engine_done && port==1 && (reading[1] || rvalid[1] || writing[1] || response_pending[1] || bvalid[1]))
                $fatal(1,"Engine DONE before accepted DDR work drained");
        end
    end

    // Flat-color frames make the independent oracle compact: every spatial
    // kernel preserves the color, then bloom adds its documented halo. RGB8
    // persistence is calculated here without looking at engine internals.
    reg [23:0] oracle_history=0;
    reg [7:0] oracle_options=0;
    reg oracle_valid=0;
    reg [15:0] expected_pixel[0:255];
    integer expected_frame[0:255],jobs=0,last_job_frame=-1,segment_jobs=0;
    integer seq,source_frame,k,maximum,coefficient,rr,gg,bb,pr,pg,pb,halo_shift;
    reg [15:0] current_word;
    reg [23:0] manual_history[0:PIXELS-1],manual_hblur[0:PIXELS-1],manual_hglow[0:PIXELS-1];
    reg [15:0] expected_native[0:255][0:PIXELS-1];
    integer manual_last_sequence=0;
    function automatic [23:0] manual_expand(input [15:0] word);
        begin manual_expand={{word[4:0],word[4:2]},{word[10:5],word[10:9]},{word[15:11],word[15:13]}};end
    endfunction
    function automatic integer clamp_x(input integer x);
        begin clamp_x=x<0 ? 0:x>=WIDTH ? WIDTH-1:x;end
    endfunction
    function automatic integer clamp_y(input integer y);
        begin clamp_y=y<0 ? 0:y>=HEIGHT ? HEIGHT-1:y;end
    endfunction
    task manual_oracle(input integer job_sequence,frame,input [7:0] effect_options,input bit history_reset);
        integer x,y,i,ch,tap,weight,radius,denominator,total,filtered,raw,halo;
        integer prior_r,prior_g,prior_b,source_r,source_g,source_b,peak,factor;
        reg [23:0] source,final_rgb;
        begin
            for(y=0;y<HEIGHT;y=y+1)for(x=0;x<WIDTH;x=x+1)begin
                i=y*WIDTH+x;source=manual_expand(manual_source(expected_source_word[frame],x,y));
                source_r=source[7:0];source_g=source[15:8];source_b=source[23:16];
                prior_r=history_reset ? 0:manual_history[i][7:0];
                prior_g=history_reset ? 0:manual_history[i][15:8];
                prior_b=history_reset ? 0:manual_history[i][23:16];
                peak=prior_r>prior_g ? prior_r:prior_g;if(prior_b>peak)peak=prior_b;
                case(effect_options[1:0])
                    1:factor=peak>=128 ? 14:peak>=32 ? 40:58;
                    2:factor=peak>=128 ? 18:peak>=32 ? 48:60;
                    3:factor=peak>=128 ? 24:peak>=32 ? 56:61;
                    default:factor=0;
                endcase
                prior_r=prior_r*factor/64;prior_g=prior_g*factor/64;prior_b=prior_b*factor/64;
                if(prior_r<4 && prior_g<4 && prior_b<4)begin prior_r=0;prior_g=0;prior_b=0;end
                if(prior_r>source_r)source_r=prior_r;if(prior_g>source_g)source_g=prior_g;if(prior_b>source_b)source_b=prior_b;
                manual_history[i]={source_b[7:0],source_g[7:0],source_r[7:0]};
            end
            for(y=0;y<HEIGHT;y=y+1)for(x=0;x<WIDTH;x=x+1)begin
                i=y*WIDTH+x;manual_hblur[i]=0;manual_hglow[i]=0;
                for(ch=0;ch<3;ch=ch+1)begin
                    radius=effect_options[3:2]==3 ? 2:1;denominator=radius==2 ? 8:4;total=0;
                    for(tap=-radius;tap<=radius;tap=tap+1)begin
                        weight=radius==1 ? (tap==0 ? 2:1):(tap==-2 || tap==2 ? 1:2);
                        total=total+weight*manual_history[y*WIDTH+clamp_x(x+tap)][ch*8+:8];
                    end
                    filtered=effect_options[3:2]==0 ? manual_history[i][ch*8+:8]:(total+denominator/2)/denominator;
                    manual_hblur[i][ch*8+:8]=filtered;
                    total=manual_history[y*WIDTH+clamp_x(x-1)][ch*8+:8]+2*manual_history[i][ch*8+:8]+manual_history[y*WIDTH+clamp_x(x+1)][ch*8+:8];
                    manual_hglow[i][ch*8+:8]=total/4;
                end
            end
            for(y=0;y<HEIGHT;y=y+1)for(x=0;x<WIDTH;x=x+1)begin
                i=y*WIDTH+x;final_rgb=0;
                for(ch=0;ch<3;ch=ch+1)begin
                    filtered=manual_hblur[i][ch*8+:8];
                    if(effect_options[3:2]>=2)filtered=(manual_hblur[clamp_y(y-1)*WIDTH+x][ch*8+:8]+2*manual_hblur[i][ch*8+:8]+manual_hblur[clamp_y(y+1)*WIDTH+x][ch*8+:8]+2)/4;
                    raw=manual_history[i][ch*8+:8];denominator=effect_options[3:2]==1 ? 8:effect_options[3:2]==2 ? 4:2;
                    if(effect_options[3:2]!=0)filtered=((denominator-1)*raw+filtered+denominator/2)/denominator;
                    halo=(manual_hglow[clamp_y(y-1)*WIDTH+x][ch*8+:8]+2*manual_hglow[i][ch*8+:8]+manual_hglow[clamp_y(y+1)*WIDTH+x][ch*8+:8])/4;
                    if(effect_options[5:4]!=0)filtered=filtered+(halo>>(4-effect_options[5:4]));
                    if(filtered>255)filtered=255;final_rgb[ch*8+:8]=filtered;
                end
                expected_native[job_sequence][i]={final_rgb[7:3],final_rgb[15:10],final_rgb[23:19]};
            end
            manual_last_sequence=job_sequence;
        end
    endtask
    integer native_check,output_index,history_index;
    always @(posedge clk)if(resetn && MANUAL_CROP_TEST && dut.engine_done)begin
        // Hidden rows/columns remain complete native outputs and persisted
        // history in DDR, even though scanout starts at a cropped sample.
        for(native_check=0;native_check<PIXELS;native_check=native_check+1)begin
            output_index=(4+dut.engine_output_slot)*SLOT_BYTES+(native_check/WIDTH)*STRIDE+(native_check%WIDTH)*2;
            history_index=8*SLOT_BYTES+native_check*4;
            if({memory[output_index+1],memory[output_index]}!==expected_native[manual_last_sequence][native_check])
                $fatal(1,"Full native output differs under crop at pixel%0d",native_check);
            if({memory[history_index+2],memory[history_index+1],memory[history_index]}!==manual_history[native_check])
                $fatal(1,"Hidden native history differs under crop at pixel%0d",native_check);
        end
    end
    always @(posedge clk)if(resetn && dut.engine_start)begin
        seq=dut.scheduler_i.job_value[31:2];source_frame=capture_frame[dut.engine_input_slot];
        if(segment_jobs!=0 && source_frame!=last_job_frame+1)$fatal(1,"Capture order skipped: %0d after %0d",source_frame,last_job_frame);
        if(dut.engine_history_reset!==(!oracle_valid || oracle_options!=dut.engine_options))$fatal(1,"Incorrect history reset on start/options/restart");
        for(k=0;k<PIXELS;k=k+1)history_read_seen[k]=0;
        idx=dut.engine_input_slot*SLOT_BYTES;
        current_word={memory[idx+1],memory[idx]};
        if(current_word!==(MANUAL_CROP_TEST ? manual_source(expected_source_word[source_frame],0,0):expected_source_word[source_frame]))
            $fatal(1,"Native capture did not match ready/black source frame%0d got%h expected%h",source_frame,current_word,expected_source_word[source_frame]);
        for(k=0;k<PIXELS;k=k+1)begin
            idx=dut.engine_input_slot*SLOT_BYTES+(k/WIDTH)*STRIDE+(k%WIDTH)*2;
            if({memory[idx+1],memory[idx]}!==(MANUAL_CROP_TEST ? manual_source(expected_source_word[source_frame],k%WIDTH,k/WIDTH):current_word))
                $fatal(1,"Capture frame contains torn or cropped native pixels");
        end
        if(MANUAL_CROP_TEST)manual_oracle(seq,source_frame,dut.engine_options,dut.engine_history_reset);
        else begin
        rr={current_word[15:11],current_word[15:13]};gg={current_word[10:5],current_word[10:9]};bb={current_word[4:0],current_word[4:2]};
        pr=oracle_history[7:0];pg=oracle_history[15:8];pb=oracle_history[23:16];
        maximum=pr>pg?pr:pg;if(pb>maximum)maximum=pb;
        case(dut.engine_options[1:0])
            1:coefficient=maximum>=128?14:maximum>=32?40:58;
            2:coefficient=maximum>=128?18:maximum>=32?48:60;
            3:coefficient=maximum>=128?24:maximum>=32?56:61;
            default:coefficient=0;
        endcase
        if(dut.engine_history_reset)coefficient=0;
        pr=(pr*coefficient)>>6;pg=(pg*coefficient)>>6;pb=(pb*coefficient)>>6;
        if(pr<4 && pg<4 && pb<4)begin pr=0;pg=0;pb=0;end
        if(pr>rr)rr=pr;if(pg>gg)gg=pg;if(pb>bb)bb=pb;
        oracle_history={bb[7:0],gg[7:0],rr[7:0]};
        // Every normalized Blur kernel and its blend preserve a flat field
        // exactly. Glow is independently generated from oracle_history.
        if(dut.engine_options[5:4]!=0)begin
            halo_shift=4-dut.engine_options[5:4];rr=rr+(oracle_history[7:0]>>halo_shift);gg=gg+(oracle_history[15:8]>>halo_shift);bb=bb+(oracle_history[23:16]>>halo_shift);
        end
        if(rr>255)rr=255;if(gg>255)gg=255;if(bb>255)bb=255;
        expected_pixel[seq]={rr[7:3],gg[7:2],bb[7:3]};expected_frame[seq]=source_frame;
        end
        expected_frame[seq]=source_frame;
        oracle_valid=1;oracle_options=dut.engine_options;last_job_frame=source_frame;
        jobs=jobs+1;segment_jobs=segment_jobs+1;
    end
    reg check_running=0,display_started=0;
    integer displayed=0,segment_displayed=0,checked_pixels=0,last_display_frame=-1,last_sequence=0,latency=-1;
    integer pipeline_latency=-1;
    integer frame_sequence=0,frame_pixels=0;
    integer expected_native_index;
    reg [15:0] displayed_expected_pixel;
    always @(posedge pixel_clk)begin
        #1;
        if(shared_pixel_resetn && check_running)begin
            if(scan_x==DISPLAY_X0 && scan_y==DISPLAY_Y0)begin
                if(pixel_use)begin
                    frame_sequence=dut.active_value[31:2];
                    if(display_started && (frame_sequence!=last_sequence+1 || expected_frame[frame_sequence]!=last_display_frame+1))
                        $fatal(1,"Display skipped/repeated frame seq%0d source%0d after seq%0d source%0d",frame_sequence,expected_frame[frame_sequence],last_sequence,last_display_frame);
                    if(!display_started)begin
                        latency=frame_number-expected_frame[frame_sequence];
                        if(pipeline_latency<0)pipeline_latency=latency;
                        else if(latency!=pipeline_latency)$fatal(1,"Restart changed pipeline latency");
                    end
                    if(latency<1 || frame_number-expected_frame[frame_sequence]!=latency)$fatal(1,"Display latency changed");
                    display_started=1;last_sequence=frame_sequence;last_display_frame=expected_frame[frame_sequence];
                    frame_pixels=0;displayed=displayed+1;segment_displayed=segment_displayed+1;
                end else if(display_started)$fatal(1,"Processed output missing for a display tick");
            end
            if(display_started && scan_x>=DISPLAY_X0 && scan_x<DISPLAY_X1 && scan_y>=DISPLAY_Y0 && scan_y<DISPLAY_Y1)begin
                expected_native_index=((scan_y-READER_Y)*READER_DEN/READER_NUM)*WIDTH+((scan_x-READER_X)*READER_DEN/READER_NUM);
                displayed_expected_pixel=MANUAL_CROP_TEST ? expected_native[frame_sequence][expected_native_index]:expected_pixel[frame_sequence];
                if(!pixel_use || pixel_rgb565!==displayed_expected_pixel)
                    $fatal(1,"Processed frame pixel mismatch frame%0d xy%0d,%0d seq%0d got%h expected%h use%0d",frame_number,scan_x,scan_y,frame_sequence,pixel_rgb565,displayed_expected_pixel,pixel_use);
                frame_pixels=frame_pixels+1;checked_pixels=checked_pixels+1;
                if(scan_x==DISPLAY_X1-1 && scan_y==DISPLAY_Y1-1 && frame_pixels!=DISPLAY_PIXELS)
                    $fatal(1,"Incomplete displayed frame");
            end
            if(pixel_use && !(scan_x>=DISPLAY_X0 && scan_x<DISPLAY_X1 && scan_y>=DISPLAY_Y0 && scan_y<DISPLAY_Y1))
                $fatal(1,"Processed pixel escaped viewport");
        end
    end
    task write_reg(input [11:0] address,input [31:0] data);
        begin @(negedge clk);wr_en=1;wr_addr=address;wr_data=data;@(negedge clk);wr_en=0;end
    endtask
    task assert_clean;
        begin
            if(dut.capture_drops!=dut.drop_base || dut.capture_errors!=dut.capture_error_base ||
               dut.read_errors!=dut.read_error_base || dut.underruns!=dut.underrun_base ||
               dut.deadline_misses || dut.engine_errors || dut.scheduler_error || dut.bad_write)
                $fatal(1,"Pipeline diagnostics drops%0d capture%0d read%0d underrun%0d deadline%0d engine%0d scheduler%0d badwrite%0d",
                    dut.capture_drops,dut.capture_errors,dut.read_errors,dut.underruns,dut.deadline_misses,dut.engine_errors,dut.scheduler_error,dut.bad_write);
        end
    endtask
    task startup_disable_at_capture_completion;
        integer timeout_cycles,traffic_at_quiet;
        begin
            write_reg(12'h180,7);
            timeout_cycles=0;
            // The following edge completes the first capture. No output has
            // been submitted, so reader drain cannot hide a false QUIESCENT.
            @(negedge clk);
            while(!(dut.capture_i.state==1 && dut.capture_i.ended &&
                    dut.capture_i.completed_bursts==HEIGHT && !dut.capture_i.fifo_read &&
                    dut.capture_i.staged!=16 && !dut.capture_i.failed))begin
                @(negedge clk);timeout_cycles=timeout_cycles+1;
                if(timeout_cycles>100000)$fatal(1,"Could not align startup capture completion");
            end
            if(dut.engine_busy || dut.active_valid || dut.scheduler_i.cadence)
                $fatal(1,"Startup-disable race was not isolated before presentation");
            wr_en=1;wr_addr=12'h180;wr_data=0;rd_addr=12'h184;
            @(posedge clk);#1;
            if(dut.capture_i.state!=0 || !dut.capture_enable)
                $fatal(1,"Did not reach the registered-enable completion window");
            if(rd_data[3] || dut.all_quiet)
                $fatal(1,"QUIESCENT advertised before registered capture enable fell");
            @(negedge clk);wr_en=0;
            timeout_cycles=0;
            while(!dut.all_quiet)begin
                @(negedge clk);timeout_cycles=timeout_cycles+1;
                if(timeout_cycles>100000)$fatal(1,"Startup disable did not finish draining");
            end
            traffic_at_quiet=beat_count;
            repeat(1000)begin
                @(negedge clk);
                if(!dut.all_quiet || dut.capture_i.arm_launch || awvalid || arvalid ||
                   reading || writing || response_pending || rvalid || bvalid || beat_count!=traffic_at_quiet)
                    $fatal(1,"Capture rearmed or DDR resumed after startup QUIESCENT");
            end
            write_reg(12'h1ac,1);assert_clean();
            $display("PASS startup disable: final capture FILL-to-IDLE aligned; no premature QUIESCENT or later arming/DDR");
        end
    endtask
    integer saved_beats,wait_cycles,pause_frame,source_pause_frames=0,before_options;
    initial begin
        for(k=0;k<8*SLOT_BYTES+PIXELS*4;k=k+1)memory[k]=8'hcd;
        for(k=0;k<4;k=k+1)capture_frame[k]=-1;
        repeat(20)@(negedge clk);resetn=1;
        repeat(100)@(negedge clk);
        startup_disable_at_capture_completion();
        write_reg(12'h1b0,0);write_reg(12'h180,7);check_running=1;
        wait(segment_displayed>=3);assert_clean();
        if(SOURCE_PAUSE_TEST)begin
            // A paused/resetting core still owes one native image per output
            // period: the wrapper captures black while the raster continues.
            // Start/end in blanking so complete source frames are unambiguous.
            wait(vblank_start);@(negedge pixel_clk);machine_ready=0;
            pause_frame=frame_number;
            if(EXPECT_SOURCE_FAILURE)check_running=0;
            wait(frame_number>=pause_frame+4 && vblank_start);
            @(negedge pixel_clk);machine_ready=1;source_pause_frames=4;
            wait(frame_number>=pause_frame+6 && vblank_start);@(negedge pixel_clk);
            if(EXPECT_SOURCE_FAILURE)begin
                if(!dut.scheduler_error || dut.capture_drops==dut.drop_base)
                    $fatal(1,"Expected old source-unready failure was not reproduced");
                $display("PASS source-unready failure reproduced: scale%0d/%0d held low4 frames then recovered2; drops%0d sticky%0d jobs%0d displays%0d",
                    SCALE_NUM,SCALE_DEN,dut.capture_drops,dut.scheduler_error,jobs,displayed);
                $finish;
            end
            assert_clean();
        end
        before_options=segment_displayed;write_reg(12'h1b0,8'b01_01_10_11);
        wait(segment_displayed>=before_options+3);assert_clean();write_reg(12'h1b0,8'b11_11_11_11);
        wait(segment_displayed>=before_options+6);assert_clean();
        // Stop after a job owns an accepted write. Its B response remains
        // pending across disable; quiescence must retain that ownership.
        wait(response_pending[1]);@(negedge clk);hold_engine_response=1;check_running=0;
        if(DYNAMIC_DISPLAY)display_pause=1;else write_reg(12'h180,0);
        repeat(80)@(negedge clk);
        if(!dut.engine_busy || dut.all_quiet)$fatal(1,"Disable cancelled an accepted engine response");
        hold_engine_response=0;
        wait_cycles=0;
        while(!dut.all_quiet && wait_cycles<100000)begin @(negedge clk);wait_cycles=wait_cycles+1;end
        if(!dut.all_quiet)$fatal(1,"Pipeline did not drain after disable");
        rd_addr=12'h184;#1;if(DYNAMIC_DISPLAY ? !display_quiescent : !rd_data[3])$fatal(1,"Public QUIESCENT status disagrees with drain");
        if(reading || writing || response_pending || rvalid || bvalid)$fatal(1,"Quiescent before AXI drain");
        saved_beats=beat_count;repeat(1000)@(negedge clk);
        if(beat_count!=saved_beats || arvalid || awvalid)$fatal(1,"New DDR traffic after quiescence");
        if(dut.capture_errors || dut.read_errors || dut.deadline_misses || dut.engine_errors || dut.scheduler_error || dut.bad_write)
            $fatal(1,"Disable drain caused an AXI/deadline/control fault");
        // Match firmware's restart sequence. Intentional disable can cancel
        // a partial capture/cache refill, counted separately from steady-run
        // drops. ERROR_CLEAR is legal only after every accepted AXI drained.
        if(DYNAMIC_DISPLAY)begin
            display_pixel_hold=1;repeat(100)@(negedge clk);
            if(!display_quiescent || beat_count!=saved_beats)$fatal(1,"DMA resumed during pixel reset");
        end else write_reg(12'h1ac,1);
        assert_clean();
        rd_addr=12'h184;#1;if(rd_data[4])$fatal(1,"Public error status survived disabled ERROR_CLEAR");
        // Restart at a much darker source level; retaining old persistence
        // would visibly contaminate the first restarted output.
        epoch=1;epoch_start_frame=frame_number;oracle_valid=0;segment_jobs=0;
        display_started=0;segment_displayed=0;latency=-1;
        write_reg(12'h1b0,8'b11_11_11_11);
        if(DYNAMIC_DISPLAY)begin display_pixel_hold=0;display_pause=0;end
        else write_reg(12'h180,7);
        check_running=1;
        wait(segment_displayed>=6);wait(frame_pixels==DISPLAY_PIXELS);#1;assert_clean();
        $display("PASS autonomous frame pipeline: scale%0d/%0d jobs%0d displayed%0d pixels%0d latency%0d paused-source%0d; ordered/live options/drain/restart/history",
            SCALE_NUM,SCALE_DEN,jobs,displayed,checked_pixels,latency,source_pause_frames);
        if(MANUAL_CROP_TEST)$display("PASS manual crop pipeline: Auto capture%0d/%0d full native%0dx%0d, reader3/1 origin-4,-5 source1,1 phase1,2; all hidden output/history and first/last visible pixels",
            SCALE_NUM,SCALE_DEN,WIDTH,HEIGHT);
        $finish;
    end
    initial begin #15000000;$fatal(1,"Autonomous pipeline timeout frame%0d jobs%0d displays%0d",frame_number,jobs,displayed);end
endmodule
