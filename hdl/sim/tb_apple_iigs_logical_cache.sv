// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
// Exercise the production raw-address cache through the P65 bus boundary.
// This transport fixture forces CPU outputs; ISA execution is tested separately.
// Every byte is synthetic except the assembled, GPL-licensed HDD ROM fixture.
module tb_apple_iigs_logical_cache;
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
    reg [7:0] rom[0:262143],hdd_rom[0:255];
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
                value=(kind==8'h40 || kind==8'h41) ? rom[i] : kind==8'h45 ? hdd_rom[i] : kind==8'h43 && i==1 ? speed : kind==8'h43 && i==0 ? config_rom : 0;
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
            packet(8'h42,4096,0);packet(8'h46,4096,0);packet(8'h45,256,0);
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
        // IOLC inhibit creates mixed physical mapping within raw00FFE8:
        // nativeIRQ vector FFEE/FFEF forces ROM, other VPB-low lanes remain RAM.
        write_byte(24'h00c035,8'h7f);
        memory[24'h00ffe8>>3]=64'h5555555555555555;
        memory[24'hf9ffe8>>3]=64'h6666666666666666;
        read_byte(24'h00ffe8,8'h55);
        bus_cycle(24'h00ffee,0,0,0,1,8'h66);
        bus_cycle(24'h00ffea,0,0,0,1,8'h55);
        bus_cycle(24'h00ffee,0,0,0,1,8'h66);
        read_byte(24'h00ffee,8'h55);
        mark("mixed-vector pull and ordinary LC RAM same8B word");
        // Restore IOLC via E0, which remains native when bank00C0xx is RAM.
        write_byte(24'he0c035,8'h3f);
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
        if(cases!=6)$fatal(1,"mapping coverage mismatch");
        $display("PASS: raw logical mapping total=%0d",cases);
        $finish;
    end
    initial begin #1000000000;$fatal(1,"acceleration throughput timeout");end
endmodule
