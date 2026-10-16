// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
// Exercise the complete production adapter with original synthetic 65816
// programs and the reproducibly assembled, licensed HDD slot ROM.
module tb_apple_iigs_adapter;
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
    reg [7:0] rom[0:131071],hdd_rom[0:2303];integer program_cursor;
    integer trace_count=0;
    reg [23:0] trace_addr=24'hffffff;
    always @(negedge core_clk)if($test$plusargs("TRACE") && !dut.machine_reset && trace_addr!=dut.machine_i.cpu_addr && trace_count<64)begin
        trace_count=trace_count+1;
        trace_addr=dut.machine_i.cpu_addr;
        $display("CPU addr=%x physical=%x in=%x rom=%b write=%b data=%x",dut.machine_i.cpu_addr,dut.physical_addr,
            dut.machine_i.cpu_din,dut.rom_ce,dut.bus_write,dut.bus_write_data);
    end
    string rom_path;
    bit disk_bypass=0;
    always @(posedge dut.clk14)if($test$plusargs("TRACE_HDD") && !dut.machine_reset && dut.native_commit_q)
        $display("NATIVE_TRACE addr=%x we=%b data=%x hddsel=%b phi0=%b cmd=%x unit=%x block=%x state=%d request=%b mounted=%b",
            dut.machine_i.cpu_addr,dut.bus_write,dut.bus_write_data,dut.machine_i.device_select[7],dut.machine_i.hdd.phi0,
            dut.machine_i.hdd.reg_command,dut.machine_i.hdd.reg_unit,dut.machine_i.hdd.sector,
            dut.machine_i.hdd.dma_state,dut.machine_i.hdd.dma_req_rd,dut.machine_i.hdd.hdd_mounted);
    task emit(input [7:0] b);begin rom[program_cursor]=b;program_cursor=program_cursor+1;end endtask
    task immediate(input [7:0] b);begin emit(8'ha9);emit(b);end endtask
    task sta(input [15:0] a);begin emit(8'h8d);emit(a[7:0]);emit(a[15:8]);end endtask
    task lda(input [15:0] a);begin emit(8'had);emit(a[7:0]);emit(a[15:8]);end endtask
    task build_program(input bit disk);
        begin
            for(integer i=0;i<131072;i=i+1)rom[i]=8'hea;
            rom[131068]=0;rom[131069]=8'he0;
            program_cursor=122880;
            emit(8'h78);immediate(8'h80);sta(16'hc036);
            emit(8'h58); // Exercise real accelerated operation with IRQ delay enabled.
            if(!disk)begin
                emit(8'h18);emit(8'hfb);emit(8'hc2);emit(8'h30);
                emit(8'ha9);emit(8'h34);emit(8'h12);
                emit(8'h8f);emit(0);emit(0);emit(2);emit(8'hdb);
            end else begin
                immediate(8'h3f);sta(16'hc035); // Disable shadow copies, retain bank-0 I/O.
                immediate(8'h80);sta(16'hc02d); // Slot 7 external I/O/ROM.
                if(disk_bypass)begin
                    immediate(8'h50);repeat(4)sta(16'hc05a);
                    immediate(8'hf7);sta(16'hc059); // Software disables both cache levels.
                end
                immediate(8'h70);sta(16'hc0f3); // Unit 0, slot 7.
                immediate(0);sta(16'hc0f4);sta(16'hc0f7);
                immediate(8'h40);sta(16'hc0f5);
                immediate(1);sta(16'hc0f6);sta(16'hc0f2);
                lda(16'hc0f0); // Read LBA 1 into $00:4000 (CPU is DMA-held).
                immediate(2);sta(16'hc0f2);lda(16'hc0f0); // Write same sector back.
                emit(8'hdb);
            end
        end
    endtask
    task packet(input [7:0] kind,input integer size,input integer speed);
        reg [7:0] value;
        begin
            load_epoch=~load_epoch;
            for(integer i=0;i<size;i=i+1)begin
                value=kind==8'h41 ? rom[i]:kind==8'h49 ? hdd_rom[i]:kind==8'h48 ? ((i*7+19)&255):
                    kind==8'h43 && i==1 ? speed:0;
                @(negedge core_clk);load_kind=kind;load_total=size;load_offset=i;load_data=value;
                load_first=i==0;load_last=i==size-1;load_valid=1;
                while(!load_ready)@(negedge core_clk);
                @(negedge core_clk);load_valid=0;
            end
            repeat(40)@(negedge core_clk);
            if(load_error)$fatal(1,"runtime load rejected kind=%x",kind);
        end
    endtask
    task start_machine(input bit disk,input integer speed);
        begin
            @(negedge core_clk);reset=1;core_reset_request=1;
            repeat(20)@(negedge core_clk);reset=0;
            build_program(disk);packet(8'h43,16,speed);packet(8'h41,131072,0);
            packet(8'h42,4096,0);packet(8'h46,4096,0);packet(8'h49,2304,0);
            if(!rom_loaded)$fatal(1,"synthetic ROMs did not become ready");
            if(memory[24'hf9fff8>>3][47:32]!==16'he000 || memory[24'hf9e000>>3][7:0]!==8'h78)
                $fatal(1,"runtime reset vector/program memory incorrect vector=%x program=%x",memory[24'hf9fff8>>3],memory[24'hf9e000>>3]);
            if(disk)begin
                media_slot=0;media_kind=8'h40;media_image_size=1024;media_generation=32'h5a;
                media_mount=1;@(negedge core_clk);media_mount=0;
                repeat(100)@(negedge core_clk);
            end
            core_reset_request=0;
        end
    endtask
    task wait_stopped;
        integer polls;
        begin
            polls=0;
            while(!dut.machine_i.cpu.STPExec && polls<2000000)begin @(negedge core_clk);polls=polls+1;end
            if(!dut.machine_i.cpu.STPExec || load_error)$fatal(1,"CPU did not stop: addr=%x pc=%x cya=%x phi2=%b stall=%b",
                dut.bus_addr,dut.machine_i.cpu_addr,dut.machine_i.CYAREG,dut.phi2,dut.cache_stall);
            // Writes are posted: compare DDR only after the cache drained.
            polls=0;while(memory_busy && polls<100000)begin @(negedge core_clk);polls=polls+1;end
            if(memory_busy)$fatal(1,"posted DDR writes did not drain");
        end
    endtask
    task wait_media(input bit writing);
        integer polls;
        begin
            polls=0;
            while(!media_request_valid && polls<2000000)begin @(negedge core_clk);polls=polls+1;end
            if(!media_request_valid || media_request_write!=writing || media_request_lba!=1 ||
                media_request_generation!=32'h5a || media_request_block_count!=2)
                $fatal(1,"HDD request mismatch write=%b lba=%d gen=%x blocks=%x cpu=%x",media_request_write,
                    media_request_lba,media_request_generation,media_request_block_count,dut.machine_i.cpu_addr);
        end
    endtask
    task complete(input [31:0] status);
        begin
            @(negedge core_clk);media_complete_request_id=media_request_id;
            media_complete_generation=media_request_generation;media_complete_status=status;
            media_complete_block_count=status==0 ? media_request_block_count:0;media_complete=1;
            @(negedge core_clk);media_complete=0;
        end
    endtask
    task receive_sector;
        integer polls;
        begin
            for(integer i=0;i<512;i=i+1)begin
                polls=0;
                while(!media_write_data_valid && polls<100000)begin @(negedge core_clk);polls=polls+1;end
                if(!media_write_data_valid || media_write_data!==((i*7+19)&255) || media_write_data_last!=(i==511))
                    $fatal(1,"HDD DMA write byte %0d =%x expected=%x last=%b RAM4000=%x RAM4008=%x sector0=%x sector1=%x",i,
                        media_write_data,((i*7+19)&255),media_write_data_last,memory[24'h004000>>3],memory[24'h004008>>3],
                        dut.machine_i.hdd.sector_ram.mem[0],dut.machine_i.hdd.sector_ram.mem[1]);
                media_write_data_ready=1;@(negedge core_clk);media_write_data_ready=0;
            end
        end
    endtask
    // Original, minimal double-sided WOZ2 data. The real production parser,
    // track RAM, save state machine and adapter ACK/retry path are exercised.
    reg [7:0] woz_image[0:2559];
    task woz_u16(input integer offset,input integer value);
        begin woz_image[offset]=value;woz_image[offset+1]=value>>8;end
    endtask
    task woz_u32(input integer offset,input integer value);
        begin woz_u16(offset,value);woz_u16(offset+2,value>>16);end
    endtask
    task load_woz_sector(input integer lba);
        begin
            load_epoch=~load_epoch;
            for(integer i=0;i<512;i=i+1)begin
                @(negedge core_clk);load_kind=8'h48;load_total=512;load_offset=i;
                load_data=woz_image[lba*512+i];load_first=i==0;load_last=i==511;load_valid=1;
                while(!load_ready)@(negedge core_clk);
                @(negedge core_clk);load_valid=0;
            end
            repeat(40)@(negedge core_clk);
        end
    endtask
    task mount_woz;
        integer polls,requests;
        begin
            @(negedge core_clk);media_slot=2;media_kind=8'h41;media_image_size=2560;
            media_generation=32'h6b;media_read_only=0;media_mount=1;
            @(negedge core_clk);media_mount=0;
            polls=0;requests=0;
            while(!(dut.woz_ready[0] && !dut.woz_busy[0]) && polls<1000000)begin
                @(negedge core_clk);polls=polls+1;
                if(media_request_valid)begin
                    if(media_request_write || media_request_generation!=32'h6b ||
                        media_request_block_count!=16'h0202 || media_request_lba>4)
                        $fatal(1,"WOZ fixture read mismatch");
                    load_woz_sector(media_request_lba);complete(0);requests=requests+1;
                    while(media_request_valid)@(negedge core_clk);
                end
            end
            if(!dut.woz_ready[0] || requests<5 || !dut.machine_i.DISK_READY[2])
                $fatal(1,"WOZ fixture did not mount and cache both sides");
        end
    endtask
    task native_eject(input bit writing);
        begin
            // Force only the native core output boundary. Adapter wiring and
            // phase1 capture must forward this full CLK_14M pulse correctly.
            @(negedge core_clk);
            force dut.machine_i.drive35_eject_req=1'b1;
            if(writing)begin
                force dut.machine_i.WOZ_TRACK3_BIT_WE=1'b1;
                force dut.machine_i.WOZ_TRACK3_BIT_WR_ADDR=16'd37;
                force dut.machine_i.WOZ_TRACK3_BIT_DATA_IN=8'ha5;
            end
            repeat(4)@(negedge core_clk);
            release dut.machine_i.drive35_eject_req;
            if(writing)begin
                release dut.machine_i.WOZ_TRACK3_BIT_WE;
                release dut.machine_i.WOZ_TRACK3_BIT_WR_ADDR;
                release dut.machine_i.WOZ_TRACK3_BIT_DATA_IN;
            end
        end
    endtask
    task wait_woz_write;
        integer polls;
        begin
            polls=0;
            while(!media_request_valid && polls<100000)begin @(negedge core_clk);polls=polls+1;end
            if(!media_request_valid || !media_request_write || media_request_lba!=3 ||
                media_request_generation!=32'h6b || media_request_block_count!=16'h0202)
                $fatal(1,"native eject did not request the dirty WOZ block");
        end
    endtask
    task receive_woz_sector;
        integer polls;
        reg [7:0] expected;
        begin
            for(integer i=0;i<512;i=i+1)begin
                polls=0;
                while(!media_write_data_valid && polls<100000)begin @(negedge core_clk);polls=polls+1;end
                expected=i==37 ? 8'ha5:woz_image[1536+i];
                if(!media_write_data_valid || media_write_data!==expected || media_write_data_last!=(i==511))
                    $fatal(1,"native eject lost track byte %0d got=%x expected=%x",i,media_write_data,expected);
                media_write_data_ready=1;@(negedge core_clk);media_write_data_ready=0;
            end
        end
    endtask
    task wait_native_empty;
        integer polls;
        begin
            polls=0;
            while(dut.mounted[2] && polls<10000)begin @(negedge core_clk);polls=polls+1;end
            repeat(20)@(negedge core_clk);
            if(dut.mounted[2] || dut.woz_mounted[0] || dut.machine_i.DISK_READY[2] ||
                dut.eject_pending[2] || dut.woz_dirty[0])$fatal(1,"native eject did not clear media present");
        end
    endtask
`include "tb_apple_iigs_native_woz_cases.sv"
`include "tb_apple_iigs_smartport_cases.sv"
    initial begin
        if(!$value$plusargs("HDD_ROM=%s",rom_path))$fatal(1,"licensed HDD ROM fixture required");
        $readmemh(rom_path,hdd_rom);
        for(integer speed=0;speed<7;speed=speed+1)begin
            memory[24'h020000>>3]=0;
            start_machine(0,speed);wait_stopped();
            if(dut.machine_i.accel_cfg_speed!=speed)$fatal(1,"requested accelerator setting did not apply");
            if(memory[24'h020000>>3][15:0]!==16'h1234)$fatal(1,"native 65816 write failed speed=%0d got=%x",speed,memory[24'h020000>>3]);
            $display("PASS: complete machine native 65816 speed=%0d cache hits=%0d misses=%0d",speed,dut.cache_i.hits,dut.cache_i.misses);
        end
        for(integer bypass_mode=0;bypass_mode<2;bypass_mode++)begin
        disk_bypass=bypass_mode!=0;
        start_machine(1,6);wait_media(0);packet(8'h48,512,0);complete(0);
        wait_media(1);receive_sector();
        complete(32'h27);wait_stopped();
        if(dut.sd_ack[0] || media_request_valid || dut.machine_i.hdd.reg_status!=8'h27)
            $fatal(1,"failed HDD write did not release ACK/CPU with explicit I/O error");
        for(integer i=0;i<512;i=i+1)if(memory[(16'h4000+i)>>3][((16'h4000+i)&7)*8+:8]!==((i*7+19)&255))
            $fatal(1,"HDD read DMA memory mismatch at byte %0d",i);
        $display("PASS: complete HDD read/write DMA, every sector byte, failed-write completion and native RAM coherence");
        end
        smartport_machine_checks(0,0);smartport_machine_checks(6,0);smartport_machine_checks(6,1);
        smartport_reset_checks();
        smartport_rom_call_checks(0);smartport_rom_call_checks(6);
        smartport_metadata_checks();
        for(integer i=0;i<2560;i=i+1)woz_image[i]=0;
        woz_u32(0,32'h325a4f57);woz_u32(4,32'h0a0d0aff);
        woz_u32(12,32'h4f464e49);woz_u32(16,60);woz_image[20]=2;woz_image[21]=2;
        woz_u32(80,32'h50414d54);woz_u32(84,160);
        for(integer i=0;i<160;i=i+1)woz_image[88+i]=8'hff;
        woz_image[88]=0;woz_image[89]=1;
        woz_u32(248,32'h534b5254);woz_u32(252,2304);
        woz_u16(256,3);woz_u16(258,1);woz_u32(260,4096);
        woz_u16(264,4);woz_u16(266,1);woz_u32(268,4096);
        for(integer i=0;i<1024;i=i+1)woz_image[1536+i]=(i*7+19)&255;
        mount_woz();native_woz_read_checks();native_eject(0);wait_native_empty();
        mount_woz();native_woz_write_checks();native_eject(1);wait_woz_write();receive_woz_sector();
        complete(4);repeat(100)@(negedge core_clk);
        if(!dut.mounted[2] || !dut.woz_dirty[0] || !dut.eject_pending[2] ||
            !dut.sd_ack[2] || !dut.machine_i.DISK_READY[2] || !media_state_dirty)
            $fatal(1,"failed native eject write discarded the mounted dirty image");
        wait_woz_write();receive_woz_sector();complete(0);wait_native_empty();
        $display("PASS: native 3.5 eject, simultaneous final track write, exact dirty-block retry, ACK retention and media-present removal");
        $finish;
    end
    initial begin #1000000000;$fatal(1,"complete adapter regression timeout");end
endmodule
