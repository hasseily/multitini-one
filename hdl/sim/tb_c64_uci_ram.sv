// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_uci_ram;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,hold=0,paused=0,reu_idle=0;
    reg load_valid=0,load_first=0,load_last=0,load_abort=0,clear_error=0;
    reg [7:0] load_data=0;reg [31:0] load_offset=0,load_total=0,read_request=0;
    wire load_ready,load_error,ram_write,peek_request,buffer_owned,buffer_write;
    wire [15:0] ram_address,peek_address;wire [7:0] ram_data,buffer_data;
    wire [10:0] buffer_address;wire [3:0] read_done;
    reg peek_valid=0;reg [7:0] peek_data=0;reg [7:0] ram[0:65535],buffer[0:895];
    integer checks=0,writes=0,buffer_writes=0,waits;
    multitini_c64_uci_ram dut(.*);
    always @(posedge clk)begin
        peek_valid<=peek_request;
        if(peek_request)peek_data<=ram[peek_address];
        if(ram_write)begin ram[ram_address]<=ram_data;writes<=writes+1;end
        if(buffer_write)begin buffer[buffer_address-'h380]<=buffer_data;buffer_writes<=buffer_writes+1;end
    end
    task tick(input integer n);repeat(n)begin @(posedge clk);#1;end endtask
    task check(input bit ok,input string message);checks++;if(!ok)$fatal(1,"UCI RAM %s",message);endtask
    task byte_load(input integer offset,input [7:0] value);
        @(negedge clk);load_valid=1;load_offset=offset;load_data=value;
        load_first=offset==0;load_last=offset+1==load_total;
        tick(1);@(negedge clk);load_valid=0;tick(1);
    endtask
    initial begin
        for(integer i=0;i<65536;i++)ram[i]=i[7:0]^8'h5a;
        tick(2);reset=0;tick(2);check(!load_ready,"refuse without HOLD");
        hold=1;paused=1;tick(1);check(!load_ready,"wait until REU idle");
        reu_idle=1;tick(1);check(load_ready,"acknowledged hold ready");
        load_total=5;byte_load(0,8'hff);byte_load(1,8'hff);byte_load(2,8'h11);byte_load(3,8'h22);byte_load(4,8'h33);
        check(writes==3 && ram[65535]==8'h11 && ram[0]==8'h22 && ram[1]==8'h33,"physical write wrap, no CPU reset");
        load_total=4;byte_load(0,0);byte_load(2,8'h66);check(load_error && writes==3,"out-of-order bytes rejected");
        clear_error=1;tick(1);clear_error=0;check(!load_error,"clear error");
        load_total=4;byte_load(0,0);byte_load(1,8'h20);load_abort=1;tick(1);load_abort=0;
        byte_load(2,8'h77);check(load_error && writes==3,"aborted stream cannot resume");
        read_request={4'd1,1'b1,1'b0,10'd895,16'hff00};
        waits=0;while(read_done!=1 && waits<6000)begin tick(1);waits++;end
        check(waits<6000 && buffer_writes==896,"bounded maximum read completes after writes");
        for(integer i=0;i<896;i++)check(buffer[i]===ram[(16'hff00+i)&65535],"read bytes cross RAM wrap");
        tick(20);check(buffer_writes==896,"same token cannot replay");
        reset=1;tick(2);reset=0;tick(10);check(read_done==1 && !buffer_owned,"reset consumes stale request baseline");
        read_request={4'd2,1'b1,1'b0,10'd0,16'h1234};hold=0;tick(10);check(read_done==1,"read waits for HOLD");
        hold=1;waits=0;while(read_done!=2 && waits<30)begin tick(1);waits++;end
        check(read_done==2 && buffer[0]==ram['h1234],"new request after reset");
        $display("PASS C64 UCI RAM checks=%0d",checks);$finish;
    end
    initial begin #200000;$fatal(1,"UCI RAM timeout");end
endmodule
