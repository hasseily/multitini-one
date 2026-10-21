// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
// Original synthetic 65816 programs exercise IRQ vector fetching through the
// complete production adapter/cache in native and emulation modes. No Apple
// ROM or game bytes are included.
module tb_apple_iigs_iolc_vectors;
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

    // Original synthetic ROM bootstraps each real CPU program in bank 02.
    reg [7:0] rom[0:131071];integer cursor,read_count=0,speed=0;
    task emit(input [7:0] x);begin rom[cursor]=x;cursor++;end endtask
    task packet(input [7:0] kind,input integer size);
      reg [7:0] value;
      begin
        load_epoch=~load_epoch;
        for(integer i=0;i<size;i++)begin
          // Disable Zip IRQ delay so the maximum-speed IRQ vector cycles
          // remain accelerated after the CPU sets its interrupt mask.
          value=kind==8'h41 ? rom[i]:kind==8'h43 && i==1 ? speed:kind==8'h43 && i==3 ? 8'h10:0;
          @(negedge core_clk);load_kind=kind;load_total=size;load_offset=i;load_data=value;
          load_first=i==0;load_last=i==size-1;load_valid=1;
          while(!load_ready)@(negedge core_clk);
          @(negedge core_clk);load_valid=0;
        end
        repeat(40)@(negedge core_clk);
        if(load_error)$fatal(1,"Synthetic loader error");
      end
    endtask

    integer ram_cursor=0,vector_reads=0,emulation_case=0,iolc_case=0;
    reg [15:0] vector_address;
    byte vector_lo=0,vector_hi=0;bit test_irq=0;
    task put(input integer address,input byte value);
      memory[address>>3][(address&7)*8+:8]=value;
    endtask
    task ram_emit(input byte value);
      begin put(ram_cursor,value);ram_cursor++;end
    endtask
    always @(posedge core_clk) if(!dut.machine_reset && dut.cpu_ce && !dut.machine_i.cpu_vpb && dut.machine_i.cpu_we_n) begin
      if((dut.machine_i.cpu_addr=={8'h00,vector_address} || dut.machine_i.cpu_addr=={8'h00,vector_address+16'd1}) && dut.cpu_speed!==3'(speed))
        $fatal(1,"IRQ vector effective speed%0d differs from configured speed%0d",dut.cpu_speed,speed);
      if(dut.machine_i.cpu_addr=={8'h00,vector_address})begin vector_lo=dut.cpu_read_data;vector_reads++;end
      if(dut.machine_i.cpu_addr=={8'h00,vector_address+16'd1})begin vector_hi=dut.cpu_read_data;vector_reads++;end
      $display("VECTOR speed%0d emulation%0d iolc%0d addr%06x data%02x physical%06x rom%0b shadow%02x",speed,emulation_case,iolc_case,dut.machine_i.cpu_addr,dut.cpu_read_data,dut.physical_addr,dut.machine_i.rom_ce,dut.machine_i.shadow);$fflush();
    end
    initial begin
      integer polls;byte expected_marker,actual_marker;bit expect_rom;
      bit original_upstream;original_upstream=$test$plusargs("UPSTREAM_NEGATIVE_CONTROL");
      force dut.machine_i.snd_irq=test_irq;
      for(integer setting=0;setting<2;setting++) for(emulation_case=0;emulation_case<2;emulation_case++) for(iolc_case=0;iolc_case<2;iolc_case++)begin
        expect_rom=!iolc_case || original_upstream;expected_marker=expect_rom ? 8'h5a : 8'ha5;
        vector_address=emulation_case ? 16'hfffe : 16'hffee;
        speed=setting==0 ? 0:6;vector_reads=0;test_irq=0;reset=1;core_reset_request=1;
        for(integer i=0;i<131072;i++)rom[i]=8'hea;
        rom[131068]=0;rom[131069]=8'he0;
        rom[65536+vector_address]=0;rom[65536+vector_address+1]=8'he4;
        cursor=65536+16'he400;emit(8'ha9);emit(8'h5a);emit(8'h8f);emit(0);emit(1);emit(2);emit(8'hdb);
        cursor=122880;emit(8'h78);emit(8'h5c);emit(0);emit(0);emit(2); // SEI; JML $020000
        // RAM program: A/X8; install vector with IOLC set; then select requested map.
        // The normal-LC case must retain ROM vectors; IOLC must expose installed RAM.
        ram_cursor=24'h020000;if(!emulation_case)begin ram_emit(8'h18);ram_emit(8'hfb);end
        ram_emit(8'he2);ram_emit(8'h30);
        ram_emit(8'ha9);ram_emit(8'h40);ram_emit(8'h8f);ram_emit(8'h35);ram_emit(8'hc0);ram_emit(8'he0);
        ram_emit(8'ha9);ram_emit(0);ram_emit(8'h8f);ram_emit(vector_address[7:0]);ram_emit(8'hff);ram_emit(0);
        ram_emit(8'ha9);ram_emit(8'hdd);ram_emit(8'h8f);ram_emit(vector_address[7:0]+8'd1);ram_emit(8'hff);ram_emit(0);
        ram_emit(8'ha9);ram_emit(iolc_case ? 8'h40 : 0);ram_emit(8'h8f);ram_emit(8'h35);ram_emit(8'hc0);ram_emit(8'he0);
        ram_emit(8'h58);ram_emit(8'hcb);ram_emit(8'hdb);
        // Correct RAM vector target and distinct wrong-ROM-vector target are both RAM code under IOLC.
        ram_cursor=24'h00dd00;ram_emit(8'ha9);ram_emit(8'ha5);ram_emit(8'h8f);ram_emit(0);ram_emit(1);ram_emit(2);ram_emit(8'hdb);
        ram_cursor=24'h00e400;ram_emit(8'ha9);ram_emit(8'h5a);ram_emit(8'h8f);ram_emit(0);ram_emit(1);ram_emit(2);ram_emit(8'hdb);
        put(24'h020100,0);
        repeat(20)@(negedge core_clk);reset=0;
        packet(8'h43,16);packet(8'h41,131072);packet(8'h42,4096);packet(8'h46,4096);packet(8'h45,256);
        if(!rom_loaded)$fatal(1,"ROM not loaded");core_reset_request=0;polls=0;
        while(!dut.machine_i.cpu.WAIExec && polls<200000)begin @(negedge core_clk);polls++;end
        if(!dut.machine_i.cpu.WAIExec)$fatal(1,"CPU did not reach WAI pc%06x shadow%02x",dut.machine_i.cpu_addr,dut.machine_i.shadow);
        if(dut.machine_i.shadow[6]!==iolc_case[0])$fatal(1,"Wrong IOLC setting");
        repeat(100)@(negedge core_clk);test_irq=1;polls=0;
        while(!dut.machine_i.cpu.STPExec && polls<200000)begin @(negedge core_clk);polls++;end
        test_irq=0;
        if(!dut.machine_i.cpu.STPExec)$fatal(1,"ISR did not reach STP pc%06x",dut.machine_i.cpu_addr);
        polls=0;while(memory_busy && polls<100000)begin @(negedge core_clk);polls++;end
        actual_marker=memory[24'h020100>>3][7:0];
        $display("RESULT speed%0d emulation%0d iolc%0d vector%02x%02x marker%02x expected%02x reads%0d shadow%02x",speed,emulation_case,iolc_case,vector_hi,vector_lo,actual_marker,expected_marker,vector_reads,dut.machine_i.shadow);$fflush();
        if(vector_reads!=2 || actual_marker!==expected_marker || {vector_hi,vector_lo}!==(expect_rom ? 16'he400 : 16'hdd00))$fatal(1,"IOLC nativeIRQ result differs");
        $display("PASS: speed%0d emulation%0d iolc%0d IRQ vector%04x marker%02x",speed,emulation_case,iolc_case,{vector_hi,vector_lo},actual_marker);$fflush();
      end
      $finish;
    end
    initial begin #100000000;$fatal(1,"IOLC IRQ timeout");end
endmodule
