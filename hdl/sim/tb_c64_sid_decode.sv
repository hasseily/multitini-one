// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_sid_decode;
    reg clk=0;
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
    reg [2:0] legacy_mode=0;reg [15:0] legacy_addr=0;
    wire legacy_l,legacy_r;wire [1:0] legacy_fast,legacy_read,legacy_source;
    c64_sid_decode_observed legacy(.sid_mode(legacy_mode),.cpuAddr(legacy_addr),.cs_sid(1'b1),
        .ioe_i(legacy_addr[11:8]==14),.iof_i(legacy_addr[11:8]==15),
        .fastSidA(legacy_addr[9:0]),.fio_rd_addr(legacy_addr[9:0]),.fastSidWr(1'b1),.fastSidLatch(1'b0),.fio_rd_sid(1'b1),
        .sid_sel_l(legacy_l),.sid_sel_r(legacy_r),.fastSidSelect(legacy_fast),.fastSidReadSelect(legacy_read),.fastSidLatchSource(legacy_source));
    wire [7:0] nselect,fselect,latch_source;
    for(genvar p=0;p<4;p++)begin
        assign nselect[p*2+:2]=dut.pairs[p].present.ncs;
        assign fselect[p*2+:2]=dut.pairs[p].present.fcs;
        assign latch_source[p*2+:2]=dut.pairs[p].present.src;
    end
    reg [31:0] tables[0:6*57-1];reg [31:0] rng=32'h12345678;
    integer checks=0,table_count=0;reg [7:0] mask,expected,owner;
    function automatic [2:0] first(input [7:0] m);
        first=0;for(integer b=7;b>=0;b--)if(m[b])first=b;
    endfunction
    task automatic check(input bit ok,input string why);
        checks++;if(!ok)$fatal(1,"SID DECODE %s table=%0d addr=%h",why,table_count,addr);
    endtask
    task automatic tick;#1;clk=1;#1;clk=0;endtask
    initial begin
        for(integer m=0;m<8;m++)for(integer a=0;a<4096;a++)begin
            reg l,r;reg [1:0] f;
            legacy_mode=m;legacy_addr=16'hd000+a;#1;
            l=m==2?!a[5]:m==3?!a[8]:1;
            r=m==0?l:m==1?(a/256==14):m==4?(a/256==15):!l;
            f=m==0?3:m==2?(a[5]?2:1):m==3?(a[8]?2:1):1;
            check({legacy_r,legacy_l}==={r,l},"all legacy native modes/addresses");
            check(legacy_fast===f&&legacy_read===f&&legacy_source===(f[0]?1:2),"all legacy FIO selects/owner");
        end
        $readmemh("presets.hex",tables);repeat(64)tick();reset=0;map_enable=1;
        for(integer slot=0;slot<32;slot++)begin slot_masks[slot*8+:8]=1<<(slot%8);owners[slot*3+:3]=slot%8;end
        for(integer chip=0;chip<8;chip++)begin cs=1;we=1;addr=chip*32;data_in=8'h40+chip;tick();end
        cs=0;we=0;
        // Every nonempty legal mask, owner chosen independently by priority.
        // 1000 deterministic random tables supplement all six firmware presets.
        for(integer t=0;t<1006;t++)begin
            for(integer slot=0;slot<32;slot++)begin
                rng=rng^(rng<<13);rng=rng^(rng>>17);rng=rng^(rng<<5);
                mask=t<6?tables[t*57+slot][7:0]:(rng[7:0]!=0?rng[7:0]:8'h01);
                slot_masks[slot*8+:8]=mask;owners[slot*3+:3]=first(mask);
                write_inhibit[slot]=t<6?tables[t*57+slot][8]:rng[8];
            end
            table_count=t;cs=1;fio_cs=1;fio_we=1;fio_latch=0;
            // Entire A11..A0 space, including all mirrors, with independent
            // native and FIO addresses. Hold clk: this is a decode proof.
            for(integer a=0;a<4096;a++)begin
                addr=a;fio_addr=(4095-a)&1023;fast_rd_addr=(a*73)&1023;we=a[0];#1;
                mask=slot_masks[(a/32%32)*8+:8];
                check(nselect===(we&&write_inhibit[a/32%32]?0:mask),"native write inhibit/select");
                mask=slot_masks[(fio_addr/32)*8+:8];
                check(fselect===(write_inhibit[fio_addr/32]?0:mask),"FIO write inhibit/select");
                check(latch_source===(8'b1<<first(mask)),"lowest owner latch source");
                owner=first(slot_masks[(fast_rd_addr/32)*8+:8]);
                case(fast_rd_addr[4:0])
                25:expected=dut.candidates[owner*40+32+:8];
                26:expected=dut.candidates[owner*40+24+:8];
                27:expected=dut.candidates[owner*40+16+:8];
                28:expected=dut.candidates[owner*40+8+:8];
                default:expected=dut.candidates[owner*40+:8];
                endcase
                check(fast_rd_data===expected,"lowest selected chip read mux");
            end
        end
        $display("PASS SID DECODE tables=1006 addresses=4096 checks=%0d",checks);$finish;
    end
endmodule
