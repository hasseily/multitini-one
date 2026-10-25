// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_sid_array_legacy;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,map_enable=0,io_select=0,we=0,fio_we=0,fio_latch=0;
    reg [4:0] phase=0;always @(posedge clk)phase<=phase+1'b1;
    wire ce_1m=phase==0;
    reg [1:0] cs=0,fio_cs=0,fio_latch_src=0,fast_rd_cs=0;
    reg [11:0] addr=0;reg [9:0] fio_addr=0,fast_rd_addr=0;
    reg [7:0] data_in=0,fio_wdata=0;wire [7:0] data_out,fast_rd_data;
    reg [255:0] slot_masks=0,chip_params=0;reg [95:0] owners=0;
    reg [31:0] write_inhibit=0;reg [127:0] io_masks=0;wire [143:0] chip_audio;
    wire [17:0] audio_l,audio_r;
    reg [1:0] model=0;
    wire [7:0] ref_data,ref_fast,mapped_data,mapped_fast;
    wire [17:0] ref_l,ref_r,mapped_l,mapped_r;
    wire [143:0] mapped_audio;
    multitini_c64_sid_array #(.SID_COUNT(8),.FAST_PORT(1)) dut(
        .clk(clk),.reset(reset),.ce_1m(ce_1m),.cs(cs),.we(we),.addr(addr),.data_in(data_in),.data_out(data_out),
        .fio_cs(fio_cs),.fio_we(fio_we),.fio_latch(fio_latch),.fio_latch_src(fio_latch_src),.fio_addr(fio_addr),.fio_wdata(fio_wdata),
        .fast_rd_cs(fast_rd_cs),.fast_rd_addr(fast_rd_addr),.fast_rd_data(fast_rd_data),
        .fc_offset_l(13'd0),.fc_offset_r(13'd0),.pot_x_l(8'h12),.pot_y_l(8'h34),.pot_x_r(8'hff),.pot_y_r(8'hff),
        .ext_in_l(18'd0),.ext_in_r(18'd0),.audio_l(audio_l),.audio_r(audio_r),.filter_en(2'b11),.mode(model),.cfg(4'b0),
        .ld_clk(clk),.ld_addr(12'd0),.ld_data(16'd0),.ld_wr(1'b0),.map_enable(map_enable),.io_select(io_select),
        .slot_masks(slot_masks),.write_inhibit(write_inhibit),.owners(owners),.io_masks(io_masks),.chip_params(chip_params),.chip_audio(chip_audio));
    sid_top #(.MULTI_FILTERS(0),.DUAL(1),.FAST_PORT(1),.EDGE_STAGE(1)) reference(
        .clk(clk),.reset(reset),.ce_1m(ce_1m),.cs(cs),.we(we),.addr(addr[4:0]),.data_in(data_in),.data_out(ref_data),
        .fio_cs(fio_cs),.fio_we(fio_we),.fio_latch(fio_latch),.fio_latch_src(fio_latch_src),.fio_addr(fio_addr[4:0]),.fio_wdata(fio_wdata),
        .fast_rd_cs(fast_rd_cs),.fast_rd_addr(fast_rd_addr[4:0]),.fast_rd_data(ref_fast),
        .fc_offset_l(13'd0),.fc_offset_r(13'd0),.pot_x_l(8'h12),.pot_y_l(8'h34),.pot_x_r(8'hff),.pot_y_r(8'hff),
        .ext_in_l(18'd0),.ext_in_r(18'd0),.audio_l(ref_l),.audio_r(ref_r),.filter_en(2'b11),.mode(model),.cfg(4'b0),
        .ld_clk(clk),.ld_addr(12'd0),.ld_data(16'd0),.ld_wr(1'b0),.engine_reset(reset));
    multitini_c64_sid_array #(.SID_COUNT(8),.FAST_PORT(1)) mapped(
        .clk(clk),.reset(reset),.ce_1m(ce_1m),.cs(cs),.we(we),.addr(addr),.data_in(data_in),.data_out(mapped_data),
        .fio_cs(fio_cs),.fio_we(fio_we),.fio_latch(fio_latch),.fio_latch_src(fio_latch_src),.fio_addr(fio_addr),.fio_wdata(fio_wdata),
        .fast_rd_cs(fast_rd_cs),.fast_rd_addr(fast_rd_addr),.fast_rd_data(mapped_fast),
        .fc_offset_l(13'd0),.fc_offset_r(13'd0),.pot_x_l(8'h12),.pot_y_l(8'h34),.pot_x_r(8'hff),.pot_y_r(8'hff),
        .ext_in_l(18'd0),.ext_in_r(18'd0),.audio_l(mapped_l),.audio_r(mapped_r),.filter_en(2'b11),.mode(model),.cfg(4'b0),
        .ld_clk(clk),.ld_addr(12'd0),.ld_data(16'd0),.ld_wr(1'b0),.map_enable(1'b1),.io_select(io_select),
        .slot_masks(slot_masks),.write_inhibit(write_inhibit),.owners(owners),.io_masks(io_masks),.chip_params(chip_params),.chip_audio(mapped_audio));
    wire signed [23:0] mixed_l,mixed_r;reg signed [17:0] ua_l=0,ua_r=0;
    multitini_c64_sid_mix mix(.clk(clk),.reset(reset),.ce_1m(ce_1m),.sample_tick(1'b0),.map_enable(1'b1),
        .chip_audio(mapped_audio),.chip_params(chip_params),.output_config(32'h8080),
        .ua_audio_l(ua_l),.ua_audio_r(ua_r),.audio_l(mixed_l),.audio_r(mixed_r),.clip());
    integer checks=0;reg [31:0] rng=32'h31ab8fd2;
    task automatic check(input bit ok,input string why);checks++;if(!ok)$fatal(1,"SID LEGACY %s phase=%0d",why,phase);endtask
    function automatic signed [23:0] sat(input signed [24:0] value);
        if(value>8388607)sat=8388607;else if(value< -8388608)sat=-8388608;else sat=value[23:0];
    endfunction
    initial begin
        chip_params=0;chip_params[7:0]=128;chip_params[47:40]=128;
        for(integer s=0;s<32;s++)begin slot_masks[s*8+:8]=(s&1)?2:1;owners[s*3+:3]=s&1;end
        for(integer m=0;m<2;m++)begin
            reset=1;repeat(64)@(negedge clk);reset=0;model=m?3:0;chip_params[16]=m;chip_params[48]=m;
            // Whole-edge equality, including random write-only registers,
            // readable registers, native priority, FIO latch and edge queues.
            for(integer t=0;t<131072;t++)begin
                @(negedge clk);rng=rng^(rng<<13);rng=rng^(rng>>17);rng=rng^(rng<<5);
                addr=12'h400+(rng[5:0]);fio_addr=rng[11:6];fast_rd_addr=rng[17:12];
                cs=rng[18]?(addr[5]?2:1):0;we=rng[19];data_in=rng[31:24];
                fio_cs=fio_addr[5]?2:1;fio_we=rng[20];fio_latch=!fio_we&&rng[21];
                fio_latch_src=fio_cs;fio_wdata=rng[23:16];fast_rd_cs=fast_rd_addr[5]?2:1;
                ua_l=$signed(rng[17:0]);ua_r=-ua_l;
                @(posedge clk);#1;
                check(data_out===ref_data&&fast_rd_data===ref_fast,"map disabled exact leaf reads");
                // Native output is only meaningful while its chip is selected.
                if(cs!=0)check(mapped_data===ref_data,"mapped legacy native reads");
                check(mapped_fast===ref_fast,"mapped legacy fast reads");
                if(t>64)begin
                    check({audio_l,audio_r}==={ref_l,ref_r},"map disabled every-edge audio");
                    check({mapped_l,mapped_r}==={ref_l,ref_r},"mapped legacy every-edge audio");
                    check(mixed_l===sat($signed({ref_l,6'b0})+$signed({ua_l,6'b0}))&&
                          mixed_r===sat($signed({ref_r,6'b0})+$signed({ua_r,6'b0})),"legacy signed board sum");
                end
                if(t>1024)check(!$isunknown({ref_l,ref_r,mapped_l,mapped_r,audio_l,audio_r}),"settled audio is defined");
            end
        end
        $display("PASS SID LEGACY checks=%0d clocks=262144 models=2",checks);$finish;
    end
    initial begin #5000000;$fatal(1,"SID legacy timeout");end
endmodule
