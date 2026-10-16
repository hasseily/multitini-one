// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
// Measure real production CPU execution through the normal ZipGS speed path.
// Original synthetic native program; assembled licensed HDD ROM is auxiliary.
module tb_gs_acceleration;
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
    reg [7:0] rom[0:131071],hdd_rom[0:255];
    integer cursor,current_speed=-1;
    longint unsigned primary_ticks=0,start_tick=0,elapsed_ticks=0,cpu_advances=0;
    longint unsigned miss_start=0,wait_start=0,hits_start=0,memory_reads=0;
    bit measuring=0,measured=0,software_max=0,cache_bypass=0;
    string rom_path;
    always @(posedge core_clk)primary_ticks=primary_ticks+1;
    // Observe the same edge and pre-edge enable that execute real CPU state.
    always @(posedge core_clk)begin
        if(!dut.machine_reset && dut.machine_i.cpu.CE && dut.machine_i.cpu.RDY_IN)begin
            if(!measured && !measuring && dut.machine_i.cpu.STATE==0 &&
               dut.machine_i.cpu.PC==LOOP_START && dut.machine_i.cpu.X==ITERATIONS-WARMUP)begin
                if(dut.machine_i.cpu.A!==16'h1334 || dut.machine_i.cpu.Y!==WARMUP)
                    $fatal(1,"warmup architectural outcome incorrect");
                measuring=1;start_tick=primary_ticks;cpu_advances=0;memory_reads=0;
                miss_start=dut.cache_i.misses;wait_start=dut.cache_i.wait_cycles;hits_start=dut.cache_i.hits;
            end
            if(measuring)begin
                if(dut.machine_i.fast_thresh!=(current_speed>=4 ? 0 : 4-current_speed) || dut.machine_i.irq_slowdown ||
                   dut.machine_i.CYAREG[7]!==1'b1 || !dut.machine_i.zip_regs_en)
                    $fatal(1,"live acceleration path not at requested normal ZipGS setting");
                if(dut.machine_i.cpu.STATE==0 && dut.machine_i.cpu.PC==LOOP_END)begin
                    elapsed_ticks=primary_ticks-start_tick;measuring=0;measured=1;
                    if(cpu_advances!=EXPECTED_ADVANCES || (!cache_bypass && dut.cache_i.misses!=miss_start) ||
                       (cache_bypass && (dut.cache_i.misses-miss_start!=memory_reads || memory_reads==0)))
                        $fatal(1,"measured loop cycles/cache mismatch advances=%0d expected=%0d misses=%0d memory_reads=%0d",cpu_advances,
                            EXPECTED_ADVANCES,dut.cache_i.misses-miss_start,memory_reads);
                    $display("MEASURE speed=%0d primary_ticks=%0d cpu_advances=%0d iterations=%0d cache_misses=%0d cache_hits=%0d cache_wait_ticks=%0d software_max=%0d cache_bypass=%0d memory_reads=%0d",
                        current_speed,elapsed_ticks,cpu_advances,ITERATIONS-WARMUP,dut.cache_i.misses-miss_start,
                        dut.cache_i.hits-hits_start,dut.cache_i.wait_cycles-wait_start,software_max,cache_bypass,memory_reads);
                end else begin
                    cpu_advances=cpu_advances+1;
                    if(dut.cpu_valid && dut.cpu_memory_cycle && !dut.bus_write)memory_reads=memory_reads+1;
                end
            end
        end
    end
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
                value=kind==8'h41 ? rom[i] : kind==8'h45 ? hdd_rom[i] : kind==8'h43 && i==1 ? speed : 0;
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
    task wait_stopped;
        integer polls;
        begin
            polls=0;
            while(!dut.machine_i.cpu.STPExec && polls<5000000)begin @(negedge core_clk);polls=polls+1;end
            if(!dut.machine_i.cpu.STPExec || !measured || load_error)
                $fatal(1,"accelerated CPU failed to stop: speed=%0d PC=%x X=%x measured=%0d",current_speed,
                    dut.machine_i.cpu.PC,dut.machine_i.cpu.X,measured);
            // Writes are posted: compare DDR only after the cache drained.
            polls=0;while(memory_busy && polls<100000)begin @(negedge core_clk);polls=polls+1;end
            if(memory_busy)$fatal(1,"posted DDR writes did not drain");
            if(dut.machine_i.cpu.A!==16'h5234 || dut.machine_i.cpu.X!==0 || dut.machine_i.cpu.Y!==16'h4000 ||
               dut.machine_i.cpu.P!==9'h002 || dut.machine_i.cpu.SP!==16'h01ff ||
               dut.machine_i.cpu.D!==0 || dut.machine_i.cpu.DBR!==0 || dut.machine_i.cpu.PBR!==0 ||
               dut.machine_i.cpu.PC!==16'he08a || memory[24'h020000>>3][15:0]!==16'h5234)
                $fatal(1,"accelerated native loop final state incorrect A=%x X=%x Y=%x P=%x SP=%x PC=%x RAM=%x",
                    dut.machine_i.cpu.A,dut.machine_i.cpu.X,dut.machine_i.cpu.Y,dut.machine_i.cpu.P,dut.machine_i.cpu.SP,
                    dut.machine_i.cpu.PC,memory[24'h020000>>3]);
            $display("PASS: accelerated native loop speed=%0d exact registers and RAM",current_speed);
        end
    endtask
    initial begin
        if(!$value$plusargs("HDD_ROM=%s",rom_path))$fatal(1,"licensed HDD ROM fixture required");
        $readmemh(rom_path,hdd_rom);
        if(!$test$plusargs("BYPASS_ONLY"))begin
            for(integer speed=0;speed<7;speed=speed+1)begin start_machine(speed);wait_stopped();end
            software_max=1;start_machine(3);wait_stopped();
        end
        software_max=1;cache_bypass=1;start_machine(6);wait_stopped();
        $finish;
    end
    initial begin #1000000000;$fatal(1,"acceleration throughput timeout");end
endmodule
