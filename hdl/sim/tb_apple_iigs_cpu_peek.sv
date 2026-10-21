// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
// Check CPU-only IO/Zip reads and independent native DMA byte selection.
// P65 bus-boundary comparisons also cover cache coherency and hot remapping.
// This transport fixture forces CPU outputs; ISA execution is tested separately.
// Every byte is synthetic except the assembled, GPL-licensed HDD ROM fixture.
module tb_apple_iigs_cpu_peek;
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
    localparam integer ITERATIONS=16384,WARMUP=256;
    localparam [15:0] LOOP_START=16'he080,LOOP_END=16'he085;
    localparam integer EXPECTED_ADVANCES=(ITERATIONS-WARMUP)*9-1;
    reg [7:0] rom[0:262143],hdd_rom[0:2303];
    integer cursor,current_speed=-1;
    longint unsigned primary_ticks=0,start_tick=0,elapsed_ticks=0,cpu_advances=0;
    longint unsigned miss_start=0,wait_start=0,hits_start=0,memory_reads=0;
    bit measuring=0,measured=0,software_max=0,cache_bypass=0;
    string rom_path;
    always @(posedge core_clk)primary_ticks=primary_ticks+1;
    task emit(input [7:0] value);begin rom[cursor]=value;cursor=cursor+1;end endtask
    task program_rom;
        begin
            for(integer i=0;i<131072;i=i+1)rom[i]=8'hea;
            rom[131068]=0;rom[131069]=8'he0;cursor=122880;
            emit(8'h78); // SEI during setup.
            emit(8'ha9);emit(8'h80);emit(8'h8d);emit(8'h36);emit(8'hc0); // Motherboard fast.
            if(software_max)begin
                emit(8'ha9);emit(8'h50);
                repeat(4)begin emit(8'h8d);emit(8'h5a);emit(8'hc0);end
                emit(8'ha9);emit(0);emit(8'h8d);emit(8'h5d);emit(8'hc0);
                emit(8'h8d);emit(8'h5b);emit(8'hc0); // Software100% uses installed maximum.
                if(cache_bypass)begin emit(8'ha9);emit(8'hf7);emit(8'h8d);emit(8'h59);emit(8'hc0);end
            end
            emit(8'h18);emit(8'hfb);emit(8'h18);emit(8'hc2);emit(8'h30); // Native, carry0, 16-bit A/X/Y.
            emit(8'ha2);emit(8'hff);emit(8'h01);emit(8'h9a); // Explicit S=$01FF.
            emit(8'ha9);emit(8'h34);emit(8'h12); // A=$1234.
            emit(8'ha0);emit(0);emit(0); // Y=0.
            emit(8'ha2);emit(ITERATIONS&255);emit(ITERATIONS>>8); // X=count.
            emit(8'h58); // Normal IRQ delay remains enabled; CLI allows acceleration.
            while(cursor<122880+128)emit(8'hea);
            emit(8'h1a);emit(8'hc8);emit(8'hca);emit(8'hd0);emit(8'hfb); // INC A; INY; DEX; BNE.
            emit(8'h8f);emit(0);emit(0);emit(2); // Exact final long RAM store.
            emit(8'hdb); // STP.
        end
    endtask
    task packet(input [7:0] kind,input integer size,input integer speed);
        reg [7:0] value;
        begin
            load_epoch=~load_epoch;
            for(integer i=0;i<size;i=i+1)begin
                value=(kind==8'h40 || kind==8'h41) ? rom[i] : kind==8'h49 ? hdd_rom[i] : kind==8'h43 && i==1 ? speed : kind==8'h43 && i==0 ? config_rom : 0;
                @(negedge core_clk);load_kind=kind;load_total=size;load_offset=i;load_data=value;
                load_first=i==0;load_last=i==size-1;load_valid=1;
                while(!load_ready)@(negedge core_clk);
                @(negedge core_clk);load_valid=0;
            end
            repeat(40)@(negedge core_clk);
            if(load_error)$fatal(1,"runtime load rejected kind=%x",kind);
        end
    endtask
    task start_machine(input integer speed);
        begin
            @(negedge core_clk);reset=1;core_reset_request=1;measuring=0;measured=0;current_speed=software_max ? 6 : speed;
            memory[24'h020000>>3]=0;
            repeat(20)@(negedge core_clk);reset=0;
            program_rom();packet(8'h43,16,speed);packet(8'h41,131072,0);
            packet(8'h42,4096,0);packet(8'h46,4096,0);packet(8'h49,2304,0);
            if(!rom_loaded)$fatal(1,"synthetic system/aux ROM load failed");
            core_reset_request=0;
        end
    endtask

    // Check the invariants that allow raw cached data to bypass MMU decoding.
    always @(posedge core_clk) if(!reset && !dut.machine_reset && !dut.hdd_dma)begin
        if(dut.cpu_valid && dut.cpu_memory_cycle && dut.physical_addr[2:0]!==dut.cpu_logical_addr[2:0])
            $fatal(1,"Raw/physical word offset differs");
        if(dut.l0_hit && dut.cpu_local_read)$fatal(1,"Raw cache hit overlaps local register");
    end
    bit config_rom=0;
    reg [23:0] forced_addr=0;
    reg forced_vpa=0,forced_vda=0,forced_we_n=1,forced_vpb_n=1;
    reg [7:0] forced_data=0;
    integer cases=0;
    task bus_cycle(input [23:0] addr,input bit wr,input byte data,input bit vector_n,input bit check,input byte expected);
        integer polls;
        byte received;
        begin
            @(negedge core_clk);
            pause_request=1;forced_addr=addr;forced_we_n=!wr;forced_data=data;forced_vpb_n=vector_n;forced_vda=1;forced_vpa=0;
            @(negedge core_clk);pause_request=0;polls=0;
            // Observe the exact pre-edge enable/data consumed by the P65 port.
            begin:wait_response
                while(polls<200000)begin
                    @(posedge core_clk);
                    if(dut.cpu_ce)begin received=dut.cpu_read_data;disable wait_response;end
                    polls++;
                end
                $fatal(1,"Mapping bus timeout addr=%06x speed%0d",addr,dut.cpu_speed);
            end
            @(negedge core_clk);pause_request=1;forced_vda=0;
            if(check && received!==expected)$fatal(1,"Mapping data addr%06x vector%0d actual%02x expected%02x phys%06x",addr,vector_n,received,expected,dut.physical_addr);
        end
    endtask
    task read_byte(input [23:0] addr,input byte expected);
        bus_cycle(addr,0,0,1,1,expected);
    endtask
    task write_byte(input [23:0] addr,input byte value);
        bus_cycle(addr,1,value,1,0,0);
    endtask
    task side_read(input [23:0] addr);
        bus_cycle(addr,0,0,1,0,0);
    endtask
    task mark(input string name);
        cases++;$display("PASS: raw logical mapping %s",name);
    endtask

    reg [7:0] f_shadow=0,f_cya=0,f_slot=0,f_newvideo=0;
    reg [10:0] f_flags=0;
    reg f_rom=0,f_dma=0,f_dma_we=0,f_unlock=0,f_card=0;
    reg [15:0] f_dma_addr=0;
    reg [7:0] f_dma_bank=0,f_c059=0,f_c05c=0;
    reg [3:0] f_sp=0;reg f_ms=0,f_disabled=0,f_bypass=0;
    reg [63:0] f_word=64'hf7e6d5c4b3a29180;
    integer decode_checks=0,peek_checks=0,lane_checks=0,dma_decode_checks=0;
    reg [31:0] rng=32'h71623854;
    function automatic [7:0] expected_zip(input [2:0] selector);
      case(selector)
        1:expected_zip=f_c059;
        2:expected_zip={f_ms,f_sp[2:0],4'hf};
        3:expected_zip={f_ms,1'b1,1'b0,f_disabled,4'h0};
        4:expected_zip=f_c05c;
        default:expected_zip=0;
      endcase
    endfunction
    task check_decode;
      bit bank_io,external_io,predicted_io,old_local;
      begin
        #0.001;
        bank_io=((forced_addr[23:16]<=1 && !f_shadow[6]) || forced_addr[23:16]==8'he0 || forced_addr[23:16]==8'he1 ||
                 (f_cya[4] && forced_addr[23:16]>=2 && forced_addr[23:16]<=8'h7f));
        external_io=((forced_addr[23:16]<=1 && !f_shadow[6]) || forced_addr[23:16]==8'he0 || forced_addr[23:16]==8'he1) &&
                    forced_addr[15:0]>=16'hc090 && forced_addr[15:0]<16'hc100 && f_slot[forced_addr[6:4]];
        predicted_io=bank_io && forced_addr[15:8]==8'hc0 && !external_io;
        old_local=dut.machine_i.IO && !dut.machine_i.we && dut.machine_i.zip_unlocked && forced_addr[7:3]==5'b01011;
        if(dut.machine_i.cpu_peek_io!==predicted_io || dut.machine_i.IO!==predicted_io)
          $fatal(1,"CPU peek MMU differs addr%06x shadow%02x cya%02x slot%02x actual%0b original%0b expected%0b",forced_addr,f_shadow,f_cya,f_slot,dut.machine_i.cpu_peek_io,dut.machine_i.IO,predicted_io);
        if(dut.cpu_local_read!==old_local)$fatal(1,"CPU local classifier differs without DMA");
        if(dut.physical_addr[2:0]!==forced_addr[2:0])$fatal(1,"CPU physical/raw byte lane changed");
        if(dut.cpu_local_data!==dut.machine_i.zip_rdata)$fatal(1,"CPU Zip peek differs without DMA");
        decode_checks++;
      end
    endtask
    task focused_cpu_peek;
      byte retained;
      reg [15:0] addresses[0:15];
      begin
        pause_request=1;forced_vda=1;forced_vpa=0;forced_we_n=1;
        addresses[0]=16'hc050;addresses[1]=16'hc057;addresses[2]=16'hc058;addresses[3]=16'hc059;
        addresses[4]=16'hc05a;addresses[5]=16'hc05b;addresses[6]=16'hc05c;addresses[7]=16'hc05f;
        addresses[8]=16'hc060;addresses[9]=16'hc08f;addresses[10]=16'hc090;addresses[11]=16'hc0ff;
        addresses[12]=16'hc100;addresses[13]=16'hd000;addresses[14]=16'hffee;addresses[15]=16'hffea;
        force dut.machine_i.shadow=f_shadow;
        force dut.machine_i.CYAREG=f_cya;
        force dut.machine_i.SLTROMSEL=f_slot;
        force dut.machine_i.NEWVIDEO=f_newvideo;
        force dut.machine_i.LCRAM2=f_flags[0];force dut.machine_i.RDROM=f_flags[1];force dut.machine_i.LC_WE=f_flags[2];
        force dut.machine_i.ALTZP=f_flags[3];force dut.machine_i.RAMRD=f_flags[4];force dut.machine_i.RAMWRT=f_flags[5];
        force dut.machine_i.STORE80=f_flags[6];force dut.machine_i.PAGE2=f_flags[7];force dut.machine_i.HIRES_MODE=f_flags[8];
        force dut.machine_i.rom_select=f_rom;
        force dut.machine_i.hdd_dma=f_dma;force dut.machine_i.hdd_dma_we=f_dma_we;
        force dut.machine_i.hdd_dma_addr={f_dma_bank,f_dma_addr};force dut.machine_i.DMAREG=f_dma_bank;
        force dut.machine_i.zip_unlocked_raw=f_unlock;force dut.machine_i.zip_regs_en=f_card;
        force dut.machine_i.zipgs.reg_c059=f_c059;force dut.machine_i.zipgs.reg_c05c=f_c05c;
        force dut.machine_i.zipgs.sp=f_sp;force dut.machine_i.zipgs.ms_toggle=f_ms;force dut.machine_i.zipgs.disabled=f_disabled;
        force dut.cache_disable=f_bypass;
        force dut.cache_i.lookup_data=f_word;force dut.cache_i.bypass_data_q=f_word;
        f_dma=0;f_unlock=1;f_card=1;f_flags=0;f_cya=8'h80;f_newvideo=0;
        // Exhaust every bank/slot-selection byte and IOLC/all-bank/RW mode.
        for(integer bank=0;bank<256;bank++)for(integer slot=0;slot<256;slot++)
          for(integer cfg=0;cfg<8;cfg++)begin
            forced_addr={bank[7:0],16'hc059};f_slot=slot;f_shadow={1'b0,cfg[0],6'h3f};
            f_cya={3'b100,cfg[1],4'b0};forced_we_n=!cfg[2];check_decode();
          end
        // All register/neighbor/vector addresses, every bank, all ROM/VPB modes,
        // with independently varied complete MMU input state and Zip visibility.
        for(integer bank=0;bank<256;bank++)for(integer mode=0;mode<16;mode++)
          for(integer address=0;address<16;address++)begin
            rng={rng[30:0],rng[31]^rng[21]^rng[1]^rng[0]};
            forced_addr={bank[7:0],addresses[address]};f_slot=rng[7:0];f_shadow=rng[15:8];
            f_cya={3'b100,mode[0],4'b0};f_newvideo=rng[23:16];f_flags=rng[31:21];
            f_rom=mode[1];forced_vpb_n=mode[2];forced_we_n=mode[3];f_unlock=mode[0];f_card=mode[1];check_decode();
          end
        $display("PASS: CPU-only MMU bank/config comparisons=%0d",decode_checks);
        // CPU and DMA are deliberately in different IO/bank classes. The pure
        // CPU overlay must remain CPU-addressed while original native IO stays
        // selected by the DMA bus. Neither implies permission to advance CPU.
        f_dma=1;f_card=1;f_unlock=1;forced_we_n=1;f_dma_we=0;
        for(integer bank=0;bank<256;bank++)for(integer cfg=0;cfg<4;cfg++)
          for(integer native_class=0;native_class<3;native_class++)for(integer selector=0;selector<8;selector++)begin
            bit expected_cpu_io,expected_native_io,expected_native_external;
            forced_addr={bank[7:0],16'hc058}+selector;
            f_shadow={1'b0,cfg[0],6'h3f};f_cya={3'b100,cfg[1],4'b0};f_slot=8'hff;
            f_dma_bank=(bank+127)&255;
            f_dma_addr=native_class==0 ? 16'hc059 : native_class==1 ? 16'hc0f2 : 16'h1003;
            #0.001;
            expected_cpu_io=((bank<=1 && !f_shadow[6]) || bank==224 || bank==225 || (f_cya[4] && bank>=2 && bank<=127));
            expected_native_external=((f_dma_bank<=1 && !f_shadow[6]) || f_dma_bank==224 || f_dma_bank==225) && f_dma_addr==16'hc0f2;
            expected_native_io=f_dma_addr[15:8]==8'hc0 && !expected_native_external &&
              ((f_dma_bank<=1 && !f_shadow[6]) || f_dma_bank==224 || f_dma_bank==225 || (f_cya[4] && f_dma_bank>=2 && f_dma_bank<=127));
            if(dut.cpu_local_read!==expected_cpu_io || dut.machine_i.IO!==expected_native_io || dut.cpu_ce)
              $fatal(1,"Independent CPU/DMA IO classification failed cpu%06x dma%02x%04x",forced_addr,f_dma_bank,f_dma_addr);
            dma_decode_checks++;
          end
        $display("PASS: independent CPU/DMA IO class comparisons=%0d",dma_decode_checks);
        // Both Zip read selectors independently choose from the identical state.
        f_card=1;f_unlock=1;f_shadow=8'h3f;f_cya=8'h80;forced_we_n=1;forced_vpb_n=1;f_dma=1;f_dma_bank=8'he0;
        for(integer state=0;state<256;state++)for(integer cpu_lane=0;cpu_lane<8;cpu_lane++)
          for(integer native_lane=0;native_lane<8;native_lane++)begin
            f_c059=state;f_c05c=255-state;f_sp=state[3:0];f_ms=state[4];f_disabled=state[5];
            forced_addr=24'h00c058+cpu_lane;f_dma_addr=16'hc058+native_lane;#0.001;
            if(dut.cpu_local_data!==expected_zip(cpu_lane) || dut.machine_i.zip_rdata!==expected_zip(native_lane))
              $fatal(1,"Zip dual selector mismatch CPU%0d native%0d state%02x",cpu_lane,native_lane,state);
            if(dut.cpu_ce)$fatal(1,"CPU advanced during HDD DMA selector test");
            peek_checks++;
          end
        $display("PASS: CPU/native independent Zip selector comparisons=%0d",peek_checks);
        // Raw CPU fallback and native DMA byte lanes deliberately disagree.
        f_c059=0;f_card=0;forced_addr=24'h021000;f_dma_addr=16'h1000;f_dma_bank=2;
        for(integer bypass=0;bypass<2;bypass++)for(integer valid=0;valid<2;valid++)
          for(integer cpu_lane=0;cpu_lane<8;cpu_lane++)for(integer native_lane=0;native_lane<8;native_lane++)begin
            f_bypass=bypass;forced_vda=valid;forced_addr=24'h021000+cpu_lane;f_dma_addr=16'h1000+native_lane;
            f_dma=0;#0.001;
            if(dut.cpu_read_data!==dut.bus_read_data || dut.cpu_read_data!==f_word[cpu_lane*8+:8])
              $fatal(1,"CPU raw fallback differs without DMA lane%0d valid%0d bypass%0d",cpu_lane,valid,bypass);
            f_dma=1;#0.001;
            if(dut.cpu_read_data!==f_word[cpu_lane*8+:8] || dut.native_memory_data!==f_word[native_lane*8+:8] || dut.bus_read_data!==f_word[native_lane*8+:8])
              $fatal(1,"Independent CPU/native lane mismatch cpu%0d native%0d",cpu_lane,native_lane);
            if(dut.cpu_ce || dut.l0_hit)$fatal(1,"DMA permitted CPU advance/cache hit");
            lane_checks++;
          end
        retained=dut.machine_i.cpu_last_bus;
        repeat(8)begin @(posedge core_clk);#0.001;if(dut.cpu_ce || dut.machine_i.cpu_last_bus!==retained)$fatal(1,"DMA changed CPU/floating state");end
        $display("PASS: CPU/native fallback lanes cached+bypass valid+invalid comparisons=%0d",lane_checks);
      end
    endtask

    initial begin
        if(!$value$plusargs("HDD_ROM=%s",rom_path))$fatal(1,"licensed HDD ROM fixture required");
        $readmemh(rom_path,hdd_rom);
        force dut.machine_i.cpu_addr=forced_addr;
        force dut.machine_i.cpu_vpa=forced_vpa;
        force dut.machine_i.cpu_vda=forced_vda;
        force dut.machine_i.cpu_we_n=forced_we_n;
        force dut.machine_i.cpu_dout=forced_data;
        force dut.machine_i.cpu_vpb=forced_vpb_n;
        force dut.machine_i.irq_slowdown=1'b0;
        pause_request=1;
        start_machine(6);
        if($test$plusargs("FOCUSED_ONLY"))begin focused_cpu_peek();$finish;end
        write_byte(24'h00c036,8'h80);
        // Read-soft-switch LC bank changes must flush raw aliases on native
        // retirement, after the read data already crossed the native bridge.
        write_byte(24'h00c035,8'h3f); // Disable video shadow, retain IOLC.
        memory[24'h00c000>>3]=64'h1111111111111111;
        memory[24'h00d000>>3]=64'h2222222222222222;
        // DDR fixtures are written before the first read of their 32-byte
        // line: the cache is coherent only with its own writes.
        memory[24'h00c008>>3]=64'h9999999999999999;
        side_read(24'h00c088); // LC bank1 RAM.
        read_byte(24'h00d000,8'h11);read_byte(24'h00d000,8'h11);
        side_read(24'h00c080); // LC bank2 RAM.
        read_byte(24'h00d000,8'h22);
        side_read(24'h00c088);read_byte(24'h00d000,8'h11);
        mark("LC read-soft-switch bank1/bank2 alias");
        read_byte(24'h00d008,8'h99);
        @(negedge core_clk);forced_addr=24'h00d000;forced_vda=0;forced_vpa=0;forced_vpb_n=1;
        #1;
        if(!dut.l0_valid[0] || dut.l0_hit)$fatal(1,"Invalid-cycle raw cache validity gating failed");
        begin:invalid_floating
            byte expected_bus;
            pause_request=0;
            @(posedge core_clk);
            expected_bus=dut.bus_read_data;
            if(!dut.cpu_ce)$fatal(1,"Invalid internal CPU cycle was not enabled");
            #0.001;
            if(dut.machine_i.cpu_last_bus!==expected_bus)
                $fatal(1,"Invalid cycle changed retained floating bus %02x/%02x",dut.machine_i.cpu_last_bus,expected_bus);
        end
        @(negedge core_clk);pause_request=1;
        mark("invalid internal cycle retains L1 floating bus");

        memory[24'h001000>>3]=64'h3333333333333333;
        memory[24'h011000>>3]=64'h4444444444444444;
        write_byte(24'h00c002,0); // Main RAM read.
        read_byte(24'h001000,8'h33);read_byte(24'h001000,8'h33);
        write_byte(24'h00c003,0); // Aux RAM read.
        read_byte(24'h001000,8'h44);
        write_byte(24'h00c002,0);read_byte(24'h001000,8'h33);
        mark("RAMRD main/aux alias");
        // IOLC inhibit maps every vector lane to RAM, including native IRQ.
        write_byte(24'h00c035,8'h7f);
        memory[24'h00ffe8>>3]=64'h5555555555555555;
        memory[24'hf9ffe8>>3]=64'h6666666666666666;
        read_byte(24'h00ffe8,8'h55);
        bus_cycle(24'h00ffee,0,0,0,1,8'h55);
        bus_cycle(24'h00ffea,0,0,0,1,8'h55);
        bus_cycle(24'h00ffee,0,0,0,1,8'h55);
        read_byte(24'h00ffee,8'h55);
        mark("IOLC-inhibited vectors and ordinary reads use RAM");
        // Restore IOLC via E0, which remains native when bank00C0xx is RAM.
        write_byte(24'he0c035,8'h3f);
        // With the language card mapped and RDROM=0, ordinary reads use RAM
        // but vector pulls use ROM. A warm raw cache word must not alias them.
        read_byte(24'h00ffee,8'h55);
        bus_cycle(24'h00ffee,0,0,0,1,8'h66);
        bus_cycle(24'h00ffea,0,0,0,1,8'h66);
        read_byte(24'h00ffee,8'h55);
        mark("mapped LC RAM and ROM vectors share a raw cache word safely");
        side_read(24'h00c081); // RDROM=1.
        memory[24'hf9d008>>3]=64'h7777777777777777;
        read_byte(24'h00d008,8'h77);
        // Warm reset restores MMU mapping. Force RAMRD first, then verify main
        // mapping and logical line coherency after the reset release.
        write_byte(24'h00c003,0);read_byte(24'h001000,8'h44);
        @(negedge core_clk);core_reset_request=1;repeat(20)@(negedge core_clk);
        core_reset_request=0;
        while(dut.machine_reset || dut.machine_reset_raw)@(negedge core_clk);
        write_byte(24'h00c036,8'h80);
        read_byte(24'h001000,8'h33);
        mark("warm-reset mapping and raw cache invalidation");
        // Load both public synthetic ROMs, then select each live using the
        // actual configuration protocol. No ROM bytes from Apple are used.
        core_reset_request=1;
        for(integer i=0;i<262144;i++)rom[i]=8'h88;
        packet(8'h40,262144,0);
        for(integer i=0;i<131072;i++)rom[i]=8'h99;
        packet(8'h41,131072,0);packet(8'h44,4096,0);
        core_reset_request=0;
        while(dut.machine_reset || dut.machine_reset_raw)@(negedge core_clk);
        config_rom=0;packet(8'h43,16,6);
        read_byte(24'hffe000,8'h99);read_byte(24'hffe000,8'h99);
        config_rom=1;packet(8'h43,16,6);
        read_byte(24'hffe000,8'h88);
        config_rom=0;packet(8'h43,16,6);
        read_byte(24'hffe000,8'h99);
        mark("hot ROM1/ROM3 config selection");
        focused_cpu_peek();
        if(cases!=7)$fatal(1,"mapping coverage mismatch");
        $display("PASS: raw logical mapping total=%0d",cases);
        $finish;
    end
    initial begin #1000000000;$fatal(1,"acceleration throughput timeout");end
endmodule
