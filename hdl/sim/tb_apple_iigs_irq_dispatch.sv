// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
// Exercise the complete production adapter with original synthetic 65816
// programs and the reproducibly assembled, licensed HDD slot ROM.
module tb_apple_iigs_irq_dispatch;
    reg core_clk=0;always #8.730 core_clk=~core_clk;
    reg reset=1,core_reset_request=1,pause_request=0,clear_load_error=0;
    reg [15:0] input_code=0,input_value=0;reg input_valid=0;wire input_ready;
    reg [7:0] load_data=0,load_kind=0;
    reg load_valid=0,load_first=0,load_last=0,load_epoch=0,load_abort=0,load_abort_epoch=0;
    reg [31:0] load_offset=0,load_total=0;wire load_ready;
    wire [23:0] memory_addr;wire memory_write;wire [63:0] memory_wdata;wire [7:0] memory_wstrb;
    wire memory_valid,memory_ready;reg [63:0] memory_rdata=0;reg memory_rvalid=0,memory_fault=0;
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
    reg busy=0,pending_write=0;reg [23:0] pending_addr;reg [63:0] pending_data;reg [7:0] pending_mask;
    integer delay_count=0,writes=0;
    assign memory_ready=!busy;
    always @(posedge core_clk)begin
        memory_rvalid<=0;
        if(memory_valid && memory_ready)begin
            pending_addr<=memory_addr;pending_data<=memory_wdata;pending_mask<=memory_wstrb;
            pending_write<=memory_write;delay_count<=3+memory_addr[5:3];busy<=1;
        end else if(busy)begin
            if(delay_count!=0)delay_count<=delay_count-1;
            else begin
                if(pending_write)begin
                    for(integer lane=0;lane<8;lane=lane+1)
                        if(pending_mask[lane])memory[pending_addr[23:3]][lane*8+:8]<=pending_data[lane*8+:8];
                    writes<=writes+1;
                end
                memory_rdata<=memory[pending_addr[23:3]];memory_rvalid<=1;busy<=0;
            end
        end
    end

    // Original synthetic ROM checks every native dispatch byte and a real BRK.
    reg [7:0] rom[0:131071];integer cursor,read_count=0,speed=0;
    task emit(input [7:0] x);begin rom[cursor]=x;cursor++;end endtask
    task packet(input [7:0] kind,input integer size);
      reg [7:0] value;
      begin
        load_epoch=~load_epoch;
        for(integer i=0;i<size;i++)begin
          value=kind==8'h41 ? rom[i]:kind==8'h43 && i==1 ? speed:0;
          @(negedge core_clk);load_kind=kind;load_total=size;load_offset=i;load_data=value;
          load_first=i==0;load_last=i==size-1;load_valid=1;
          while(!load_ready)@(negedge core_clk);
          @(negedge core_clk);load_valid=0;
        end
        repeat(40)@(negedge core_clk);
        if(load_error)$fatal(1,"Synthetic loader error");
      end
    endtask
    always @(posedge core_clk)if(!dut.machine_reset && dut.cpu_ce && dut.machine_i.cpu_we_n && dut.cpu_valid &&
        dut.machine_i.cpu_addr>=24'h00c071 && dut.machine_i.cpu_addr<=24'h00c07f)begin
      read_count<=read_count+1;
      $display("DISPATCH_READ addr=%x data=%x expected=%x physical=%x memory_cycle=%b native=%b",dut.machine_i.cpu_addr,dut.cpu_read_data,rom[65536+dut.machine_i.cpu_addr[15:0]],dut.physical_addr,dut.cpu_memory_cycle,dut.native_pending);
      if(dut.cpu_read_data!==rom[65536+dut.machine_i.cpu_addr[15:0]])$fatal(1,"Native IRQ dispatch must return exact system-ROM byte");
    end
    always @(posedge core_clk)if(!dut.machine_reset && dut.cpu_ce && dut.machine_i.IO &&
        dut.machine_i.cpu_we_n && dut.machine_i.cpu_addr[15:0]==16'hc070 && dut.cpu_memory_cycle)
      $fatal(1,"Ordinary C070 IO must not request mapped ROM");
    initial begin
      integer polls;
      for(integer setting=0;setting<2;setting++)begin
      speed=setting==0 ? 0:6;read_count=0;reset=1;core_reset_request=1;
      for(integer i=0;i<131072;i++)rom[i]=8'hea;
      rom[131068]=0;rom[131069]=8'he0;
      rom[131070]=8'h74;rom[131071]=8'hc0;
      for(integer i=0;i<15;i++)rom[65536+16'hc071+i]=8'h80+i;
      rom[65536+16'hc074]=8'h5c;rom[65536+16'hc075]=8'h10;
      rom[65536+16'hc076]=8'h00;rom[65536+16'hc077]=8'he1;
      cursor=122880;emit(8'h78); // SEI, stock native path.
      emit(8'ha9);emit(8'hdb);emit(8'h8f);emit(8'h10);emit(0);emit(8'he1);
      emit(8'h58); // CLI also exercises the configured maximum CPU setting.
      emit(8'had);emit(8'h70);emit(8'hc0); // Ordinary IO needs no ROM lookup.
      for(integer i=0;i<15;i++)begin
        emit(8'had);emit(8'h71+i);emit(8'hc0);
        emit(8'h8f);emit(i);emit(1);emit(2);
      end
      emit(8'h00);emit(8'hea); // BRK vector enters the native ROM dispatch.
      repeat(20)@(negedge core_clk);reset=0;
      packet(8'h43,16);packet(8'h41,131072);packet(8'h42,4096);packet(8'h46,4096);packet(8'h45,256);
      if(!rom_loaded)$fatal(1,"ROM not loaded");
      core_reset_request=0;polls=0;
      while(!dut.machine_i.cpu.STPExec && polls<1000000)begin @(negedge core_clk);polls++;end
      if(!dut.machine_i.cpu.STPExec || read_count!=19)$fatal(1,"Expected15data+4JML dispatch reads and target STP, reads=%0d",read_count);
      for(integer i=0;i<15;i++)if(memory[(24'h020100+i)>>3][(i&7)*8+:8]!==rom[65536+16'hc071+i])$fatal(1,"RAM dispatch readback mismatch");
      $display("PASS: speed=%0d native dispatch15ROMdata reads plusBRK/JML E1:0010 exact19byte reads",speed);
      end
      $finish;
    end
endmodule
