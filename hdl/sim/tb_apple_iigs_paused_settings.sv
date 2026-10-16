// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
// Apply live menu-style settings while paused, then measure real CPU progress.
// Original synthetic native program; assembled licensed HDD ROM is auxiliary.
module tb_apple_iigs_paused_settings;
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

    reg [7:0] rom[0:131071],hdd_rom[0:255];integer cursor;
    integer selected=0,config_flags=0,case_number=0;bit sei_loop=0,live=0;
    longint unsigned primary_ticks=0;
    string rom_path;
    always @(posedge core_clk)begin
        primary_ticks=primary_ticks+1;
        if(live&&(dut.machine_reset||dut.machine_reset_raw||reset||core_reset_request))
            $fatal(1,"Live settings unexpectedly reset machine");
    end
    task emit(input [7:0] value);begin rom[cursor]=value;cursor++;end endtask
    task program_rom;
        begin
            for(integer i=0;i<131072;i++)rom[i]=8'hea;
            rom[131068]=0;rom[131069]=8'he0;cursor=122880;
            emit(8'h78);emit(8'ha9);emit(8'h80);emit(8'h8d);emit(8'h36);emit(8'hc0);
            emit(8'h18);emit(8'hfb);emit(8'hc2);emit(8'h30);
            emit(8'ha9);emit(8'h34);emit(8'h12);emit(8'ha0);emit(0);emit(0);
            if(!sei_loop)emit(8'h58);
            while(cursor<122880+128)emit(8'hea);
            emit(8'h1a);emit(8'hc8);emit(8'h80);emit(8'hfc); // INC A; INY; BRA loop.
        end
    endtask
    task packet(input [7:0] kind,input integer size);
        reg [7:0] value;
        begin
            load_epoch=~load_epoch;
            for(integer i=0;i<size;i++)begin
                value=kind==8'h41 ? rom[i]:kind==8'h45 ? hdd_rom[i]:
                    kind==8'h43&&i==1 ? 8'(selected):kind==8'h43&&i==3 ? 8'(config_flags):0;
                @(negedge core_clk);load_kind=kind;load_total=size;load_offset=i;load_data=value;
                load_first=i==0;load_last=i==size-1;load_valid=1;
                while(!load_ready)@(negedge core_clk);
                @(negedge core_clk);load_valid=0;
            end
            repeat(40)@(negedge core_clk);
            if(load_error)$fatal(1,"Configuration packet rejected");
        end
    endtask
    task start_machine;
        begin
            live=0;reset=1;core_reset_request=1;pause_request=0;
            repeat(20)@(negedge core_clk);reset=0;
            program_rom();packet(8'h43,16);packet(8'h41,131072);
            packet(8'h42,4096);packet(8'h46,4096);packet(8'h45,256);
            core_reset_request=0;
            while(dut.machine_reset || dut.machine_reset_raw || dut.machine_i.cpu.Y<64)@(negedge core_clk);
            live=1;
        end
    endtask
    task warm;
        integer advances;
        begin
            advances=0;
            while(advances<128)begin
                @(posedge core_clk);if(dut.cpu_ce)advances++;
            end
            @(negedge core_clk);
        end
    endtask
    task live_config;
        reg [15:0] a,x,y,pc,sp;reg [8:0] p;reg [7:0] state;
        begin
            @(negedge core_clk);pause_request=1;
            repeat(16)@(negedge core_clk);
            a=dut.machine_i.cpu.A;x=dut.machine_i.cpu.X;y=dut.machine_i.cpu.Y;
            pc=dut.machine_i.cpu.PC;sp=dut.machine_i.cpu.SP;p=dut.machine_i.cpu.P;state=dut.machine_i.cpu.STATE;
            packet(8'h43,16);
            if(dut.config_bytes[15:8]!==8'(selected)||dut.config_bytes[31:24]!==8'(config_flags)||
               dut.machine_i.zipgs.host_prev!==3'(selected)||dut.machine_i.zip_speed_code!==3'(selected))
                $fatal(1,"Paused loader/Zip speed setting did not apply");
            if(dut.machine_i.zip_irq_delay_en!==!config_flags[4])$fatal(1,"Paused IRQ setting did not apply");
            if(dut.machine_i.cpu.A!==a||dut.machine_i.cpu.X!==x||dut.machine_i.cpu.Y!==y||dut.machine_i.cpu.PC!==pc||
               dut.machine_i.cpu.SP!==sp||dut.machine_i.cpu.P!==p||dut.machine_i.cpu.STATE!==state)
                $fatal(1,"Paused config changed architectural CPU state");
            pause_request=0;warm();
        end
    endtask
    task measure(input integer effective);
        integer count,cycles,misses;longint unsigned first_tick,elapsed;
        begin
            count=0;first_tick=0;elapsed=0;cycles=0;misses=dut.cache_i.misses;
            while(count<=4096)begin
                @(posedge core_clk);cycles++;
                if(dut.cpu_ce)begin
                    if(dut.cpu_speed!==3'(effective))$fatal(1,"Incorrect live speed %0d expected%0d",dut.cpu_speed,effective);
                    if(count==0)first_tick=primary_ticks;
                    if(count==4096)elapsed=primary_ticks-first_tick;
                    count++;
                end
                if(cycles>200000)$fatal(1,"Live speed measurement timed out");
            end
            if(dut.cache_i.misses!=misses)$fatal(1,"Warm register loop missed DDR cache");
            $display("PAUSED_MEASURE case=%0d requested=%0d flags=%0d i_flag=%0d effective=%0d ticks=%0d advances=4096",case_number,selected,config_flags,dut.machine_i.cpu_i_flag,effective,elapsed);
            case_number++;
            @(negedge core_clk);
        end
    endtask
    initial begin
        if(!$value$plusargs("HDD_ROM=%s",rom_path))$fatal(1,"Licensed HDD bootstrap required");
        $readmemh(rom_path,hdd_rom);
        selected=0;config_flags=0;sei_loop=0;start_machine();warm();
        // Same running CPU, menu-style pause/config/resume; no reset per rate.
        for(integer speed=0;speed<7;speed++)begin
            selected=speed;live_config();measure(speed);
        end
        // Isolate the default IRQ-delay policy with a naturally masked-I loop.
        selected=6;config_flags=0;sei_loop=1;start_machine();warm();
        live_config();measure(0);
        config_flags=16;live_config();measure(6);
        // Explicitly selecting compatibility ON again must remain effective.
        config_flags=0;live_config();measure(0);
        if(case_number!=10)$fatal(1,"Incomplete paused-settings matrix");
        $display("PASS: live paused settings all7 rates plus IRQ On/Off/On, exact config and frozen architecture, no reset");
        $finish;
    end
    initial begin #1000000000;$fatal(1,"Paused settings regression timed out");end
endmodule
