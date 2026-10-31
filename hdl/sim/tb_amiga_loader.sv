// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_amiga_loader;
    reg clk=0,reset=1,clear_error=0,load_abort=0,load_abort_epoch=0,load_epoch=0;
    always #5 clk=~clk;
    reg [7:0] load_kind=0,load_data=0;
    reg load_valid=0,load_first=0,load_last=0;
    reg [31:0] load_offset=0,load_total=0;
    wire load_ready,error,rom_loaded,config_valid;
    wire [127:0] config_bytes;
    wire write_valid;
    wire [23:0] write_addr;
    wire [7:0] write_data;
    reg write_ready=1,write_done=0,memory_fault=0;
    multitini_amiga_loader dut(.*);
    integer writes=0,cycle=0,expected_offset=0;
    reg auto_done=1,check_rom=0,backpressure=1;
    reg [23:0] expected_address;
    function automatic [7:0] rom_byte(input integer offset);
        rom_byte=8'(offset^(offset>>8)^(offset>>16));
    endfunction
    always @(negedge clk)begin
        cycle=cycle+1;
        if(backpressure)write_ready=(cycle%11!=3 && cycle%11!=4 && cycle%17!=8);
    end
    always @(posedge clk)begin
        if(auto_done)write_done<=write_valid && write_ready;
        if(!reset && write_valid && write_ready)begin
            writes=writes+1;
            if(check_rom)begin
                expected_address=(expected_offset<524288 ? 24'h600000 : 24'h780000)+(expected_offset&524287);
                if(write_addr!==expected_address || write_data!==rom_byte(expected_offset))
                    $fatal(1,"Kickstart write offset%0d expected%h/%h got%h/%h",expected_offset,expected_address,rom_byte(expected_offset),write_addr,write_data);
                expected_offset=expected_offset+1;
            end
        end
    end
    task tick;begin @(posedge clk);#1;end endtask
    task send_byte(input [7:0] kind,input [31:0] offset,total,input [7:0] value,input first,last,epoch);
        integer timeout;
        begin
            @(negedge clk);timeout=0;
            while(!load_ready && timeout<100)begin @(negedge clk);timeout=timeout+1;end
            if(timeout==100)$fatal(1,"loader backpressure timed out");
            load_kind=kind;load_offset=offset;load_total=total;load_data=value;
            load_first=first;load_last=last;load_epoch=epoch;load_valid=1;
            @(negedge clk);load_valid=0;
        end
    endtask
    task clear;
        begin @(negedge clk);clear_error=1;tick;@(negedge clk);clear_error=0;end
    endtask
    task config_record(input [7:0] cpu,standard,blitter,disk,fastmem);
        reg [127:0] bytes;
        integer offset;
        begin
            bytes=0;bytes[31:0]=32'h31304d41;bytes[39:32]=cpu;
            bytes[47:40]=standard;bytes[55:48]=blitter;bytes[63:56]=disk;
            bytes[71:64]=fastmem;bytes[79:72]=2;
            for(offset=0;offset<16;offset=offset+1)
                send_byte(8'h60,offset,16,bytes[offset*8+:8],offset==0,offset==15,1);
            tick;
        end
    endtask
    task reset_loader;
        begin @(negedge clk);reset=1;load_valid=0;load_abort=0;memory_fault=0;write_done=0;auto_done=1;backpressure=1;
            repeat(4)tick;@(negedge clk);reset=0;repeat(3)tick;end
    endtask
    integer offset,saved_writes;
    initial begin
        reset_loader;
        config_record(0,0,0,0,8);
        if(!config_valid || error || config_bytes[39:32]!=0)$fatal(1,"stock AM01 config rejected");
        config_record(2,0,0,1,4);
        if(!config_valid || error || config_bytes[39:32]!=2 || config_bytes[71:64]!=4)$fatal(1,"turbo AM01 config rejected");
        config_record(0,1,0,0,8);
        if(config_valid || !error)$fatal(1,"unsupported NTSC config accepted");
        clear;config_record(0,0,1,0,8);
        if(config_valid || !error)$fatal(1,"unsupported fast blitter accepted");
        clear;config_record(1,0,0,0,8);
        if(config_valid || !error)$fatal(1,"unimplemented CPU mode accepted");
        clear;
        send_byte(8'h60,0,15,8'h41,1,0,0);tick;
        if(!error)$fatal(1,"bad config total accepted");
        clear;
        send_byte(8'h60,0,16,8'h41,1,0,0);
        send_byte(8'h60,2,16,8'h30,0,0,0);tick;
        if(!error)$fatal(1,"skipped config offset accepted");
        clear;
        send_byte(8'h60,0,16,8'h41,1,0,0);
        send_byte(8'h60,1,16,8'h4d,0,0,1);tick;
        if(!error)$fatal(1,"changed load epoch accepted");
        clear;
        send_byte(8'h61,0,1048576,0,1,1,0);tick;
        if(!error || write_valid)$fatal(1,"premature ROM last accepted");
        clear;
        check_rom=1;expected_offset=0;saved_writes=writes;
        for(offset=0;offset<1048576;offset=offset+1)
            send_byte(8'h61,offset,1048576,rom_byte(offset),offset==0,offset==1048575,1);
        while(!load_ready)tick;
        tick;check_rom=0;
        if(!rom_loaded || error || writes-saved_writes!=1048576 || expected_offset!=1048576)
            $fatal(1,"full Kickstart did not publish exactly1MiB");
        $display("PASS AMIGA loader full1MiB ROM,split apertures,each byte/address checked,write backpressure");
        // A new load must withdraw the previous ROM immediately. Abort with
        // the wrong epoch is ignored; the matching epoch cancels publication.
        auto_done=0;backpressure=0;write_ready=0;
        send_byte(8'h61,0,1048576,0,1,0,0);tick;
        if(rom_loaded)$fatal(1,"new ROM stream retained old loaded flag");
        load_abort=1;load_abort_epoch=1;tick;load_abort=0;tick;
        if(error)$fatal(1,"foreign epoch abort corrupted active stream");
        load_abort=1;load_abort_epoch=0;tick;load_abort=0;
        if(!error || rom_loaded)$fatal(1,"matching abort was not recorded");
        write_ready=1;tick;write_done=1;tick;write_done=0;tick;
        if(rom_loaded)$fatal(1,"aborted pending write published loaded");
        reset_loader;
        // Complete a second full stream, withholding only its final write
        // acknowledgement. Abort and acknowledgement on the same edge must
        // keep loaded clear, including the final commit boundary.
        for(offset=0;offset<1048576;offset=offset+1)begin
            if(offset==1048575)begin
                while(!load_ready)tick;
                @(negedge clk);auto_done=0;write_done=0;
            end
            send_byte(8'h61,offset,1048576,rom_byte(offset),offset==0,offset==1048575,0);
        end
        while(write_valid)tick;
        if(rom_loaded)$fatal(1,"ROM published before final write completed");
        @(negedge clk);load_abort=1;load_abort_epoch=0;write_done=1;
        tick;@(negedge clk);load_abort=0;write_done=0;tick;
        if(rom_loaded || !error)$fatal(1,"final ROM abort lost to completion");
        clear;
        // A memory fault invalidates a previously published image and may
        // never be cleared merely by the host's clear-error command.
        @(negedge clk);memory_fault=1;tick;memory_fault=0;tick;
        if(rom_loaded || !error)$fatal(1,"memory fault did not invalidate ROM");
        clear;tick;
        if(rom_loaded)$fatal(1,"clear error revived faulted ROM");
        reset_loader;
        for(offset=0;offset<1048576;offset=offset+1)begin
            if(offset==1048575)begin
                while(!load_ready)tick;
                @(negedge clk);auto_done=0;write_done=0;
            end
            send_byte(8'h61,offset,1048576,rom_byte(offset),offset==0,offset==1048575,1);
        end
        while(write_valid)tick;
        @(negedge clk);memory_fault=1;write_done=1;tick;
        @(negedge clk);memory_fault=0;write_done=0;tick;
        if(rom_loaded || !error)$fatal(1,"final memory fault lost to completion");
        $display("PASS AMIGA loader AM01 modes,rejected options,framing,epochs,abort-at-final-commit,memory fault");
        $finish;
    end
    initial begin #200000000;$fatal(1,"loader watchdog");end
endmodule
