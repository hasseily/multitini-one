// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_loader;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,core_reset_request=1,clear_error=0,byte_tick=1;
    reg load_abort=0,load_abort_epoch=0,load_epoch=0;
    reg [7:0] load_kind=0,load_data=0;
    reg load_valid=0,load_first=0,load_last=0;
    reg [31:0] load_offset=0,load_total=0;
    wire load_ready,error,rom_loaded,config_valid,config_commit;
    wire [127:0] config_bytes;
    wire aux_write,sector_write,nv_write,invalidate,write_valid,write_enable;
    wire [15:0] aux_addr;wire [7:0] aux_data,sector_data,nv_data;
    wire [8:0] sector_addr,nv_addr;
    wire [23:0] write_addr;wire [63:0] write_data;wire [7:0] write_strobe;
    reg write_ready=1,write_response=0,memory_fault=0;
    multitini_apple_iigs_loader dut(.*);
    integer count=0,delay_count=0;
    reg waiting=0;
    always @(posedge clk)begin
        write_response<=0;
        if(write_valid && write_ready)begin
            if(write_addr!==(24'hf80000+count*8) || write_strobe!=255 || !write_enable)$fatal(1,"packed ROM address/strobe");
            for(integer lane=0;lane<8;lane=lane+1)if(write_data[lane*8+:8]!==((count*8+lane)&255))$fatal(1,"packed ROM lane");
            count<=count+1;delay_count<=3;waiting<=1;write_ready<=0;
        end
        if(waiting)begin
            if(delay_count!=0)delay_count<=delay_count-1;
            else begin write_response<=1;waiting<=0;write_ready<=1;end
        end
    end
    task send(input [7:0] kind,input integer length);
        begin
            load_epoch=~load_epoch;
            for(integer i=0;i<length;i=i+1)begin
                @(negedge clk);load_kind=kind;load_total=length;load_offset=i;load_data=i;
                load_first=i==0;load_last=i==length-1;load_valid=1;
                while(!load_ready)@(negedge clk);
                @(negedge clk);load_valid=0;
            end
            repeat(12)@(negedge clk);
        end
    endtask
    task config_packet(input [7:0] speed,input [7:0] card);
        begin
            load_epoch=~load_epoch;
            for(integer i=0;i<16;i=i+1)begin
                @(negedge clk);load_kind=8'h43;load_total=16;load_offset=i;
                load_data=i==1 ? speed : i==2 ? card : 0;load_first=i==0;load_last=i==15;load_valid=1;
                while(!load_ready)@(negedge clk);
                @(negedge clk);load_valid=0;
            end
            repeat(12)@(negedge clk);
        end
    endtask
    initial begin
        repeat(3)@(negedge clk);reset=0;
        config_packet(4,0);if(!config_valid || rom_loaded)$fatal(1,"config or missing ROM gate");
        config_packet(5,0);if(error || config_bytes[15:8]!=5)$fatal(1,"28MHz speed rejected");
        config_packet(6,0);if(error || config_bytes[15:8]!=6)$fatal(1,"57MHz speed rejected");
        config_packet(7,0);if(!error || config_bytes[15:8]!=6)$fatal(1,"invalid speed mutated config");
        clear_error=1;@(negedge clk);clear_error=0;
        send(8'h41,131072);send(8'h42,4096);send(8'h46,4096);send(8'h45,256);
        if(error || !rom_loaded || count!=16384)$fatal(1,"complete runtime ROM load");
        config_packet(4,1);if(!error)$fatal(1,"unsupported accelerator accepted");
        clear_error=1;@(negedge clk);clear_error=0;
        core_reset_request=0;
        // A system ROM cannot be overwritten while executing.
        load_epoch=~load_epoch;load_kind=8'h41;load_total=131072;load_offset=0;load_data=0;load_first=1;load_last=0;load_valid=1;
        @(negedge clk);load_valid=0;@(negedge clk);if(!error)$fatal(1,"live system ROM overwrite accepted");
        core_reset_request=1;clear_error=1;@(negedge clk);clear_error=0;
        // A stale byte after abort is drained and cannot revive ROM readiness.
        load_epoch=~load_epoch;load_kind=8'h42;load_total=4096;load_offset=0;load_first=1;load_valid=1;
        @(negedge clk);load_valid=0;load_abort=1;load_abort_epoch=load_epoch;
        @(negedge clk);load_abort=0;load_first=0;load_offset=1;load_valid=1;
        @(negedge clk);load_valid=0;@(negedge clk);
        if(rom_loaded || error)$fatal(1,"aborted stream revived or stale byte failed");
        $display("PASS: IIgs ROM packing/bounds, auxiliary ROM readiness, configuration validation, live ROM protection and abort epochs");$finish;
    end
    initial begin #30000000;$fatal(1,"timeout");end
endmodule
