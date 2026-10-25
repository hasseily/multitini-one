// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_sid_osc3_8;
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
    multitini_c64_sid_array #(.SID_COUNT(8),.FAST_PORT(1)) dut(
        .clk(clk),.reset(reset),.ce_1m(ce_1m),.cs(cs),.we(we),.addr(addr),.data_in(data_in),.data_out(data_out),
        .fio_cs(fio_cs),.fio_we(fio_we),.fio_latch(fio_latch),.fio_latch_src(fio_latch_src),.fio_addr(fio_addr),.fio_wdata(fio_wdata),
        .fast_rd_cs(fast_rd_cs),.fast_rd_addr(fast_rd_addr),.fast_rd_data(fast_rd_data),
        .fc_offset_l(13'd0),.fc_offset_r(13'd0),.pot_x_l(8'h12),.pot_y_l(8'h34),.pot_x_r(8'hff),.pot_y_r(8'hff),
        .ext_in_l(18'd0),.ext_in_r(18'd0),.audio_l(audio_l),.audio_r(audio_r),.filter_en(2'b11),.mode(2'b00),.cfg(4'b0),
        .ld_clk(clk),.ld_addr(12'd0),.ld_data(16'd0),.ld_wr(1'b0),.map_enable(map_enable),.io_select(io_select),
        .slot_masks(slot_masks),.write_inhibit(write_inhibit),.owners(owners),.io_masks(io_masks),.chip_params(chip_params),.chip_audio(chip_audio));
    multitini_c64_sid_array #(.SID_COUNT(4),.FAST_PORT(1)) four(
        .clk(clk),.reset(reset),.ce_1m(ce_1m),.cs(cs),.we(we),.addr(addr),.data_in(data_in),.data_out(),
        .fio_cs(fio_cs),.fio_we(fio_we),.fio_latch(fio_latch),.fio_latch_src(fio_latch_src),.fio_addr(fio_addr),.fio_wdata(fio_wdata),
        .fast_rd_cs(fast_rd_cs),.fast_rd_addr(fast_rd_addr),.fast_rd_data(),
        .fc_offset_l(13'd0),.fc_offset_r(13'd0),.pot_x_l(8'h12),.pot_y_l(8'h34),.pot_x_r(8'hff),.pot_y_r(8'hff),
        .ext_in_l(18'd0),.ext_in_r(18'd0),.audio_l(),.audio_r(),.filter_en(2'b11),.mode(2'b00),.cfg(4'b0),
        .ld_clk(clk),.ld_addr(12'd0),.ld_data(16'd0),.ld_wr(1'b0),.map_enable(map_enable),.io_select(io_select),
        .slot_masks(slot_masks),.write_inhibit(write_inhibit),.owners(owners),.io_masks(io_masks),.chip_params(chip_params),.chip_audio());
    reg [31:0] tables[0:341];integer checks=0;integer target,base;
    reg [31:0] four_saved;
    wire [31:0] four_latches={four.pairs[0].present.sid.bus_data[0],four.pairs[0].present.sid.bus_data[1],four.pairs[1].present.sid.bus_data[0],four.pairs[1].present.sid.bus_data[1]};
    task automatic check(input bit ok,input string why);checks++;if(!ok)$fatal(1,"SID OSC8 %s target=%0d address=%h",why,target,addr);endtask
    task automatic step;@(posedge clk);#1;endtask
    task automatic load_table(input integer preset);
        for(integer s=0;s<32;s++)begin
            slot_masks[s*8+:8]=tables[preset*57+s][7:0];write_inhibit[s]=tables[preset*57+s][8];
            for(integer n=7;n>=0;n--)if(tables[preset*57+s][n])owners[s*3+:3]=n;
        end
    endtask
    task automatic write_reg(input integer a,input [7:0] value);
        @(negedge clk);cs=1;we=1;addr=a;data_in=value;step();@(negedge clk);cs=0;we=0;
    endtask
    initial begin
        $readmemh("presets.hex",tables);map_enable=1;
        for(target=0;target<8;target++)begin
            reset=1;repeat(64)step();while(phase!=8)step();@(negedge clk);reset=0;load_table(5);
            base=-1;for(integer s=31;s>=0;s--)if(slot_masks[s*8+:8]==(1<<target)&&!write_inhibit[s])base=12'h400+s*32;
            check(base>=0,"preset provides each target address");
            write_reg(base+14,8'h31);write_reg(base+15,8'h24);
            write_reg(base+19,0);write_reg(base+20,8'hf0);write_reg(base+18,8'h21);
            check(phase!=0&&phase<31,"all initial writes precede first native CE");
            repeat(32768)step();
            $display("OSC target=%0d base=%h phase=%0d controls=%h/%h freq=%h/%h env=%h active=%b reset=%b",target,base,phase,dut.pairs[0].present.sid.Voice_3_Control[0],dut.pairs[1].present.sid.Voice_3_Control[0],dut.pairs[0].present.sid.Voice_3_Freq[0],dut.pairs[1].present.sid.Voice_3_Freq[0],dut.candidates[target*40+8+:8],dut.pairs[1].present.active,dut.pairs[1].present.engine_reset);
            for(integer s=0;s<32;s++)begin
                @(negedge clk);addr=12'h400+s*32+28;cs=1;fast_rd_addr=s*32+28;#1;
                check(fast_rd_data===(owners[s*3+:3]==target?dut.candidates[target*40+8+:8]:8'h00),"all mirror ENV3 owners");
                check((fast_rd_data!=0)==(owners[s*3+:3]==target),"only target envelope advances");
                begin reg [7:0] sampled;sampled=fast_rd_data;step();check(data_out===sampled,"native ENV3 accepted read");end
                addr=12'h400+s*32+27;fast_rd_addr=s*32+27;#1;
                check(fast_rd_data===(owners[s*3+:3]==target?dut.candidates[target*40+16+:8]:8'h00),"all mirror OSC3 owners");
                begin reg [7:0] sampled;sampled=fast_rd_data;step();check(data_out===sampled,"native OSC3 accepted read");end
                addr=12'h400+s*32+25;fast_rd_addr=s*32+25;#1;
                check(fast_rd_data===(owners[s*3+:3]==owners[2:0]?8'h12:8'hff),"paddle owner only");
                begin reg [7:0] sampled;sampled=fast_rd_data;step();check(data_out===sampled,"native POT accepted read");end
            end
            @(negedge clk);cs=0;
        end
        reset=1;repeat(64)step();@(negedge clk);reset=0;load_table(3);target=0;
        write_reg(12'h40e,8'h53);write_reg(12'h40f,8'h17);
        // Triple's D460 mirror is explicitly inhibited; every chip register
        // and latch must remain unchanged despite native and fast writes.
        begin reg [127:0] before_regs;reg [63:0] before_latches;
            before_regs=0;before_latches=0;
            for(integer n=0;n<8;n++)before_latches[n*8+:8]=dut.candidates[n*40+:8];
            write_reg(12'h46e,8'h99);write_reg(12'h46f,8'h88);
            @(negedge clk);fio_cs=1;fio_we=1;fio_addr=10'h06e;fio_wdata=8'h77;step();@(negedge clk);fio_cs=0;fio_we=0;
            check(dut.pairs[0].present.sid.Voice_3_Freq[0]==16'h1753,"Triple D460 cannot corrupt SID1");
            for(integer n=0;n<8;n++)check(dut.candidates[n*40+:8]===before_latches[n*8+:8],"Triple inhibited write cannot alter any latch");
        end
        reset=1;repeat(64)step();while(phase!=8)step();@(negedge clk);reset=0;load_table(5);
        four_saved=four_latches;
        for(integer r=0;r<25;r++)begin
            write_reg(12'h500+r,8'h90+r);
            @(negedge clk);fio_cs=1;fio_we=1;fio_addr=10'h100+r;fio_wdata=8'h60+r;step();@(negedge clk);fio_cs=0;fio_we=0;
        end
        check(four_latches===four_saved,"count4 D500 does not alias existing chips");
        check({four.pairs[0].present.sid.Voice_3_Freq[0],four.pairs[0].present.sid.Voice_3_Freq[1],four.pairs[1].present.sid.Voice_3_Freq[0],four.pairs[1].present.sid.Voice_3_Freq[1]}===64'd0,"count4 D500 does not change oscillators");
        $display("PASS SID OSC8 checks=%0d targets=8 mirror_slots=32 triple=1",checks);$finish;
    end
    initial begin #10000000;$fatal(1,"SID OSC8 timeout");end
endmodule
