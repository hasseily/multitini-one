// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Original, generated 6502 program. No Nintendo ROM or game data is present.
// Exercise the real pinned T65+PPU/APU through complete iNES loading, delayed
// PSRAM service, a 256x240 raster, controller bus reads and master pause.
module tb_nes_machine;
    reg core_clk=0,reset=1,core_reset_request=1,pause_request=0,clear_load_error=0;
    reg [15:0] input_code=0,input_value=0;reg input_valid=0;wire input_ready;
    reg load_abort=0,load_abort_epoch=0,load_epoch=0;
    reg [7:0] load_data=0,load_kind=8'h50;reg load_valid=0,load_first=0,load_last=0;
    reg [31:0] load_offset=0,load_total=24592;wire load_ready;
    wire [23:0] memory_addr;wire memory_write;wire [63:0] memory_wdata;wire [7:0] memory_wstrb;
    wire memory_valid;reg memory_ready=1;reg [63:0] memory_rdata=0;reg memory_rvalid=0;reg memory_fault=0;
    wire [31:0] core_id;wire ready,load_error,cart_loaded;wire [31:0] cart_size;
    wire [7:0] video_r,video_g,video_b,video_x,video_y;wire video_de,video_hsync,video_vsync,video_valid;
    wire signed [23:0] audio_l,audio_r;wire audio_valid;
    wire media_write_data_valid,media_write_data_last,media_state_mounted,media_state_dirty,media_state_eject_pending;
    wire media_request_valid,media_request_write;
    wire [31:0] media_request_id,media_request_lba,media_request_generation;
    wire [15:0] media_request_block_count;
    wire [7:0] media_write_data;
    reg media_mount=0,media_eject=0,media_complete=0;
    reg [31:0] media_generation=1;
    multitini_nes_adapter dut(
        .media_slot(8'd0),.media_kind(8'h51),.media_read_only(1'b0),.media_image_size(32'd8192),
        .media_complete_request_id(32'd0),.media_complete_status(32'd0),.media_complete_block_count(16'd0),
        .media_complete_generation(32'd0),.media_write_data_ready(1'b1),.*
    );
    always #11.64 core_clk=!core_clk;
    reg [7:0] prg[0:16383],chr[0:8191],header[0:15];
    integer pending=0,service_cycles=0,master_ticks=0,stalled=0,rom_reads=0,peak_debt=0;
    reg [23:0] pending_addr;
    reg [63:0] pending_data;
    reg pending_write;
    reg [7:0] pending_strobe;
    integer i,j,pc=0;
    task emit(input [7:0] b);begin prg[pc]=b;pc=pc+1;end endtask
    task lda(input [7:0] b);begin emit(8'ha9);emit(b);end endtask
    task sta(input [15:0] a);begin emit(8'h8d);emit(a[7:0]);emit(a[15:8]);end endtask
    always @(posedge core_clk)begin
        service_cycles<=service_cycles+1;memory_rvalid<=0;
        if(!dut.machine_reset)begin
            if(dut.tick_debt>peak_debt)peak_debt<=dut.tick_debt;
            if(dut.machine_ce)master_ticks<=master_ticks+1;
            if(dut.master_phase==0&&!dut.machine_ce&&!pause_request)stalled<=stalled+1;
        end
        if(memory_valid&&memory_ready)begin
            if(pending!=0)$fatal(1,"overlapping memory request");
            pending<=core_reset_request?2:80;
            pending_addr<=memory_addr;pending_data<=memory_wdata;pending_strobe<=memory_wstrb;pending_write<=memory_write;
            if(!memory_write)rom_reads<=rom_reads+1;
        end
        if(pending>0)begin
            pending<=pending-1;
            if(pending==1)begin
                for(j=0;j<8;j=j+1)begin
                    if(pending_addr<16384)begin
                        if(pending_write&&pending_strobe[j])prg[pending_addr+j]<=pending_data[j*8+:8];
                        memory_rdata[j*8+:8]<=prg[pending_addr+j];
                    end else if(pending_addr>=24'h200000&&pending_addr<24'h202000)begin
                        if(pending_write&&pending_strobe[j])chr[pending_addr-24'h200000+j]<=pending_data[j*8+:8];
                        memory_rdata[j*8+:8]<=chr[pending_addr-24'h200000+j];
                    end else $fatal(1,"unexpected external address %h",pending_addr);
                end
                memory_rvalid<=1;
            end
        end
    end
    task stream_byte(input integer offset,input [7:0] value);
        begin
            @(negedge core_clk);load_offset=offset;load_data=value;load_first=offset==0;
            load_last=offset+1==load_total;load_valid=1;
            do @(posedge core_clk);while(!load_ready);
            @(negedge core_clk);load_valid=0;
        end
    endtask
    integer frame_pixels=0,frames=0,last_frame_service=0,last_frame_ticks=0,frame_start_cycles=0;
    integer frame_start_reads=0,last_frame_reads=0,blank_recoveries=0;
    reg blank_recovered=0,monitor_frames=1;
    reg [239:0] rows=0;
    reg [255:0] columns=0;
    integer colored=0;
    always @(negedge core_clk)if(video_valid&&video_de&&monitor_frames)begin
        if(video_x==0&&video_y==0)begin
            if(frames!=0&&frame_pixels!=61440)$fatal(1,"incomplete native frame %0d",frame_pixels);
            if(frames!=0)begin
                last_frame_service=service_cycles-frame_start_cycles;
                last_frame_ticks=master_ticks-last_frame_ticks;
                last_frame_reads=rom_reads-frame_start_reads;
                $display("NES FRAME %0d service=%0d native_master=%0d ROMmisses=%0d",frames,last_frame_service,last_frame_ticks,last_frame_reads);
            end
            frames=frames+1;frame_pixels=0;frame_start_cycles=service_cycles;last_frame_ticks=master_ticks;frame_start_reads=rom_reads;
            if(blank_recovered)blank_recoveries=blank_recoveries+1;
            blank_recovered=0;
        end
        frame_pixels=frame_pixels+1;rows[video_y]=1;columns[video_x]=1;
        if({video_r,video_g,video_b}==24'h78abff)colored=colored+1;
    end
    always @(negedge core_clk)if(!dut.machine_reset&&dut.scanline>=242&&dut.scanline<260&&dut.tick_debt==0)
        blank_recovered=1;
    reg [8:0] held_cycle,held_scanline;
    reg [24:0] held_cpu_addr;
    integer before_ticks,before_services,before_debt,before_residue,elapsed,budget,drift,accounted;
    initial begin
        for(i=0;i<16;i=i+1)header[i]=0;
        header[0]=8'h4e;header[1]=8'h45;header[2]=8'h53;header[3]=8'h1a;header[4]=1;header[5]=1;header[6]=2;
        for(i=0;i<16384;i=i+1)prg[i]=8'hea;
        for(i=0;i<8192;i=i+1)chr[i]=i[3]?8'h00:8'hff;
        emit(8'h78);emit(8'hd8); // SEI; CLD
        emit(8'ha2);emit(8'hff);emit(8'h9a); // LDX#$ff; TXS
        lda(8'h5a);sta(16'h0002); // prove real CPU reset-vector execution
        lda(0);sta(16'h2000);sta(16'h2001);
        // Wait two PPU vblanks, as a real cold NES must.
        emit(8'h2c);emit(2);emit(8'h20);emit(8'h10);emit(8'hfb);
        emit(8'h2c);emit(2);emit(8'h20);emit(8'h10);emit(8'hfb);
        lda(8'h3f);sta(16'h2006);lda(0);sta(16'h2006);
        for(i=0;i<32;i=i+1)begin lda((i%4)==1?8'h21:8'h0f);sta(16'h2007);end
        // Fill every nametable/attribute byte with changing tile IDs. This
        // fetches the fullpattern table and exercises recurringPSRAM misses.
        lda(8'h20);sta(16'h2006);lda(0);sta(16'h2006);
        emit(8'ha0);emit(4);emit(8'ha2);emit(0); // LDY4;LDX0
        i=pc;emit(8'h8a);sta(16'h2007);emit(8'he8);emit(8'hd0);emit((i-(pc+1))&255);
        emit(8'h88);emit(8'hd0);emit((i-(pc+1))&255);
        lda(0);sta(16'h2005);sta(16'h2005);lda(8'h0a);sta(16'h2001);
        // Strobe both controllers, consume all eight bits into CPU RAM.
        lda(1);sta(16'h4016);
        emit(8'had);emit(8'h16);emit(8'h40);sta(16'h0030);
        emit(8'had);emit(8'h16);emit(8'h40);sta(16'h0031);
        emit(8'had);emit(8'h17);emit(8'h40);sta(16'h0032);
        lda(0);sta(16'h4016);
        emit(8'ha2);emit(0);
        i=pc;
        emit(8'had);emit(8'h16);emit(8'h40);emit(8'h95);emit(8'h10);
        emit(8'had);emit(8'h17);emit(8'h40);emit(8'h95);emit(8'h20);
        emit(8'he8);emit(8'he0);emit(8'h08);emit(8'hd0);emit((i-(pc+1))&255);
        lda(8'ha5);sta(16'h0003);
        i=pc;emit(8'h4c);emit(i&255);emit(8'hc0+(i>>8)); // stable JMP forever
        prg[16378]=0;prg[16379]=8'hc0;prg[16380]=0;prg[16381]=8'hc0;prg[16382]=0;prg[16383]=8'hc0;
        repeat(5)@(negedge core_clk);reset=0;
        for(i=0;i<24592;i=i+1)
            stream_byte(i,i<16?header[i]:i<16400?prg[i-16]:chr[i-16400]);
        wait(cart_loaded);@(negedge core_clk);
        if(load_error||cart_size!=24592)$fatal(1,"cart loader failed");
        media_mount=1;@(negedge core_clk);media_mount=0;
        if(!media_state_mounted)$fatal(1,"battery cart mount failed");
        input_code=16'h5000;input_value=16'h0095;input_valid=1;@(negedge core_clk);
        input_code=16'h5001;input_value=16'h006a;@(negedge core_clk);input_valid=0;
        core_reset_request=0;
        wait(dut.memory_service.cpu_ram[2]===8'h5a);
        wait(frames>=1);@(negedge core_clk);pause_request=1;
        repeat(20)@(negedge core_clk);
        held_cycle=dut.cycle;held_scanline=dut.scanline;held_cpu_addr=dut.cpu_addr;
        repeat(200)@(negedge core_clk);
        if(dut.cycle!==held_cycle||dut.scanline!==held_scanline||dut.cpu_addr!==held_cpu_addr)
            $fatal(1,"machine state advanced during pause");
        pause_request=0;
        wait(dut.memory_service.cpu_ram[3]===8'ha5);
        if({dut.memory_service.cpu_ram[23][0],dut.memory_service.cpu_ram[22][0],dut.memory_service.cpu_ram[21][0],dut.memory_service.cpu_ram[20][0],
            dut.memory_service.cpu_ram[19][0],dut.memory_service.cpu_ram[18][0],dut.memory_service.cpu_ram[17][0],dut.memory_service.cpu_ram[16][0]}!==8'ha9)
            $fatal(1,"controller1 serial order");
        if({dut.memory_service.cpu_ram[39][0],dut.memory_service.cpu_ram[38][0],dut.memory_service.cpu_ram[37][0],dut.memory_service.cpu_ram[36][0],
            dut.memory_service.cpu_ram[35][0],dut.memory_service.cpu_ram[34][0],dut.memory_service.cpu_ram[33][0],dut.memory_service.cpu_ram[32][0]}!==8'h56)
            $fatal(1,"controller2 serial order");
        if(!dut.memory_service.cpu_ram[48][0]||!dut.memory_service.cpu_ram[49][0]||dut.memory_service.cpu_ram[50][0])
            $fatal(1,"held-high strobe must repeatedly return each live A button");
        wait(frames>=5);
        if(rows!={240{1'b1}}||columns!={256{1'b1}}||colored<61440)$fatal(1,"native full raster/color missing colorpixels=%0d",colored);
        before_ticks=master_ticks;before_services=service_cycles;before_debt=dut.tick_debt;
        before_residue=(dut.master_phase+1)%2;
        wait(frames>=13);
        elapsed=service_cycles-before_services;budget=2*(master_ticks-before_ticks);drift=elapsed-budget;
        accounted=drift-2*($signed({1'b0,dut.tick_debt})-before_debt)-(((dut.master_phase+1)%2)-before_residue);
        if(accounted!=0)$fatal(1,"native tick/debt/phase invariant %0d",accounted);
        if(drift>budget/1000||drift<-(budget/1000)||peak_debt>=4096||dut.timing_fault||blank_recoveries<8)
            $fatal(1,"native cadence debt=%0d peak=%0d drift=%0d budget=%0d recovered=%0d",dut.tick_debt,peak_debt,drift,budget,blank_recoveries);
        monitor_frames=0;core_reset_request=1;@(negedge core_clk);if(ready)$fatal(1,"reset did not drop ready for capture drain");
        media_generation=2;media_eject=1;@(negedge core_clk);media_eject=0;
        wait(!media_state_mounted);@(negedge core_clk);
        if(!dut.save_pause)$fatal(1,"clean eject lost its barrier");
        // A non-battery cartridge has no new media mount to release the old
        // eject barrier. Starting the new clean cartridge must do so itself.
        header[6]=0;load_epoch=1;
        for(i=0;i<24592;i=i+1)
            stream_byte(i,i<16?header[i]:i<16400?prg[i-16]:chr[i-16400]);
        wait(cart_loaded);@(negedge core_clk);core_reset_request=0;
        wait(dut.memory_service.cpu_ram[2]===8'h5a);
        if(dut.save_pause||!ready||media_state_mounted)$fatal(1,"battery to nonbattery switch retained the old save barrier");
        // Conversion boundary proof: a full unsigned mixer excursion must
        // remain a correctly signed24-bit PCM transient on both channels.
        force dut.raw_sample=16'd0;
        @(negedge core_clk);while(audio_valid)@(negedge core_clk);
        do @(negedge core_clk);while(!audio_valid);
        if(audio_l>=0||audio_l!==audio_r)$fatal(1,"negative APU peak wrapped in signed24-bit conversion");
        force dut.raw_sample=16'hffff;
        @(negedge core_clk);while(audio_valid)@(negedge core_clk);
        do @(negedge core_clk);while(!audio_valid);
        if(audio_l<=0||audio_l!==audio_r)$fatal(1,"positive APU peak wrapped in signed24-bit conversion");
        release dut.raw_sample;
        $display("PASS NES machine: real T65 boot, PPU256x240/color, two8-bit pads and strobe, pause, signed audio peaks, battery-to-nonbattery switch,80cyclePSRAM,8fullpatternframes; drift=%0d/%0dservices misses=%0d peakdebt=%0d vblankrecoveries=%0d",drift,budget,rom_reads,peak_debt,blank_recoveries);$finish;
    end
    initial begin #300000000;$fatal(1,"real machine timeout PCaddr=%h CPUmem=%h raster=%d,%d err=%b loaded=%b",dut.cpu_addr,dut.cpu_rdata,dut.cycle,dut.scanline,load_error,cart_loaded);end
endmodule
