// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_sid_fanout;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,map_enable=0,io_select=0,we=0,fio_we=0,fio_latch=0;
    reg [4:0] phase=0;always @(posedge clk)phase<=phase+1'b1;
    reg ce_1m=0;
    reg [1:0] cs=0,fio_cs=0,fio_latch_src=0,fast_rd_cs=0;
    reg [11:0] addr=0;reg [9:0] fio_addr=0,fast_rd_addr=0;
    reg [7:0] data_in=0,fio_wdata=0;wire [7:0] data_out,fast_rd_data;
    reg [255:0] slot_masks=0,chip_params=0;reg [95:0] owners=0;
    reg [31:0] write_inhibit=0;reg [127:0] io_masks=0;wire [143:0] chip_audio;
    wire [17:0] audio_l,audio_r;
    multitini_c64_sid_array #(.SID_COUNT(8),.FAST_PORT(1)) dut(
        .clk(clk),.reset(reset),.ce_1m(ce_1m),.cs(cs),.we(we),.addr(addr),.data_in(data_in),.data_out(data_out),
        .fio_cs(fio_cs),.fio_we(fio_we),.fio_latch(fio_latch),.fio_latch_src(fio_latch_src),.fio_addr(fio_addr),.fio_wdata(fio_wdata),
        .fast_rd_cs(fast_rd_cs),.fast_rd_addr(fast_rd_addr),.fast_rd_data(fast_rd_data),
        .fc_offset_l(13'd0),.fc_offset_r(13'd0),.pot_x_l(8'h12),.pot_y_l(8'h34),.pot_x_r(8'hff),.pot_y_r(8'hff),
        .ext_in_l(18'd0),.ext_in_r(18'd0),.audio_l(audio_l),.audio_r(audio_r),.filter_en(2'b11),.mode(2'b00),.cfg(4'b0),
        .ld_clk(clk),.ld_addr(12'd0),.ld_data(16'd0),.ld_wr(1'b0),.map_enable(map_enable),.io_select(io_select),
        .slot_masks(slot_masks),.write_inhibit(write_inhibit),.owners(owners),.io_masks(io_masks),.chip_params(chip_params),.chip_audio(chip_audio));
    wire [63:0] controls,latches;wire [23:0] pending;
    for(genvar p=0;p<4;p++)for(genvar c=0;c<2;c++)begin
        assign controls[(p*2+c)*8+:8]=dut.pairs[p].present.sid.Voice_3_Control[c];
        assign latches[(p*2+c)*8+:8]=dut.pairs[p].present.sid.bus_data[c];
        assign pending[(p*2+c)*3+:3]=dut.pairs[p].present.sid.chip[c].ctrl_pending;
    end
    integer checks=0,fanout,first_chip,cancel_chip;reg [7:0] mask;
    task automatic check(input bit ok,input string why);checks++;if(!ok)$fatal(1,"SID FANOUT %s count=%0d first=%0d controls=%h",why,fanout,first_chip,controls);endtask
    task automatic step;@(posedge clk);#1;@(negedge clk);#1;endtask
    task automatic write_native(input integer chip,input [4:0] r,input [7:0] d);
        cs=1;we=1;addr=12'h600+chip*32+r;data_in=d;step();cs=0;we=0;
    endtask
    task automatic write_fast(input [7:0] d);
        fio_cs=1;fio_we=1;fio_addr=5'h12;fio_wdata=d;step();fio_cs=0;fio_we=0;
    endtask
    task automatic sample(input [7:0] active,input bit canceled);
        repeat(32)step();
        for(integer n=0;n<8;n++)check(controls[n*8+:8]===(canceled&&n==cancel_chip?8'h21:mask[n]?active:8'h41),"independent control sample before native CE");
        ce_1m=1;step();ce_1m=0;
    endtask
    initial begin
        @(negedge clk);#1;map_enable=1;
        for(fanout=2;fanout<=8;fanout++)for(first_chip=0;first_chip<8;first_chip++)begin
            reset=1;ce_1m=1;repeat(3)step();reset=0;ce_1m=0;
            for(integer s=0;s<32;s++)begin slot_masks[s*8+:8]=1<<(s%8);owners[s*3+:3]=s%8;end
            for(integer n=0;n<8;n++)write_native(n,5'h12,8'h41);
            mask=0;for(integer n=0;n<fanout;n++)mask[(first_chip+n)%8]=1;
            slot_masks[7:0]=mask;for(integer n=7;n>=0;n--)if(mask[n])owners[2:0]=n;
            cancel_chip=owners[2:0];
            write_fast(8'h40);write_fast(8'h41);sample(8'h40,0);sample(8'h41,0);sample(8'h41,0);
            write_fast(8'h40);write_fast(8'h41);write_native(cancel_chip,5'h12,8'h21);
            sample(8'h40,1);sample(8'h41,1);sample(8'h41,1);
            // Fanout live reads latch each nonowner chip's own value. Only
            // the lowest-addressed owner may use the captured owner byte.
            // Captured owner data may precede a live POT/sample change.
            fio_cs=1;fio_latch=1;fio_addr=5'h19;fio_wdata=8'ha5;step();fio_cs=0;fio_latch=0;
            fast_rd_addr=5'h19;#1;check(fast_rd_data===8'h12,"owner first fast read");
            for(integer n=0;n<8;n++)if(mask[n])check(latches[n*8+:8]===(n==cancel_chip?8'ha5:8'hff),"read latch owner/fanout isolation");
            begin reg [63:0] saved; saved=latches;
                write_inhibit[0]=1;write_fast(8'h88);
                cs=1;we=1;addr=12'h400;data_in=8'h77;step();cs=0;we=0;
                check(latches===saved,"inhibited native and FIO writes do not touch any latch");write_inhibit[0]=0;
            end
        end
        $display("PASS SID FANOUT checks=%0d cases=56 fanout=2..8 native_cancel=56",checks);$finish;
    end
    initial begin #5000000;$fatal(1,"SID fanout timeout");end
endmodule
