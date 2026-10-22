// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
// Original synthetic 65816 programs check slot mapping through the complete
// production adapter/cache at stock and accelerated speeds. Expected slot
// latch semantics follow KEGS 1.38 moremem.c; no Apple ROM bytes are used.
module tb_apple_iigs_mmu_slots;
    reg core_clk=0;always #8.730 core_clk=~core_clk;
    reg reset=1,core_reset_request=1,pause_request=0,clear_load_error=0;
    reg [15:0] input_code=0,input_value=0;reg input_valid=0;wire input_ready;
    reg [7:0] load_data=0,load_kind=0;
    reg load_valid=0,load_first=0,load_last=0,load_epoch=0,load_abort=0,load_abort_epoch=0;
    reg [31:0] load_offset=0,load_total=0;wire load_ready;
    wire [23:0] memory_addr;wire memory_write;wire [63:0] memory_wdata;wire [7:0] memory_wstrb;
    wire memory_valid,memory_ready,memory_burst,memory_busy;reg [63:0] memory_rdata=0;reg memory_rvalid=0,memory_fault=0;
    reg media_mount=0,media_eject=0,media_complete=0;
    reg [7:0] media_slot=0,media_kind=0;reg media_read_only=0;
    reg [31:0] media_image_size=0,media_generation=0;
    reg [31:0] media_complete_request_id=0,media_complete_status=0,media_complete_generation=0;
    reg [15:0] media_complete_block_count=0;
    wire media_request_valid,media_request_write;
    wire [31:0] media_request_id,media_request_lba,media_request_generation;
    wire [15:0] media_request_block_count;
    wire [7:0] media_write_data;wire media_write_data_valid,media_write_data_last;
    reg media_write_data_ready=0;
    wire media_state_mounted,media_state_dirty,media_state_eject_pending;
    wire [31:0] core_id;wire ready,load_error,rom_loaded;
    wire [7:0] video_r,video_g,video_b;wire video_de,video_hsync,video_vsync,video_valid,video_pal;
    wire [9:0] video_x;wire [8:0] video_y;
    wire signed [23:0] audio_l,audio_r;wire audio_valid;
    multitini_apple_iigs_adapter dut(.*);
    reg [63:0] memory[0:2097151];
    reg busy=0,pending_write=0,pending_burst=0;integer beats_left=0;reg [23:0] pending_addr;reg [63:0] pending_data;reg [7:0] pending_mask;
    integer delay_count=0,writes=0;
    assign memory_ready=!busy;
    always @(posedge core_clk)begin
        memory_rvalid<=0;
        if(memory_valid && memory_ready)begin
            pending_addr<=memory_addr;pending_data<=memory_wdata;pending_mask<=memory_wstrb;
            pending_write<=memory_write;delay_count<=3+memory_addr[5:3];busy<=1;
            // Memory interface v2: a burst is a 4-beat read of an aligned line.
            pending_burst<=memory_burst;beats_left<=memory_burst ? 4 : 1;
            if(memory_burst && (memory_write || memory_addr[4:0]!=0))$fatal(1,"invalid DDR burst %x",memory_addr);
        end else if(busy)begin
            if(delay_count!=0)delay_count<=delay_count-1;
            else begin
                if(pending_write)begin
                    for(integer lane=0;lane<8;lane=lane+1)
                        if(pending_mask[lane])memory[pending_addr[23:3]][lane*8+:8]<=pending_data[lane*8+:8];
                    writes<=writes+1;
                end
                memory_rdata<=memory[pending_addr[23:3]];memory_rvalid<=1;
                // Ascending beats, sometimes with idle cycles between them.
                if(beats_left>1)begin beats_left<=beats_left-1;pending_addr<=pending_addr+8;delay_count<=pending_addr[3];end
                else busy<=0;
            end
        end
    end

    reg [7:0] rom[0:131071];
    integer speed=0,pc=0,result_count=0,retired_slots=0,protected_writes=0,observed_result=0;
    bit captured_read=0,captured_rom=0,captured_fast=0,captured_slow=0;
    reg [7:0] expected[0:255];
    reg [23:0] expected_source[0:255];
    reg check_value[0:255];
    string case_name[0:255];
    task put(input integer address,input byte value);
      memory[address>>3][(address&7)*8+:8]=value;
    endtask
    task emit(input byte value);begin put(pc,value);pc++;end endtask
    task write_long(input [23:0] address,input byte value);
      begin
        emit(8'ha9);emit(value);emit(8'h8f);emit(address[7:0]);emit(address[15:8]);emit(address[23:16]);
      end
    endtask
    task read_long(input [23:0] address);
      begin emit(8'haf);emit(address[7:0]);emit(address[15:8]);emit(address[23:16]);end
    endtask
    task capture(input [23:0] address,input byte value,input bit compare,input string label_text);
      begin
        expected[result_count]=value;expected_source[result_count]=address;
        check_value[result_count]=compare;case_name[result_count]=label_text;
        read_long(address);emit(8'h8f);emit(8'(result_count));emit(0);emit(3);
        put(24'h030000+result_count,0);result_count++;
      end
    endtask
    task io_write(input [15:0] address,input byte value);write_long({8'he0,address},value);endtask
    task packet(input [7:0] kind,input integer size);
      byte value;
      begin
        load_epoch=~load_epoch;
        for(integer i=0;i<size;i++)begin
          value=0;
          if(kind==8'h41)value=rom[i];
          else if(kind==8'h43 && i==1)value=8'(speed);
          else if(kind==8'h43 && i==3)value=8'h10;
          else if(kind==8'h49)begin
            // Synthetic external slot7 ROM: CN page=A7, expansion=A8, CFFF=AF.
            value=i<256 ? 8'ha7 : i==2303 ? 8'haf : 8'ha8;
          end
          @(negedge core_clk);load_kind=kind;load_total=size;load_offset=i;load_data=value;
          load_first=i==0;load_last=i==size-1;load_valid=1;
          while(!load_ready)@(negedge core_clk);
          @(negedge core_clk);load_valid=0;
        end
        repeat(40)@(negedge core_clk);
        if(load_error)$fatal(1,"Synthetic slot loader error kind%02x",kind);
      end
    endtask

    // Every mapped C1-CF access must retire through the native bridge, including
    // internal firmware at acceleration, because C3/CFFF update slot latches.
    always @(posedge core_clk) if(!dut.machine_reset && dut.cpu_ce && dut.cpu_valid)begin
      if(!dut.bus_write && observed_result<result_count && dut.cpu_logical_addr==expected_source[observed_result])begin
        captured_read=1;captured_rom=dut.machine_i.rom_ce;
        captured_fast=dut.machine_i.fastram_ce;captured_slow=dut.machine_i.slowram_ce;
      end
      if(dut.bus_write && dut.cpu_logical_addr[23:8]==16'h0300)begin
        if(dut.cpu_speed!==3'(speed))$fatal(1,"Effective CPU speed%0d differs from requested%0d",dut.cpu_speed,speed);
        if(dut.cpu_logical_addr[7:0]!=8'(observed_result) || !captured_read)
          $fatal(1,"Missing source read for slot result%0d",observed_result);
        if(!check_value[observed_result] && (captured_rom || captured_fast || captured_slow))
          $fatal(1,"Unmapped/external slot returned system memory check%0d %s rom%0d fast%0d slow%0d",observed_result,case_name[observed_result],captured_rom,captured_fast,captured_slow);
        observed_result++;captured_read=0;
      end
      if(dut.cpu_logical_addr[23:16]==8'h80 || dut.cpu_logical_addr[23:16]==8'he2 ||
         dut.cpu_logical_addr[23:16]==8'hf0 || dut.cpu_logical_addr[23:16]==8'hf8)begin
        if(dut.machine_i.fastram_ce || dut.machine_i.slowram_ce || dut.machine_i.rom_ce || dut.cpu_memory_cycle)
          $fatal(1,"Absent bank accessed backing memory addr%06x",dut.cpu_logical_addr);
      end
      if((dut.cpu_logical_addr[23:16]==0 || dut.cpu_logical_addr[23:16]==1 ||
          dut.cpu_logical_addr[23:16]==8'he0 || dut.cpu_logical_addr[23:16]==8'he1) &&
          (dut.cpu_logical_addr[23:16]>=8'he0 || !dut.machine_i.shadow[6]) &&
          dut.cpu_logical_addr[15:0]>=16'hc100 && dut.cpu_logical_addr[15:0]<=16'hcfff)begin
        retired_slots++;
        if(dut.cpu_fast_cycle)$fatal(1,"Stateful slot access incorrectly fast speed%0d addr%06x",speed,dut.cpu_logical_addr);
        if(dut.machine_i.fastram_ce || dut.machine_i.slowram_ce)
          $fatal(1,"Mapped slot aliases hidden RAM speed%0d addr%06x write%0d fast%0d slow%0d",speed,dut.cpu_logical_addr,dut.bus_write,dut.machine_i.fastram_ce,dut.machine_i.slowram_ce);
        if(dut.bus_write)protected_writes++;
      end
    end

    initial begin
      integer polls;byte actual;reg [7:0] bank_id;
      for(integer setting=0;setting<2;setting++)begin
        speed=setting==0 ? 0:6;reset=1;core_reset_request=1;
        result_count=0;retired_slots=0;protected_writes=0;observed_result=0;captured_read=0;pc=24'h020000;
        for(integer i=0;i<131072;i++)rom[i]=8'hea;
        rom[131068]=0;rom[131069]=8'he0;
        // Reset entry: SEI; JML $020000.
        rom[122880]=8'h78;rom[122881]=8'h5c;rom[122882]=0;rom[122883]=0;rom[122884]=2;
        rom[65536+16'hc123]=8'hc1;rom[65536+16'hc300]=8'hc3;
        rom[65536+16'hc700]=8'hc7;rom[65536+16'hc800]=8'hc8;
        rom[65536+16'hc803]=8'hc8;rom[65536+16'hc823]=8'hc8;
        rom[65536+16'hcfff]=8'hcf;rom[65536+16'hc071]=8'h71;rom[65536+16'hc07f]=8'h7f;
        emit(8'h18);emit(8'hfb);emit(8'he2);emit(8'h30); // native A8/X8
        io_write(16'hc035,8'h08);io_write(16'hc02d,8'h80);
        io_write(16'hc007,0);
        capture(24'h00c700,8'hc7,1,"INTCXROM overrides external slot7");
        io_write(16'hc006,0);
        capture(24'h00c700,8'ha7,1,"external slot7 CN ROM");
        capture(24'h00c800,8'ha8,1,"slot7 owns expansion after CN read");
        capture(24'h00cfff,8'haf,1,"CFFF returns old external byte before release");
        capture(24'h00c800,0,0,"unowned expansion is not RAM");
        io_write(16'hc00a,0);
        capture(24'h00c300,8'hc3,1,"internal C3 read selects internal expansion");
        capture(24'h00c800,8'hc8,1,"internal C8 latch");
        capture(24'h00c700,8'ha7,1,"INTC8 does not replace CN firmware");
        capture(24'h00c800,8'hc8,1,"C7 read does not clear INTC8");
        capture(24'h00cfff,8'hcf,1,"CFFF returns internal byte before clear");
        write_long(24'h00c300,8'h11);
        capture(24'h00c800,8'hc8,1,"C3 write also sets INTC8");
        write_long(24'h00cfff,0);
        io_write(16'hc00b,0);
        capture(24'h00c300,0,0,"SLOTC3ROM selects external slot3");
        io_write(16'hc007,0);
        capture(24'h00c300,8'hc3,1,"INTCXROM overrides SLOTC3ROM");
        capture(24'h00c800,8'hc8,1,"INTCXROM selects internal C8");
        io_write(16'hc006,0);
        capture(24'h00c800,0,0,"external C3 with INTCX did not set INTC8");
        io_write(16'hc00a,0);read_long(24'h00c300);
        io_write(16'hc00b,0);
        capture(24'h00c800,8'hc8,1,"changing SLOTC3 does not clear INTC8");
        write_long(24'h00cfff,0);io_write(16'hc00a,0);
        io_write(16'hc007,0);
        for(integer k=0;k<4;k++)begin
          bank_id=k<2 ? 8'(k) : 8'he0+8'(k-2);
          capture({bank_id,16'hc071},8'h71,1,"IRQ dispatch alias C071");
          capture({bank_id,16'hc07f},8'h7f,1,"IRQ dispatch alias C07F");
          capture({bank_id,16'hc300},8'hc3,1,"internal CN alias");
          capture({bank_id,16'hc803},8'hc8,1,"internal expansion alias");
          // LC bank1 uses the cells behind mapped Cxxx. Slot stores must never
          // mutate these cells in either fast RAM or MegaII RAM.
          read_long(24'he0c08b);read_long(24'he0c08b);
          write_long({bank_id,16'hd123},8'h51+8'(k));
          write_long({bank_id,16'hd823},8'h61+8'(k));
          write_long({bank_id,16'hdfff},8'h71+8'(k));
          write_long({bank_id,16'hc123},8'h91);
          write_long({bank_id,16'hc823},8'h92);
          write_long({bank_id,16'hcfff},8'h93);
          capture({bank_id,16'hd123},8'h51+8'(k),1,"CN store preserved hidden LC RAM");
          capture({bank_id,16'hd823},8'h61+8'(k),1,"C8 store preserved hidden LC RAM");
          capture({bank_id,16'hdfff},8'h71+8'(k),1,"CFFF store preserved hidden LC RAM");
        end
        io_write(16'hc035,8'h48);
        write_long(24'h00c123,8'hd1);write_long(24'h01c123,8'hd2);
        capture(24'h00c123,8'hd1,1,"IOLC inhibit permits bank00 Cxxx RAM");
        capture(24'h01c123,8'hd2,1,"IOLC inhibit permits bank01 Cxxx RAM");
        // Arkanoid's restoration idiom: saved background remains in odd fast
        // RAM while sprites overwrite E1; rewriting the saved byte publishes it.
        io_write(16'hc035,8'h57);io_write(16'hc036,8'h90);
        for(integer region=0;region<2;region++)begin
          write_long(region==0 ? 24'h032000 : 24'h0393a0,8'h44);
          write_long(region==0 ? 24'he12000 : 24'he193a0,8'haa);
          capture(region==0 ? 24'he12000 : 24'he193a0,8'haa,1,"SHR sprite overwrote screen only");
          capture(region==0 ? 24'h032000 : 24'h0393a0,8'h44,1,"SHR saved background remains in fast bank03");
          read_long(region==0 ? 24'h032000 : 24'h0393a0);
          emit(8'h8f);emit(region==0 ? 8'h00 : 8'ha0);emit(region==0 ? 8'h20 : 8'h93);emit(8'h03);
          capture(region==0 ? 24'he12000 : 24'he193a0,8'h44,1,"SHR LDA/STA self-copy restores background");
        end
        // KEGS sizing probes and other uninstalled-bank reads return zero;
        // stores are ignored and must never request either RAM backing.
        for(integer missing=0;missing<4;missing++)begin
          bank_id=missing==0 ? 8'h80 : missing==1 ? 8'he2 : missing==2 ? 8'hf0 : 8'hf8;
          write_long({bank_id,16'h0000},8'h99);write_long({bank_id,16'h0001},8'h88);
          write_long({bank_id,16'h1234},8'h77);
          capture({bank_id,16'h0000},0,1,"KEGS absent-bank sizing offset0 returns zero");
          capture({bank_id,16'h0001},0,1,"KEGS absent-bank sizing offset1 returns zero");
          capture({bank_id,16'h1234},0,1,"KEGS absent-bank general read returns zero");
        end
        capture(24'hfc1234,0,1,"KEGS ROM01 reserved FC reads zero");
        capture(24'hfd1234,0,1,"KEGS ROM01 reserved FD reads zero");
        emit(8'hdb);
        repeat(20)@(negedge core_clk);reset=0;
        packet(8'h43,16);packet(8'h41,131072);packet(8'h42,4096);packet(8'h46,4096);packet(8'h49,2304);
        if(!rom_loaded)$fatal(1,"Synthetic slot ROM set not ready");
        // External HDD firmware is visible only while a synthetic disk is mounted.
        @(negedge core_clk);media_slot=0;media_kind=8'h40;media_image_size=8192;media_generation=1;media_mount=1;
        @(negedge core_clk);media_mount=0;
        repeat(40)@(negedge core_clk);
        core_reset_request=0;polls=0;
        while(!dut.machine_i.cpu.STPExec && polls<2000000)begin @(negedge core_clk);polls++;end
        if(!dut.machine_i.cpu.STPExec)$fatal(1,"Slot program timeout PC%06x shadow%02x",dut.cpu_logical_addr,dut.machine_i.shadow);
        polls=0;while(memory_busy && polls<100000)begin @(negedge core_clk);polls++;end
        repeat(100)@(negedge core_clk);
        for(integer i=0;i<result_count;i++)begin
          actual=memory[(24'h030000+i)>>3][((24'h030000+i)&7)*8+:8];
          if(check_value[i] && actual!==expected[i])
            $fatal(1,"Slot check%0d speed%0d %s src%06x got%02x expected%02x",i,speed,case_name[i],expected_source[i],actual,expected[i]);
        end
        if(observed_result!=result_count || retired_slots<40 || protected_writes<14)$fatal(1,"Missing slot retirement coverage");
        $display("PASS: MMU slots speed%0d checks%0d native_slot_accesses%0d protected_writes%0d",speed,result_count,retired_slots,protected_writes);$fflush();
      end
      $finish;
    end
    initial begin #200000000;$fatal(1,"MMU slot timeout");end
endmodule
