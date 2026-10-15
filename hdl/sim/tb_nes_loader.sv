// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_nes_loader;
    import multitini_nes_defs_pkg::*;
    reg clk=0,reset=1,core_reset_request=1,clear_error=0;
    always #5 clk=~clk;
    reg load_abort=0,load_abort_epoch=0,load_epoch=0;
    reg [7:0] load_data=0,load_kind=MT_LOAD_NES_CART;
    reg load_valid=0,load_first=0,load_last=0;
    reg [31:0] load_offset=0,load_total=16400;
    wire load_ready,error,cart_loaded,active,invalidate;
    wire [31:0] cart_size;
    wire [63:0] mapper_flags,write_data;
    wire [9:0] prg_mask,chr_mask;
    wire [12:0] save_address;
    wire [7:0] save_data,write_strobe;
    wire save_write,write_valid;
    wire [23:0] write_addr;
    wire write_ready;
    reg write_response=0,memory_fault=0;
    integer ticks=0,delay_count=0,writes=0,save_count=0;
    reg check_writes=1;
    reg [7:0] header[0:15];
    assign write_ready=delay_count==0 && ticks%3!=0;
    multitini_nes_loader dut(.*);
    function automatic [7:0] pattern(input integer offset);
        pattern=8'(offset^(offset>>8));
    endfunction
    always @(posedge clk)begin
        ticks<=ticks+1;write_response<=0;
        if(delay_count!=0)begin
            delay_count<=delay_count-1;
            if(delay_count==1)write_response<=1;
        end
        if(write_valid&&write_ready)begin
            if(delay_count!=0 || write_strobe!=255 || write_addr[2:0]!=0)
                $fatal(1,"malformed/overlapping write transaction");
            if(check_writes)begin
                if(write_addr!=writes*8)$fatal(1,"cartridge write address %h",write_addr);
                for(integer lane=0;lane<8;lane=lane+1)
                    if(write_data[lane*8+:8]!==pattern(write_addr+lane))
                        $fatal(1,"cartridge byte mismatch at%0d",write_addr+lane);
            end
            writes<=writes+1;delay_count<=5;
        end
        if(save_write)begin
            if(save_address!=save_count || save_data!==pattern(save_count))
                $fatal(1,"save restore address/data changed at%0d",save_count);
            save_count<=save_count+1;
        end
    end
    task byte_at(input integer offset,input [7:0] data,input integer total);
        begin
            @(negedge clk);load_offset=offset;load_data=data;load_total=total;
            load_first=offset==0;load_last=offset==total-1;load_valid=1;
            @(posedge clk);while(!load_ready)@(posedge clk);
            @(negedge clk);load_valid=0;load_first=0;load_last=0;
        end
    endtask
    task cart(input bit battery,input integer last);
        begin
            for(integer n=0;n<16;n=n+1)header[n]=0;
            header[0]="N";header[1]="E";header[2]="S";header[3]=26;header[4]=1;
            header[6]=battery?2:0;
            for(integer n=0;n<=last;n=n+1)
                byte_at(n,n<16?header[n]:pattern(n-16),16400);
        end
    endtask
    integer before_writes;
    initial begin
        repeat(5)@(negedge clk);reset=0;
        cart(0,16399);
        if(cart_loaded)$fatal(1,"cart committed before final PSRAM write response");
        wait(cart_loaded);@(negedge clk);
        if(error || active || cart_size!=16400 || writes!=2048 || prg_mask!=7 ||
           chr_mask!=3 || !mapper_flags[15] || mapper_flags[25] ||
           mapper_flags[29:26]!=7 || mapper_flags[34:31]!=0)
            $fatal(1,"NROM CHR-RAM metadata/commit invalid flags%h",mapper_flags);
        // Loading outside held reset must fail without issuing memory writes.
        core_reset_request=0;before_writes=writes;
        byte_at(0,8'h4e,16400);repeat(8)@(negedge clk);
        if(!error || cart_loaded || writes!=before_writes)$fatal(1,"unsafe running load accepted");
        core_reset_request=1;load_epoch=1;writes=0;
        cart(1,16399);wait(cart_loaded);@(negedge clk);
        if(error || !mapper_flags[25] || mapper_flags[29:26]!=0 || mapper_flags[34:31]!=7)
            $fatal(1,"battery RAM/NVRAM metadata invalid");
        // Restore all 8 KiB without writing cartridge storage.
        before_writes=writes;load_epoch=0;load_kind=MT_LOAD_NES_SAVE;
        for(integer n=0;n<8192;n=n+1)byte_at(n,pattern(n),8192);
        repeat(3)@(negedge clk);
        if(error || !cart_loaded || active || save_count!=8192 || writes!=before_writes)
            $fatal(1,"save restore incomplete");
        // Abort an accepted external write and quarantine late bytes of its epoch.
        load_kind=MT_LOAD_NES_CART;load_epoch=1;writes=0;
        cart(0,23);wait(delay_count!=0);
        @(negedge clk);load_abort_epoch=1;load_abort=1;
        @(negedge clk);load_abort=0;
        byte_at(24,8'hff,16400);repeat(12)@(negedge clk);
        if(cart_loaded || active || error)$fatal(1,"aborted epoch revived or poisoned loader");
        load_epoch=0;writes=0;cart(0,16399);wait(cart_loaded);@(negedge clk);
        if(error || writes!=2048)$fatal(1,"new epoch did not recover after abort");
        // A malformed iNES header rejects before the first storage write.
        load_epoch=1;before_writes=writes;
        for(integer n=0;n<16;n=n+1)byte_at(n,n==7?8'h08:header[n],16400);
        repeat(4)@(negedge clk);
        if(!error || active || cart_loaded || writes!=before_writes)
            $fatal(1,"unsupported NES2 header accepted");
        $display("PASS NES loader: exact cartridge bytes, delayed commit, held-reset admission, battery restore, epoch abort/stale quarantine, invalid header rejection");
        $finish;
    end
    initial begin #20000000;$fatal(1,"NES loader timeout");end
endmodule
