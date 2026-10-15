// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Optional, generic private-file smoke test. No cartridge bytes belong here.
module nes_cartridge_smoke;
    reg core_clk=0,reset=1,core_reset_request=1,pause_request=0,clear_load_error=0;
    reg [15:0] input_code=0,input_value=0;reg input_valid=0;wire input_ready;
    reg load_abort=0,load_abort_epoch=0,load_epoch=0;
    reg [7:0] load_data=0,load_kind=8'h50;reg load_valid=0,load_first=0,load_last=0;
    reg [31:0] load_offset=0,load_total;wire load_ready;
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

    reg [7:0] cartridge[0:786447];
    string cartridge_path;
    integer file_handle,loaded_bytes,prg_bytes,chr_bytes,i,j,index;
    integer target_frames=30,latency=80,pending=0,service_cycles=0,master_ticks=0;
    integer cpu_fetches=0,rom_reads=0,peak_debt=0,frames=0,frame_pixels=0;
    integer nonuniform_frames=0,nonuniform_pixels=0,early_unknown_pixels=0;
    integer last_service=0,last_ticks=0,last_reads=0,result_file,picture_file;
    reg [23:0] pending_addr,pending_rgb;
    reg pending_write;
    reg [63:0] pending_data;
    reg [7:0] pending_strobe;
    reg [63:0] colors_seen=0;
    reg [23:0] first_rgb;
    reg frame_nonuniform=0;

    function automatic integer source_index(input [23:0] address);
        if(address<prg_bytes) source_index=16+address;
        else if(address>=24'h200000&&address<24'h200000+chr_bytes)
            source_index=16+prg_bytes+address-24'h200000;
        else source_index=-1;
    endfunction

    always @(posedge core_clk)begin
        service_cycles<=service_cycles+1;memory_rvalid<=0;
        if(!dut.machine_reset)begin
            if(load_error)$fatal(1,"NES load or timing fault during private cartridge run");
            if(dut.tick_debt>peak_debt)peak_debt<=dut.tick_debt;
            if(dut.machine_ce)begin
                master_ticks<=master_ticks+1;
                if(frames>=4&&$isunknown({dut.cpu_addr,dut.cycle,dut.scanline}))
                    $fatal(1,"unknown CPU bus or PPU counter after startup");
                if(dut.machine.cpu_ce&&dut.machine.cpu_Instrnew)cpu_fetches<=cpu_fetches+1;
            end
        end
        if(memory_valid&&memory_ready)begin
            if(pending!=0)$fatal(1,"overlapping private cartridge memory request");
            pending<=core_reset_request?2:latency;
            pending_addr<=memory_addr;pending_data<=memory_wdata;
            pending_strobe<=memory_wstrb;pending_write<=memory_write;
            if(!memory_write)rom_reads<=rom_reads+1;
        end
        if(pending>0)begin
            pending<=pending-1;
            if(pending==1)begin
                for(j=0;j<8;j=j+1)begin
                    index=source_index(pending_addr+j);
                    if(index<0||index>=load_total)$fatal(1,"out-of-range cartridge address %h",pending_addr+j);
                    if(pending_write&&pending_strobe[j]&&pending_data[j*8+:8]!==cartridge[index])
                        $fatal(1,"production stream payload changed at%0d",index);
                    memory_rdata[j*8+:8]<=cartridge[index];
                end
                memory_rvalid<=1;
            end
        end
    end

    reg [42:0] stalled_state;
    always @(posedge core_clk)if(!dut.machine_reset&&dut.memory_stall)begin
        stalled_state={dut.cpu_addr,dut.cycle,dut.scanline};
        if(dut.machine_ce)$fatal(1,"machine CE advanced through private cartridge ROM miss");
        #1;
        if({dut.cpu_addr,dut.cycle,dut.scanline}!==stalled_state)
            $fatal(1,"CPU/PPU state changed during private cartridge ROM stall");
    end

    task stream_byte(input integer offset);
        begin
            @(negedge core_clk);load_offset=offset;load_data=cartridge[offset];
            load_first=offset==0;load_last=offset+1==load_total;load_valid=1;
            do @(posedge core_clk);while(!load_ready);
            @(negedge core_clk);load_valid=0;
        end
    endtask

    always @(negedge core_clk)if(video_valid&&video_de)begin
        if(video_x==0&&video_y==0)begin
            if(frames!=0)begin
                if(frame_pixels!=61440)$fatal(1,"incomplete private cartridge raster:%0d",frame_pixels);
                if(frame_nonuniform)nonuniform_frames=nonuniform_frames+1;
                $display("NES PRIVATE FRAME %0d services=%0d master=%0d reads=%0d debt=%0d fetches=%0d nonuniform=%0d",
                    frames,service_cycles-last_service,master_ticks-last_ticks,rom_reads-last_reads,
                    dut.tick_debt,cpu_fetches,frame_nonuniform);
            end
            if(frames==target_frames)begin
                if(cpu_fetches<1000||nonuniform_frames<3||colors_seen==0)
                    $fatal(1,"private cartridge CPU or nonuniform picture initialization missing");
                result_file=$fopen("result.json","w");
                $fwrite(result_file,"{\"frames\":%0d,\"cpu_fetches\":%0d,\"rom_reads\":%0d,\"peak_debt\":%0d,\"final_debt\":%0d,\"nonuniform_frames\":%0d,\"nonuniform_pixels\":%0d,\"early_unknown_pixels\":%0d,\"colors_seen\":\"%h\",\"service_cycles\":%0d,\"master_ticks\":%0d}\n",
                    frames,cpu_fetches,rom_reads,peak_debt,dut.tick_debt,nonuniform_frames,
                    nonuniform_pixels,early_unknown_pixels,colors_seen,service_cycles,master_ticks);
                $fclose(result_file);$fclose(picture_file);
                $display("PASS NES private cartridge smoke:%0d frames,CPU fetches%0d,nonuniform frames%0d,peak debt%0d",
                    frames,cpu_fetches,nonuniform_frames,peak_debt);$finish;
            end
            frames=frames+1;frame_pixels=0;frame_nonuniform=0;
            first_rgb={video_r,video_g,video_b};
            last_service=service_cycles;last_ticks=master_ticks;last_reads=rom_reads;
            if(frames==target_frames)begin
                picture_file=$fopen("final.ppm","wb");$fwrite(picture_file,"P6\n256 240\n255\n");
            end
        end
        if(frames>0)begin
            if(video_x!==frame_pixels%256||video_y!==frame_pixels/256)
                $fatal(1,"private cartridge raster coordinate discontinuity");
            if($isunknown({video_r,video_g,video_b}))begin
                if(frames>=4)$fatal(1,"unknown visible private cartridge pixel after startup");
                early_unknown_pixels=early_unknown_pixels+1;
            end else begin
                colors_seen[dut.color]=1;
                if({video_r,video_g,video_b}!=first_rgb)begin
                    frame_nonuniform=1;nonuniform_pixels=nonuniform_pixels+1;
                end
            end
            if(frames==target_frames)$fwrite(picture_file,"%c%c%c",video_r,video_g,video_b);
            frame_pixels=frame_pixels+1;
        end
    end

    initial begin
        cartridge_path="canonical.nes";
        file_handle=$fopen("config.txt","r");
        if(!file_handle)$fatal(1,"runtime config file is required");
        i=$fscanf(file_handle,"%d %d %d",load_total,target_frames,latency);$fclose(file_handle);
        if(i!=3)$fatal(1,"runtime size/frame/latency config is invalid");
        file_handle=$fopen(cartridge_path,"rb");
        if(!file_handle)$fatal(1,"cannot open private cartridge runtime file");
        loaded_bytes=$fread(cartridge,file_handle);$fclose(file_handle);
        prg_bytes=cartridge[4]*16384;chr_bytes=cartridge[5]*8192;
        if(loaded_bytes!=load_total||load_total!=16+prg_bytes+chr_bytes||prg_bytes>524288||chr_bytes>262144)
            $fatal(1,"private cartridge size is outside harness bounds");
        repeat(5)@(negedge core_clk);reset=0;
        for(i=0;i<load_total;i=i+1)stream_byte(i);
        wait(cart_loaded);@(negedge core_clk);
        if(load_error||cart_size!=load_total)$fatal(1,"private canonical cartridge failed HDL loading");
        if(dut.mapper_flags[25])begin media_mount=1;@(negedge core_clk);media_mount=0;end
        core_reset_request=0;
    end
    initial begin #2000000000;$fatal(1,"private cartridge smoke timeout");end
endmodule
