// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Exercise production I/O retirement and state transitions. The reference state
// below is independent of the DUT decode; no game or system ROM is required.
module tb_apple_iigs_mmu_state;
    reg clk=0,reset=1,cold=1,rom1=0,commit=0;
    always #5 clk=~clk;
    reg [23:0] address=24'he0c000;
    reg [7:0] data=0;
    reg read_cycle=1;
    iigs #(.EXTERNAL_CPU(1)) dut(
        .CLK_14M(clk),.CLK_28M(clk),.clk_vid(clk),.ce_pix(1'b0),
        .CPU_CLK(clk),.CPU_CE(1'b0),.CPU_FAST_CE(1'b0),.CPU_DIN(8'hff),
        .NATIVE_EDGE(1'b1),.NATIVE_COMMIT(commit),.reset(reset),.cold_reset(cold),
        .rom_select(rom1),.mem_stall(1'b0),.top_din(8'hff),.timestamp(33'd0),
        .load_rom_clk(clk),.load_rom_we(1'b0),.load_rom_addr(16'd0),.load_rom_data(8'd0));
    // Replace only the processor bus producer. MMU decode, I/O read mux and
    // retirement-gated register writes are the unmodified production modules.
    initial begin
        force dut.cpu_addr=address;
        force dut.cpu_dout=data;
        force dut.cpu_we_n=read_cycle;
        force dut.cpu_vda=1'b1;
        force dut.cpu_vpa=1'b0;
        force dut.cpu_vpb=1'b1;
        force dut.hdd_dma=1'b0;
    end
    reg e_store=0,e_ramrd=0,e_ramwrt=0,e_intcx=0,e_altzp=0;
    reg e_slot3=0,e_eighty=0,e_altchar=0,e_page2=0,e_rdrom=1,e_lcbank2=1,e_rombank=0;
    reg e_pre=0,e_write=1,e_hires=0;
    reg [7:0] e_shadow=0,e_speed=0,e_slotreg=0,e_newvideo=8'h41;
    integer checks=0,failures=0,transactions=0,printed_failures=0;
    string group_name,printed_group;
    // KEGS also clears power-on bit 6 on a reserved-bit-5 write. Such a
    // write emits a KEGS diagnostic; this bench compares the stored byte.
    function automatic [7:0] speed_value(input [7:0] value);
        speed_value=(value & 8'h9f) | ((!rom1 && !value[5]) ? (value & 8'h40):8'd0);
    endfunction
    task expect_byte(input [7:0] actual,expected,input string label_text);
        begin
            checks++;
            if(actual!==expected)begin
                failures++;
                if(printed_group!=group_name)begin printed_group=group_name;printed_failures=0;end
                if(printed_failures<8)$display("MISMATCH group=%s %s address=%06x read=%0b got=%02x expected=%02x",group_name,label_text,address,read_cycle,actual,expected);
                printed_failures++;
            end
        end
    endtask
    task state_check;
        begin
            expect_byte({dut.STORE80,dut.RAMRD,dut.RAMWRT,dut.INTCXROM,dut.ALTZP,dut.SLOTC3ROM,dut.EIGHTYCOL,dut.ALTCHARSET},
                {e_store,e_ramrd,e_ramwrt,e_intcx,e_altzp,e_slot3,e_eighty,e_altchar},"C000-C00F state");
            expect_byte({dut.ALTZP,dut.PAGE2,dut.RAMRD,dut.RAMWRT,dut.RDROM,dut.LCRAM2,dut.ROMBANK,dut.INTCXROM},
                {e_altzp,e_page2,e_ramrd,e_ramwrt,e_rdrom,e_lcbank2,e_rombank,e_intcx},"C068 state");
            expect_byte({6'd0,dut.LC_WE_PRE,dut.LC_WE},{6'd0,e_pre,e_write},"LC prewrite/write-enable");
            expect_byte(dut.shadow,e_shadow,"C035");
            expect_byte(dut.CYAREG,e_speed,"C036");
            expect_byte(dut.SLTROMSEL,e_slotreg,"C02D/slot3 state");
            expect_byte(dut.NEWVIDEO,e_newvideo,"C029");
            expect_byte({7'd0,dut.HIRES_MODE},{7'd0,e_hires},"HIRES state");
        end
    endtask
    task reference_access(input [7:0] bank,offset,value,input bit rd);
        bit io_enabled;
        begin
            io_enabled=bank==8'he0 || bank==8'he1 || ((bank==0 || bank==1) && !e_shadow[6]);
            if(io_enabled)begin
                if(!rd && offset<16)case(offset[3:1])
                    0:e_store=offset[0];1:e_ramrd=offset[0];2:e_ramwrt=offset[0];3:e_intcx=offset[0];
                    4:e_altzp=offset[0];
                    5:begin e_slot3=offset[0];e_slotreg[3]=offset[0];end
                    6:e_eighty=offset[0];7:e_altchar=offset[0];
                endcase
                if(!rd)case(offset)
                    8'h29:e_newvideo=value & 8'he1;
                    8'h2d:begin e_slotreg=value;e_slot3=value[3];end
                    8'h35:e_shadow=value;
                    8'h36:e_speed=speed_value(value);
                    8'h68:{e_altzp,e_page2,e_ramrd,e_ramwrt,e_rdrom,e_lcbank2,e_rombank,e_intcx}=value;
                    default:;
                endcase
                if(offset>=8'h80 && offset<=8'h8f)begin
                    e_rdrom=offset[0]^offset[1];e_lcbank2=!offset[3];
                    if(!offset[0])e_write=0;
                    else if(rd && e_pre)e_write=1;
                    e_pre=rd && offset[0];
                end
                if(offset==8'h56 || offset==8'h57)e_hires=offset[0];
                if(offset==8'h54 || offset==8'h55)e_page2=offset[0];
            end
        end
    endtask
    task access_io(input [7:0] bank,offset,value,input bit rd);
        begin
            @(negedge clk);address={bank,8'hc0,offset};data=value;read_cycle=rd;commit=0;
            // Merely holding an I/O address must not clock its side effects.
            repeat(2)begin @(posedge clk);#1;state_check();end
            @(negedge clk);commit=1;
            @(posedge clk);#1;
            reference_access(bank,offset,value,rd);state_check();transactions++;
            @(negedge clk);commit=0;
        end
    endtask
    task read_register(input [7:0] bank,offset,expected);
        begin
            @(negedge clk);address={bank,8'hc0,offset};read_cycle=1;commit=0;
            #1;expect_byte(dut.io_dout,expected,"register readback");
        end
    endtask
    task init_state;
        begin
            // Explicit I/O writes initialize the oracle; do not inherit the
            // implementation's power-on defaults for unrelated video state.
            access_io(8'he0,8'h35,8'h08,0);
            access_io(8'he0,8'h36,8'h80,0);
            access_io(8'he0,8'h68,8'h0c,0);
            for(integer i=0;i<16;i+=2)access_io(8'he0,8'(i),0,0);
        end
    endtask
    task check_slot3(input bit external_slot);
        begin
            read_register(8'he0,8'h17,{external_slot,7'd0});
            @(negedge clk);address=24'h00c300;read_cycle=1;#1;
            expect_byte({7'd0,dut.slot_internalrom_ce},{7'd0,!external_slot},"slot3 internal ROM decode");
            expect_byte({7'd0,dut.slot_ce},{7'd0,external_slot},"slot3 external ROM decode");
        end
    endtask
    task reset_case(input bit is_rom1,is_cold);
        reg [7:0] old_speed,old_newvideo;
        begin
            old_speed=e_speed;old_newvideo=e_newvideo;
            @(negedge clk);commit=0;rom1=is_rom1;cold=is_cold;reset=1;
            repeat(3)@(posedge clk);
            @(negedge clk);reset=0;#1;
            // KEGS do_reset() resets banking and SHADOW, and preserves the
            // complete speed register. A cold start initializes speed to 80/C0.
            expect_byte({dut.ALTZP,dut.PAGE2,dut.RAMRD,dut.RAMWRT,dut.RDROM,dut.LCRAM2,dut.LC_WE_PRE,dut.LC_WE},8'h0d,"reset LC/banking state");
            expect_byte({4'd0,dut.TEXTG,dut.MIXG,dut.HIRES_MODE,dut.AN3},8'h09,"reset video state");
            expect_byte(dut.TEXTCOLOR,8'hf0,"reset text color");
            e_store=0;e_ramrd=0;e_ramwrt=0;e_intcx=1;e_altzp=0;
            e_slot3=0;e_eighty=0;e_altchar=0;e_page2=0;
            e_rdrom=1;e_lcbank2=1;e_rombank=0;e_pre=0;e_write=1;
            e_hires=0;
            e_shadow=0;e_speed=is_cold ? (is_rom1 ? 8'h80:8'hc0):old_speed;e_slotreg=0;
            e_newvideo=is_cold ? 8'h41:(old_newvideo & 8'h41);
            state_check();
            read_register(8'he0,8'h29,e_newvideo);
            read_register(8'he0,8'h1d,8'h00);
            init_state();
        end
    endtask
    task dirty_video_state;
        begin
            access_io(8'he0,8'h50,0,0);
            access_io(8'he0,8'h53,0,1);
            access_io(8'he0,8'h57,0,1);
            access_io(8'he0,8'h5e,0,0);
            access_io(8'he0,8'h22,8'hf2,0);
        end
    endtask
    reg [7:0] banks[0:3];
    initial begin
        banks[0]=0;banks[1]=1;banks[2]=8'he0;banks[3]=8'he1;
        group_name="reset";reset_case(0,1);
        group_name="newvideo register";
        for(integer value=0;value<256;value++)begin
            access_io(8'he0,8'h29,8'(value),0);
            read_register(8'he1,8'h29,8'(value)&8'he1);
        end
        access_io(8'he0,8'h29,8'h41,0);
        group_name="shadow register";
        for(integer value=0;value<256;value++)begin
            access_io(8'he0,8'h35,8'(value),0);
            read_register(8'he1,8'h35,8'(value));
        end
        access_io(8'he0,8'h35,8'h08,0);
        group_name="aliases";
        for(integer b=0;b<4;b++)begin
            for(integer n=0;n<16;n++)begin
                access_io(banks[b],8'(n),0,0);
                access_io(banks[b],8'(n^1),0,1); // read must not toggle latch
            end
            for(integer n=0;n<256;n++)begin
                access_io(banks[b],8'h68,8'(n),0);
                read_register(banks[b],8'h68,8'(n));
            end
            // PAGE2 and HIRES are address switches on reads and writes.
            for(integer n=0;n<8;n++)begin
                access_io(banks[b],8'h54+8'(n&3),0,n<4);
                read_register(banks[b],8'h1c,{e_page2,7'd0});
                read_register(banks[b],8'h1d,{e_hires,7'd0});
            end
        end
        $display("GROUP aliases complete transactions=%0d failures=%0d",transactions,failures);
        group_name="slot3";access_io(8'he0,8'h68,8'h0c,0);
        for(integer slot=0;slot<2;slot++)begin
            access_io(8'he0,slot ? 8'h0b:8'h0a,0,0);
            read_register(8'he0,8'h2d,e_slotreg);check_slot3(slot[0]);
            for(integer value=0;value<256;value++)begin
                access_io(8'he0,8'h2d,8'(value),0);
                read_register(8'he0,8'h2d,8'(value));check_slot3(value[3]);
            end
        end
        $display("GROUP slot3 complete transactions=%0d failures=%0d",transactions,failures);
        group_name="LC";
        // Exhaust every pair of read/write C080-C08F operations, including
        // cross-alias pairs, and preserve PREWRITE across C068/unrelated reads.
        for(integer b=0;b<4;b++)for(integer first=0;first<32;first++)for(integer second=0;second<32;second++)begin
            access_io(8'he0,8'h80,0,1);
            access_io(banks[b],8'h80+8'(first&15),0,first<16);
            access_io(8'he0,8'h68,8'h0c,0);
            read_register(8'he0,8'h35,e_shadow);
            access_io(banks[(b+1)%4],8'h80+8'(second&15),0,second<16);
        end
        $display("GROUP LC complete transactions=%0d failures=%0d",transactions,failures);
        group_name="IOLC guard";access_io(8'he0,8'h35,8'h48,0);
        for(integer b=0;b<2;b++)for(integer n=0;n<16;n++)access_io(banks[b],8'(n),0,0);
        access_io(8'he0,8'h35,8'h08,0);
        group_name="speed/reset";
        for(integer version=0;version<2;version++)begin
            dirty_video_state();
            reset_case(version[0],1);
            for(integer value=0;value<256;value++)begin
                access_io(8'he0,8'h36,8'(value),0);
                read_register(8'he1,8'h36,speed_value(8'(value)));
            end
            // A warm reset must preserve slow/fast, all-bank shadow, motor
            // detection and the power-on latch, not only the power-on bit.
            access_io(8'he0,8'h36,8'h5b,0);
            dirty_video_state();
            reset_case(version[0],0);
            for(integer retained=0;retained<4;retained++)begin
                access_io(8'he0,8'h29,8'ha0 | (retained[1] ? 8'h40:8'h00) | {7'd0,retained[0]},0);
                dirty_video_state();
                reset_case(version[0],0);
            end
        end
        $display("GROUP speed/reset complete transactions=%0d failures=%0d",transactions,failures);
        if(failures)$fatal(1,"MMU state failures=%0d checks=%0d transactions=%0d",failures,checks,transactions);
        $display("PASS: IIgs MMU state checks=%0d transactions=%0d",checks,transactions);$finish;
    end
    initial begin #10000000;$fatal(1,"MMU state timeout");end
endmodule
