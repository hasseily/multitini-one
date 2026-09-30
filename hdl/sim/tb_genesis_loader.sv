// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_genesis_loader;
    import multitini_genesis_defs_pkg::*;
    reg clk=0,reset=1,core_reset_request=1,clear_error=0;
    reg load_abort=0,load_abort_epoch=0,load_epoch=0;
    always #5 clk=~clk;
    reg [7:0] load_data=0,load_kind=0;
    reg load_valid=0,load_first=0,load_last=0;
    reg [31:0] load_offset=0,load_total=0;
    wire load_ready,error,cart_loaded,config_valid;
    wire [31:0] cart_size;
    wire [7:0] region;
    wire [1:0] six_button,lowpass;
    wire fm_chip,cart_download,cart_word_write;
    wire [22:0] cart_word_address;
    wire [15:0] cart_word_data;
    wire [15:0] save_address;wire [7:0] save_data;
    wire save_write,cache_invalidate;
    wire [23:0] write_addr;wire [63:0] write_data;wire [7:0] write_strobe;
    wire write_valid;reg write_ready=0,write_response=0,memory_fault=0;
    integer cycle=0,pending=0,writes=0,words=0,saves=0,invalidations=0;
    reg verify_cart=0,verify_save=0;
    multitini_genesis_loader dut(.*);
    function automatic [7:0] pattern(input integer address);
        pattern=8'(address^(address>>8)^8'ha7);
    endfunction
    always @(posedge clk)begin
        cycle<=cycle+1;write_ready<=cycle%5!=0;write_response<=0;
        if(write_valid && write_ready)begin
            if(pending!=0)$fatal(1,"overlapping ROM writes");
            pending<=4;writes<=writes+1;
            if(verify_cart)begin
                if(write_addr!=24'(writes*8) || write_strobe!=8'hff)$fatal(1,"ROM line address or mask");
                for(integer b=0;b<8;b=b+1)
                    if(write_data[b*8+:8]!=pattern(writes*8+b))$fatal(1,"ROM byte order");
            end
        end else if(pending>0)begin
            pending<=pending-1;
            if(pending==1)write_response<=1;
        end
        if(cart_word_write)begin
            if(verify_cart && (cart_word_address!=23'(words) ||
                cart_word_data!={pattern(words*2),pattern(words*2+1)}))$fatal(1,"mapper download endian/order");
            words<=words+1;
        end
        if(save_write)begin
            if(verify_save && (save_address!=16'(saves) || save_data!=pattern(saves)))$fatal(1,"save byte order");
            saves<=saves+1;
        end
        if(cache_invalidate)invalidations<=invalidations+1;
    end
    task automatic send(input [7:0] kind,input integer offset,total,input [7:0] data,
                        input bit first,last);
        begin
            @(negedge clk);while(!load_ready)@(negedge clk);
            load_kind=kind;load_offset=offset;load_total=total;load_data=data;
            load_first=first;load_last=last;load_valid=1;
            @(negedge clk);load_valid=0;
        end
    endtask
    task automatic configuration(input [7:0] requested_region,input bit bad_reserved);
        reg [127:0] config_word;
        begin
            config_word=128'd0;config_word[31:0]=MT_GENESIS_CONFIG_MAGIC;
            config_word[39:32]=requested_region;config_word[47:40]=3;config_word[63:56]=1;
            if(bad_reserved)config_word[64]=1;
            for(integer i=0;i<16;i=i+1)send(MT_LOAD_GENESIS_CONFIG,i,16,config_word[i*8+:8],i==0,i==15);
        end
    endtask
    task automatic clear_fault;
        begin @(negedge clk);clear_error=1;@(negedge clk);clear_error=0;end
    endtask
    initial begin
        repeat(4)@(negedge clk);reset=0;
        send(MT_LOAD_GENESIS_CART,0,512,0,1,0);
        if(!error || cart_loaded)$fatal(1,"cartridge accepted without config");
        clear_fault();configuration(MT_GENESIS_REGION_EU,0);
        if(!error || config_valid)$fatal(1,"PAL config accepted by NTSC image");
        clear_fault();configuration(MT_GENESIS_REGION_US,1);
        if(!error || config_valid)$fatal(1,"nonzero reserved config accepted");
        clear_fault();configuration(MT_GENESIS_REGION_JP,0);
        if(error || !config_valid || region!=MT_GENESIS_REGION_JP || six_button!=3 || lowpass!=1 || fm_chip)
            $fatal(1,"valid config did not commit");
        @(negedge clk);writes=0;words=0;verify_cart=1;
        for(integer i=0;i<512;i=i+1)send(MT_LOAD_GENESIS_CART,i,512,pattern(i),i==0,i==511);
        if(cart_loaded)$fatal(1,"cart published before final physical write response");
        wait(cart_loaded);@(negedge clk);
        if(error || cart_size!=512 || writes!=64 || words!=256 || cart_download)$fatal(1,"cart commit/count");
        verify_cart=0;verify_save=1;saves=0;
        for(integer i=0;i<65536;i=i+1)send(MT_LOAD_GENESIS_SAVE,i,65536,pattern(i),i==0,i==65535);
        repeat(2)@(negedge clk);
        if(error || saves!=65536 || !cart_loaded)$fatal(1,"save restore failed");
        verify_save=0;
        send(MT_LOAD_GENESIS_CART,0,512,0,1,0);
        send(MT_LOAD_GENESIS_CART,2,512,0,0,0);
        if(!error || cart_loaded || config_valid)$fatal(1,"gapped stream did not invalidate content");
        clear_fault();configuration(MT_GENESIS_REGION_US,0);
        core_reset_request=0;send(MT_LOAD_GENESIS_CART,0,512,0,1,0);
        if(!error || cart_loaded)$fatal(1,"running machine accepted ROM mutation");
        core_reset_request=1;clear_fault();configuration(MT_GENESIS_REGION_US,0);
        // Cancel after a line is accepted, deliver an old mailbox byte, and
        // start the new epoch before its delayed old response can publish.
        for(integer i=0;i<8;i=i+1)send(MT_LOAD_GENESIS_CART,i,512,pattern(i),i==0,0);
        wait(write_valid && write_ready);@(negedge clk);
        load_abort=1;load_abort_epoch=load_epoch;
        @(negedge clk);load_abort=0;
        send(MT_LOAD_GENESIS_CART,8,512,0,0,0);
        if(cart_loaded || config_valid)$fatal(1,"aborted transaction remained usable");
        load_epoch=~load_epoch;
        configuration(MT_GENESIS_REGION_US,0);
        if(!config_valid || error)$fatal(1,"new epoch did not recover after aborted write");
        // A delayed old abort must also be ignored while the new CONFIG is idle.
        load_abort=1;load_abort_epoch=~load_epoch;
        @(negedge clk);load_abort=0;
        if(!config_valid || error)$fatal(1,"stale abort canceled committed config");
        // A delayed abort for the other epoch must not destroy a new stream.
        send(MT_LOAD_GENESIS_CART,0,512,pattern(0),1,0);
        load_abort=1;load_abort_epoch=~load_epoch;
        @(negedge clk);load_abort=0;
        for(integer i=1;i<512;i=i+1)send(MT_LOAD_GENESIS_CART,i,512,pattern(i),0,i==511);
        wait(cart_loaded);@(negedge clk);
        if(error || !config_valid)$fatal(1,"stale abort canceled the replacement stream");
        load_abort=1;load_abort_epoch=~load_epoch;
        @(negedge clk);load_abort=0;
        if(!cart_loaded || !config_valid || error)$fatal(1,"stale abort canceled committed cart");
        configuration(MT_GENESIS_REGION_US,0);
        for(integer i=0;i<512;i=i+1)send(MT_LOAD_GENESIS_CART,i,512,pattern(i),i==0,i==511);
        memory_fault=1;repeat(12)@(negedge clk);memory_fault=0;
        if(!error || cart_loaded)$fatal(1,"memory error published cartridge");
        if(invalidations<4)$fatal(1,"cartridge boundaries did not invalidate cache");
        $display("PASS Genesis loader: config validation, full ROM/save ordering, backpressure and faults");
        $finish;
    end
    initial begin #3000000;$fatal(1,"loader timeout");end
endmodule
