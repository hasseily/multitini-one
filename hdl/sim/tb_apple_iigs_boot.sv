// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O);assign O=I;endmodule
// Fixture-driven actual ROM3 boot progress regression. All ROM/media bytes
// are supplied privately at runtime; no machine assets are embedded here.
module tb_apple_iigs_boot;
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
    reg [7:0] rom3[0:262143],character[0:4095],adb3[0:4095],hdd_rom[0:255];
    reg [7:0] disk_image[0:33554431];
    string fixture_path,disk_path;
    integer speed=0,compatibility_flags=16,max_ticks=20000000,media_bytes=0,media_offset=0;
    integer floppy=0,expected_boot_opcode=-1;
    integer ticks=0,accepted=0,ops=0,ring_pos=0,request_count=0;
    reg running=0,have_disk=0;
    reg [23:0] pc_ring[0:63];
    reg [7:0] op_ring[0:63];
    integer io_reads[0:255],io_writes[0:255];
    reg [23:0] last_io=0;
    reg [7:0] last_io_data=0;
    reg last_io_write=0;
    integer prtc_traces=0;
    integer trace_start=0,trace_end=0,rtc_control_writes=0,media_completions=0,ram_ops=0;
    integer min_reads=2,brk_repeats=0,require_irq_dispatch=0;
    reg [23:0] last_brk=0;
    reg bootblock_seen=0,irq_dispatch_seen=0;
    task packet(input [7:0] kind,input integer size,input integer lba);
        reg [7:0] value;
        begin
            load_epoch=~load_epoch;
            for(integer i=0;i<size;i=i+1)begin
                case(kind)
                    8'h40:value=rom3[i];8'h42:value=character[i];8'h44:value=adb3[i];8'h45:value=hdd_rom[i];
                    8'h43:value=i==0 ? 1:i==1 ? speed:i==3 ? compatibility_flags:i==4 ? 8'h00:i==5 ? 8'h00:i==6 ? 8'hd0:i==7 ? 8'h6a:0;
                    8'h48:value=disk_image[media_offset+lba*512+i];
                    default:value=0;
                endcase
                @(negedge core_clk);load_kind=kind;load_total=size;load_offset=i;load_data=value;
                load_first=i==0;load_last=i==size-1;load_valid=1;
                while(!load_ready)@(negedge core_clk);
                @(negedge core_clk);load_valid=0;
            end
            repeat(40)@(negedge core_clk);
            if(load_error)$fatal(1,"Actual ROM loader rejected kind=%x",kind);
        end
    endtask
    task summary;
        begin
            $display("BOOT_SUMMARY ticks=%0d accepted=%0d ops=%0d pc=%02x:%04x ir=%02x state=%x A=%04x X=%04x Y=%04x P=%03x bus=%06x physical=%06x in=%02x reset=%b nativepending=%b returnseen=%b returntoggle=%b stall=%b dma=%b requests=%0d",
                ticks,accepted,ops,dut.machine_i.cpu.PBR,dut.machine_i.cpu.PC,dut.machine_i.cpu.IR,dut.machine_i.cpu.STATE,
                dut.machine_i.cpu.A,dut.machine_i.cpu.X,dut.machine_i.cpu.Y,dut.machine_i.cpu.P,
                dut.bus_addr,dut.physical_addr,dut.cpu_read_data,dut.machine_reset,dut.native_pending,dut.native_return_seen,
                dut.native_return_toggle,dut.cache_stall,dut.hdd_dma,request_count);
            $display("BOOT_PRTC addr=%b strobe=%b rw=%b state=%d pending=%b c033=%02x c034=%02x clock=%08x ADBstate=%d ADBcmd=%02x ADBdata=%08x VBL=%b C019=%02x lastio=%06x write=%b data=%02x",
                dut.machine_i.prtc.addr,dut.machine_i.prtc.strobe,dut.machine_i.prtc.rw,dut.machine_i.prtc.state,
                dut.machine_i.prtc.read_pending,dut.machine_i.prtc.c033,dut.machine_i.prtc.c034,dut.machine_i.prtc.clock_data,
                dut.machine_i.adb.state,dut.machine_i.adb.cmd,dut.machine_i.adb.data,dut.machine_i.mega2_vbl,
                dut.machine_i.io_dout,last_io,last_io_write,last_io_data);
        end
    endtask
    always @(posedge core_clk)if(running)begin
        ticks<=ticks+1;
        if(dut.cpu_ce)begin
            accepted<=accepted+1;
            if(dut.machine_i.cpu.STATE==0)begin
                pc_ring[ring_pos]<=dut.machine_i.cpu_addr;
                op_ring[ring_pos]<=dut.cpu_read_data;ring_pos<=(ring_pos+1)&63;ops<=ops+1;
                if(media_completions!=0 && dut.machine_i.cpu_addr<24'hfc0000)ram_ops<=ram_ops+1;
                if(media_completions!=0 && dut.machine_i.cpu_addr==24'h000801 && !bootblock_seen)begin
                    if(dut.cpu_read_data!=(expected_boot_opcode>=0 ? expected_boot_opcode[7:0]:disk_image[media_offset+1]))
                        $fatal(1,"Bootblock opcode differs from exact disk byte");
                    bootblock_seen<=1;$display("BOOT_MILESTONE exact_disk_bootblock_opcode ticks=%0d pc=%06x opcode=%02x",ticks,dut.machine_i.cpu_addr,dut.cpu_read_data);
                end
                if(media_completions!=0 && dut.machine_i.cpu_addr==24'he10010 && !irq_dispatch_seen)begin
                    irq_dispatch_seen<=1;
                    $display("BOOT_MILESTONE native_irq_dispatch_target ticks=%0d pc=%06x",ticks,dut.machine_i.cpu_addr);
                end
                if(media_completions!=0 && dut.cpu_read_data==0)begin
                    if(last_brk==dut.machine_i.cpu_addr)brk_repeats<=brk_repeats+1;
                    else begin last_brk<=dut.machine_i.cpu_addr;brk_repeats<=1;end
                    if(brk_repeats==128)$display("BOOT_FAULT repeated_BRK ticks=%0d pc=%06x repeats=%0d",ticks,last_brk,brk_repeats);
                end
                if(trace_end>trace_start && ticks>=trace_start && ticks<trace_end)
                    $display("OP_TRACE ticks=%0d pc=%06x op=%02x A=%04x X=%04x Y=%04x D=%04x SP=%04x DBR=%02x P=%03x IRQ=%b",ticks,dut.machine_i.cpu_addr,dut.cpu_read_data,dut.machine_i.cpu.A,dut.machine_i.cpu.X,dut.machine_i.cpu.Y,dut.machine_i.cpu.D,dut.machine_i.cpu.SP,dut.machine_i.cpu.DBR,dut.machine_i.cpu.P,dut.machine_i.cpu.IRQ_ACTIVE);
            end
        end
        if(ticks!=0 && ticks%1000000==0)summary();
    end
    always @(posedge dut.clk14)if(running && dut.native_commit_q && !dut.machine_reset && dut.machine_i.IO)begin
        last_io<=dut.bus_addr;last_io_write<=dut.bus_write;last_io_data<=dut.bus_write ? dut.bus_write_data:dut.machine_i.cpu_din;
        if(dut.bus_write)io_writes[dut.bus_addr[7:0]]<=io_writes[dut.bus_addr[7:0]]+1;
        else io_reads[dut.bus_addr[7:0]]<=io_reads[dut.bus_addr[7:0]]+1;
        if(dut.bus_addr[7:0]==8'h34 && dut.bus_write && dut.machine_i.native_prtc_sel)rtc_control_writes<=rtc_control_writes+1;
        if(trace_end>trace_start && ticks>=trace_start && ticks<trace_end && dut.bus_write)
            $display("IO_WRITE ticks=%0d pc=%02x:%04x addr=%06x data=%02x",ticks,dut.machine_i.cpu.PBR,dut.machine_i.cpu.PC,dut.bus_addr,dut.bus_write_data);
        if(dut.bus_addr[15:0]>=16'hc032 && dut.bus_addr[15:0]<=16'hc034 && prtc_traces<80)begin
            prtc_traces<=prtc_traces+1;
            $display("PRTC_BUS ticks=%0d PC=%02x:%04x addr=%06x write=%b data=%02x select=%b strobe=%b rw=%b state=%d c033=%02x c034=%02x",
                ticks,dut.machine_i.cpu.PBR,dut.machine_i.cpu.PC,dut.bus_addr,dut.bus_write,
                dut.bus_write ? dut.bus_write_data:dut.machine_i.cpu_din,dut.machine_i.native_prtc_sel,
                dut.machine_i.prtc.strobe,dut.machine_i.prtc.rw,dut.machine_i.prtc.state,
                dut.machine_i.prtc.c033,dut.machine_i.prtc.c034);
        end
    end
    initial begin : serve_media
        wait(running);
        forever begin
            @(negedge core_clk);
            if(media_request_valid)begin
                request_count=request_count+1;
                $display("MEDIA_REQUEST count=%0d lba=%0d write=%b gen=%x blocks=%x",request_count,media_request_lba,
                    media_request_write,media_request_generation,media_request_block_count);
                if(have_disk && !media_request_write && media_request_lba<(media_bytes-media_offset+511)/512)
                    packet(8'h48,512,media_request_lba);
                @(negedge core_clk);media_complete_request_id=media_request_id;
                media_complete_generation=media_request_generation;
                media_complete_status=have_disk && !media_request_write && media_request_lba<(media_bytes-media_offset+511)/512 ? 0:3;
                media_complete_block_count=media_complete_status==0 ? media_request_block_count:0;media_complete=1;
                @(negedge core_clk);media_complete=0;
                if(media_complete_status==0)media_completions=media_completions+1;
                while(media_request_valid)@(negedge core_clk);
            end
        end
    end
    initial begin : boot
        integer fd,n;
        if(!$value$plusargs("FIXTURES=%s",fixture_path))$fatal(1,"Missing private fixture directory");
        if($value$plusargs("SPEED=%d",speed))begin end
        if($value$plusargs("COMPAT_FLAGS=%d",compatibility_flags))begin end
        if(compatibility_flags<0 || compatibility_flags>31)$fatal(1,"Invalid compatibility flags");
        if($value$plusargs("TICKS=%d",max_ticks))begin end
        if($value$plusargs("TRACE_START=%d",trace_start))begin end
        if($value$plusargs("TRACE_END=%d",trace_end))begin end
        if($value$plusargs("MIN_READS=%d",min_reads))begin end
        if($value$plusargs("REQUIRE_IRQ=%d",require_irq_dispatch))begin end
        if($value$plusargs("FLOPPY=%d",floppy))begin end
        if($value$plusargs("BOOT_OPCODE=%d",expected_boot_opcode))begin end
        $readmemh({fixture_path,"/rom3.hex"},rom3);$readmemh({fixture_path,"/char.hex"},character);
        $readmemh({fixture_path,"/adb3.hex"},adb3);$readmemh({fixture_path,"/hdd.hex"},hdd_rom);
        if($value$plusargs("DISK=%s",disk_path))begin
            fd=$fopen(disk_path,"rb");if(!fd)$fatal(1,"Cannot open exact private media");
            n=$fread(disk_image,fd);$fclose(fd);media_bytes=n;have_disk=1;
            if($value$plusargs("OFFSET=%d",media_offset))begin end
            $display("PRIVATE_MEDIA bytes=%0d offset=%0d",media_bytes,media_offset);
        end
        for(integer i=0;i<256;i=i+1)begin io_reads[i]=0;io_writes[i]=0;end
        repeat(20)@(negedge core_clk);reset=0;
        packet(8'h43,16,0);packet(8'h40,262144,0);packet(8'h42,4096,0);packet(8'h44,4096,0);packet(8'h45,256,0);
        if(!rom_loaded || load_error)$fatal(1,"Actual ROM3 set not ready");
        $display("ACTUAL_ROM3_LOADED reset_vector=%04x first_opcode=%02x speed=%0d compatibility_flags=%02x",memory[24'hfffff8>>3][47:32],memory[24'hfc0000>>3][7:0],speed,compatibility_flags);
        media_slot=4;media_kind=8'h42;media_image_size=1024;media_generation=1;media_mount=1;
        @(negedge core_clk);media_mount=0;
        if(have_disk)begin
            repeat(40)@(negedge core_clk);media_slot=floppy!=0 ? 2:0;media_kind=floppy!=0 ? 8'h41:8'h40;
            media_image_size=floppy!=0 ? media_bytes:((media_bytes-media_offset)/512)*512;
            media_generation=2;media_read_only=1;media_mount=1;
            @(negedge core_clk);media_mount=0;
        end
        repeat(40)@(negedge core_clk);core_reset_request=0;running=1;
        repeat(max_ticks)@(negedge core_clk);
        summary();
        $display("BOOT_RESULT rtc_control_writes=%0d media_requests=%0d media_completions=%0d bootblock_seen=%b ram_ops=%0d last_brk=%06x brk_repeats=%0d",rtc_control_writes,request_count,media_completions,bootblock_seen,ram_ops,last_brk,brk_repeats);
        for(integer i=0;i<64;i=i+1)$display("OP_RING %0d pc=%06x data=%02x",i,pc_ring[(ring_pos+i)&63],op_ring[(ring_pos+i)&63]);
        for(integer i=0;i<256;i=i+1)if(io_reads[i] || io_writes[i])$display("IO_COUNTS C0%02x read=%0d write=%0d",i,io_reads[i],io_writes[i]);
        if(rtc_control_writes==0)$fatal(1,"RTC C034 control writes never reached the clock");
        if(!have_disk || media_completions<min_reads || !bootblock_seen || ram_ops==0)
            $fatal(1,"Actual disk boot milestones not reached within primary tick budget");
        if(brk_repeats>128)$fatal(1,"Actual disk boot entered a repeated BRK fault loop");
        if(require_irq_dispatch!=0 && !irq_dispatch_seen)$fatal(1,"Native IRQ did not reach E1:0010");
        if(floppy!=0 && io_reads[8'he0]+io_reads[8'he1]+io_reads[8'hec]+io_reads[8'hed]==0)
            $fatal(1,"Native IWM was not used to boot the floppy fixture");
        $display("PASS: actual ROM3 RTC and disk bootstrap milestones; no repeated BRK loop");$finish;
    end
endmodule
