// SPDX-License-Identifier: GPL-3.0-only
// Independent native-rate MiSTer pairs; shared address ownership only.
module multitini_c64_sid_array #(
    parameter integer SID_COUNT=2, FAST_PORT=0, EDGE_STAGE=1
) (
    input wire reset,clk,ce_1m,
    input wire [1:0] cs, input wire we, input wire [11:0] addr,
    input wire [7:0] data_in, output wire [7:0] data_out,
    input wire [1:0] fio_cs,input wire fio_we,fio_latch,
    input wire [1:0] fio_latch_src,input wire [9:0] fio_addr,
    input wire [7:0] fio_wdata,input wire [1:0] fast_rd_cs,
    input wire [9:0] fast_rd_addr,output wire [7:0] fast_rd_data,
    input wire [12:0] fc_offset_l,fc_offset_r,
    input wire [7:0] pot_x_l,pot_y_l,pot_x_r,pot_y_r,
    input wire [17:0] ext_in_l,ext_in_r,
    output wire [17:0] audio_l,audio_r,
    input wire [1:0] filter_en,mode,input wire [3:0] cfg,
    input wire ld_clk,input wire [11:0] ld_addr,
    input wire [15:0] ld_data,input wire ld_wr,
    input wire map_enable,io_select,
    input wire [255:0] slot_masks,input wire [31:0] write_inhibit,
    input wire [95:0] owners,input wire [127:0] io_masks,
    input wire [255:0] chip_params,output wire [143:0] chip_audio
);
    initial if(SID_COUNT!=2 && SID_COUNT!=4 && SID_COUNT!=8)
        $error("SID_COUNT must be 2, 4 or 8");
    wire [7:0] native_mask=io_select ? io_masks[addr[8:5]*8+:8] : slot_masks[addr[9:5]*8+:8];
    wire native_inhibit=!io_select && write_inhibit[addr[9:5]];
    wire [7:0] fast_mask=slot_masks[fio_addr[9:5]*8+:8];
    wire [2:0] fast_owner=owners[fast_rd_addr[9:5]*3+:3];
    wire [2:0] latch_owner=owners[fio_addr[9:5]*3+:3];
    wire [2:0] paddle_owner=owners[2:0];
    wire [319:0] candidates;
    wire [39:0] selected=candidates[fast_owner*40+:40];
    reg [7:0] mapped_read;
    always @* case(fast_rd_addr[4:0])
        5'h19:mapped_read=selected[39:32]; 5'h1a:mapped_read=selected[31:24];
        5'h1b:mapped_read=selected[23:16]; 5'h1c:mapped_read=selected[15:8];
        default:mapped_read=selected[7:0];
    endcase
    wire [31:0] pair_read,pair_fast;
    reg [7:0] mapped_native;
    integer k;
    always @* begin
        mapped_native=8'hff;
        for(k=SID_COUNT-1;k>=0;k=k-1)
            if(native_mask[k]) mapped_native=candidates[k*40+:8];
    end
    assign data_out=map_enable ? mapped_native : pair_read[7:0];
    assign fast_rd_data=map_enable ? mapped_read : pair_fast[7:0];
    assign audio_l=chip_audio[17:0]; assign audio_r=chip_audio[35:18];
    for(genvar p=0;p<4;p=p+1) begin : pairs
        if(p<SID_COUNT/2) begin : present
            reg active=0;
            reg used;
            always @* begin
                used=0;
                for(integer j=0;j<32;j=j+1) if(!write_inhibit[j] && (|slot_masks[j*8+p*2+:2])) used=1;
                for(integer j=0;j<16;j=j+1) if(|io_masks[j*8+p*2+:2]) used=1;
            end
            // Register writes can land immediately after a paused table swap.
            // Oscillators/envelopes and filter scheduling still start at CE.
            // Separating these resets prevents losing the first FIO window.
            always @(posedge clk) if(reset || !map_enable || !used) active<=0; else if(ce_1m) active<=1;
            wire pair_reset=reset || (p!=0 && (!map_enable || !used));
            wire engine_reset=pair_reset || (p!=0 && !active && !ce_1m);
            wire [1:0] ncs=map_enable ?
                ({2{((|cs)||io_select) && !(we&&native_inhibit)}} & native_mask[p*2+:2]) : (p==0?cs:2'b00);
            wire [1:0] fcs=map_enable ?
                ({2{(fio_latch || (fio_we&&!write_inhibit[fio_addr[9:5]])) && (|fio_cs)}} & fast_mask[p*2+:2]) : (p==0?fio_cs:2'b00);
            wire [1:0] src=map_enable ? {latch_owner==p*2+1,latch_owner==p*2} : fio_latch_src;
            wire [1:0] mdl=map_enable ? {chip_params[(p*2+1)*32+16],chip_params[p*64+16]} : mode;
            wire [1:0] dig=map_enable ? {chip_params[(p*2+1)*32+17],chip_params[p*64+17]} : 2'b00;
            sid_top #(.MULTI_FILTERS(0),.DUAL(1),.FAST_PORT(FAST_PORT),.EDGE_STAGE(EDGE_STAGE),.SEPARATE_RESETS(1)) sid (
                .reset(pair_reset),.engine_reset(engine_reset),.clk(clk),.ce_1m(ce_1m),.cs(ncs),.we(we),.addr(addr[4:0]),.data_in(data_in),.data_out(pair_read[p*8+:8]),
                .fio_cs(fcs),.fio_we(fio_we),.fio_latch(fio_latch),.fio_latch_src(src),.fio_addr(fio_addr[4:0]),.fio_wdata(fio_wdata),
                .fast_rd_cs(p==0?fast_rd_cs:2'b00),.fast_rd_addr(fast_rd_addr[4:0]),.fast_rd_data(pair_fast[p*8+:8]),.fast_candidates(candidates[p*80+:80]),
                .fc_offset_l(fc_offset_l),.fc_offset_r(fc_offset_r),
                .pot_x_l(map_enable?(paddle_owner==p*2?pot_x_l:8'hff):pot_x_l),.pot_y_l(map_enable?(paddle_owner==p*2?pot_y_l:8'hff):pot_y_l),
                .pot_x_r(map_enable?(paddle_owner==p*2+1?pot_x_l:8'hff):pot_x_r),.pot_y_r(map_enable?(paddle_owner==p*2+1?pot_y_l:8'hff):pot_y_r),
                .ext_in_l(map_enable?{mdl[0]&dig[0],17'b0}:ext_in_l),.ext_in_r(map_enable?{mdl[1]&dig[1],17'b0}:ext_in_r),
                .audio_l(chip_audio[p*36+:18]),.audio_r(chip_audio[p*36+18+:18]),
                .filter_en(filter_en),.mode(mdl),.cfg(map_enable?4'b0:cfg),.ld_clk(ld_clk),.ld_addr(ld_addr),.ld_data(ld_data),.ld_wr(ld_wr));
        end else begin : absent
            assign candidates[p*80+:80]=0; assign chip_audio[p*36+:36]=0;
            assign pair_read[p*8+:8]=8'hff; assign pair_fast[p*8+:8]=8'hff;
        end
    end
endmodule
