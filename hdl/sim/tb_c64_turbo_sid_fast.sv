// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
`define SID_REGS(d,c) {d.Voice_1_Freq[c],d.Voice_1_Pw[c],d.Voice_1_Control[c],d.Voice_1_Att_dec[c],d.Voice_1_Sus_Rel[c],d.Voice_2_Freq[c],d.Voice_2_Pw[c],d.Voice_2_Control[c],d.Voice_2_Att_dec[c],d.Voice_2_Sus_Rel[c],d.Voice_3_Freq[c],d.Voice_3_Pw[c],d.Voice_3_Control[c],d.Voice_3_Att_dec[c],d.Voice_3_Sus_Rel[c],d.Filter_Fc[c],d.Filter_Res_Filt[c],d.Filter_Mode_Vol[c]}
module tb_c64_turbo_sid_fast;
    reg clk=0,reset=1; always #5 clk=~clk;
    integer phase=0,tick=0,stage=0,samples=0,frame_cycles,model,ntsc,c;
    reg [7:0] config_bytes[2];
    wire ce_1m=phase==0;
    reg [1:0] native_cs=0, fast_cs=0;
    reg native_we=0,fast_we=0,fast_latch=0;
    reg [4:0] access_addr=0;
    reg [7:0] access_data=0;
    reg [1:0] select=0;
    integer landing=29;
    wire [17:0] stock_l,stock_r,fast_l,fast_r;
    wire [7:0] fast_read;
    sid_top #(.DUAL(1),.FAST_PORT(0)) stock(
        .clk(clk),.reset(reset),.ce_1m(ce_1m),.cs(native_cs),.we(native_we),.addr(access_addr),.data_in(access_data),.data_out(),
        .fc_offset_l(13'd0),.fc_offset_r(13'd0),.pot_x_l(8'h12),.pot_y_l(8'h34),.pot_x_r(8'h56),.pot_y_r(8'h78),
        .ext_in_l(18'd0),.ext_in_r(18'd0),.audio_l(stock_l),.audio_r(stock_r),.filter_en(2'b11),.mode(model?2'b11:2'b00),.cfg(4'd0),
        .ld_clk(clk),.ld_addr(12'd0),.ld_data(16'd0),.ld_wr(1'b0),
        .fio_cs(2'b00),.fio_we(1'b0),.fio_latch(1'b0),.fio_latch_src(2'b00),.fio_addr(5'd0),.fio_wdata(8'd0),.fast_rd_cs(2'b00),.fast_rd_addr(5'd0),.fast_rd_data());
    sid_top #(.DUAL(1),.FAST_PORT(1),.EDGE_STAGE(1)) fast(
        .clk(clk),.reset(reset),.ce_1m(ce_1m),.cs(2'b00),.we(1'b0),.addr(5'd0),.data_in(8'd0),.data_out(),
        .fc_offset_l(13'd0),.fc_offset_r(13'd0),.pot_x_l(8'h12),.pot_y_l(8'h34),.pot_x_r(8'h56),.pot_y_r(8'h78),
        .ext_in_l(18'd0),.ext_in_r(18'd0),.audio_l(fast_l),.audio_r(fast_r),.filter_en(2'b11),.mode(model?2'b11:2'b00),.cfg(4'd0),
        .ld_clk(clk),.ld_addr(12'd0),.ld_data(16'd0),.ld_wr(1'b0),
        .fio_cs(fast_cs),.fio_we(fast_we),.fio_latch(fast_latch),.fio_latch_src(select[0]?2'b01:2'b10),.fio_addr(access_addr),.fio_wdata(access_data),
        .fast_rd_cs(select),.fast_rd_addr(access_addr),.fast_rd_data(fast_read));
    task automatic set_access(input integer t);
        select=(t/32)%3+1;
        if(stage==0) begin
            access_addr=t%32;access_data=8'((t/96)*73+8'h31);
            landing=29;
        end else begin
            access_addr=t%25;access_data=8'((t*37)^(t>>7));
            landing=15+t%16;
            case(access_addr)
            4: access_data=(t/25)%3?8'h41:8'h40;
            11:access_data=(t/25)%5?8'h21:8'h29;
            18:access_data=(t/25)%7?8'h11:8'h81;
            23:access_data=8'((t/25)%8);
            24:access_data=(t/25)%2?8'h1f:8'h0f;
            default:begin end
            endcase
        end
    endtask
    always @(negedge clk) begin
        phase=(phase+1)%32;
        if(!reset) begin
            if(phase==0) begin tick=tick+1;set_access(tick);end
            native_cs=phase>=16?select:0;
            native_we=phase==29;
            fast_cs=phase==landing?select:0;
            fast_we=phase==landing;
        end else begin native_cs=0;native_we=0;fast_cs=0;fast_we=0;end
    end
    always @(posedge clk) if(!reset) begin
        if(phase==31) begin
            #1;
            for(c=0;c<2;c=c+1) begin
                if(`SID_REGS(stock,c) !== `SID_REGS(fast,c)) $fatal(1,"register mismatch stage=%0d tick=%0d chip=%0d offset=%02x",stage,tick,c,access_addr);
                if(stock.bus_data[c] !== fast.bus_data[c]) $fatal(1,"latch mismatch stage=%0d tick=%0d chip=%0d offset=%02x",stage,tick,c,access_addr);
            end
            if(stage==0 && tick==96*4) begin stage=1;tick=0;end
        end
        if(stage==1 && phase==15 && tick>=frame_cycles) begin
            if($isunknown({stock_l,stock_r,fast_l,fast_r}) || {stock_l,stock_r} !== {fast_l,fast_r})
                $fatal(1,"mapped audio mismatch tick=%0d stock=%x/%x fast=%x/%x",tick,stock_l,stock_r,fast_l,fast_r);
            samples=samples+1;
            if(samples==2*frame_cycles) begin
                $display("PASS SID fast register_accesses=384 audio_samples=%0d mode=%0d model=%0d",samples,ntsc,model);
                $finish;
            end
        end
    end
    initial begin
        $readmemh("case.hex",config_bytes);ntsc=config_bytes[0];model=config_bytes[1];frame_cycles=ntsc?65*263:63*312;
        repeat(96)@(negedge clk);#1;reset=0;
    end
    initial begin repeat(4*312*65*32)@(posedge clk);$fatal(1,"SID fast timeout");end
endmodule
`undef SID_REGS
