// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_island_map;
    reg clk=0,reset=1,port_write_retired=0;
    always #5 clk=~clk;
    reg [7:0] port_pins=0;
    reg [15:0] addr=0;
    reg write=0;
    wire map_ok,local_cycle,local_write,port_write,colour_cycle;
    wire fio_cycle,forward_cycle,forward_ioonly,ff00_local,forward_ack;
    wire ram_store,colour_store;
    wire [2:0] read_source;
    multitini_c64_island_map dut(.*);
    integer checks=0;
    reg l,f,io,col,pw,ff,fw,ack;
    reg [2:0] src;
    task automatic check(input bit ok,input string why);
        checks++;
        if(!ok) $fatal(1,"MAP FAIL %s bank=%x addr=%x wr=%d",why,port_pins,addr,write);
    endtask
    initial begin
        repeat(3) @(negedge clk);reset=0;
        for(integer bank=0;bank<8;bank++) begin
            port_write_retired=1;@(posedge clk);#0.01;
            check(!map_ok,"port update must close map immediately");
            @(negedge clk);port_pins=bank;port_write_retired=0;
            @(posedge clk);#0.01;check(map_ok,"new map opens at next edge");
            for(integer a=0;a<65536;a++) for(integer w=0;w<2;w++) begin
                addr=a;write=w;#0.001;
                l=1;f=0;io=0;col=0;pw=(w && a<2);ff=0;fw=w;ack=0;src=(a>=32768)?1:0;
                if(!w && a>=16'ha000 && a<16'hc000 && (bank&3)==3) src=2;
                if(!w && a>=16'he000 && (bank&2)) src=2;
                if(a>=16'hd000 && a<16'he000 && (bank&3)) begin
                    if(!(bank&4)) begin if(!w) src=3;end
                    else if(a>=16'hd800 && a<16'hdc00) begin col=1;src=4;end
                    else if(a<16'hd800) begin l=0;fw=0;f=1;src=4;end
                    else begin
                        l=0;fw=0;io=1;src=5;
                        ack=(!w && a<16'hde00 && (a&15)==13) ||
                            (!w && a>=16'hdf00 && (a&31)==0) ||
                            (w && a>=16'hdf20 && (a&31)==31);
                    end
                end
                if(w && a==16'hff00) begin l=0;fw=0;io=1;ff=1;src=5;end
                check({local_cycle,local_write,port_write,colour_cycle,fio_cycle,forward_cycle,forward_ioonly,ff00_local,forward_ack,read_source}===
                      {l,fw,pw,col,f,io,io,ff,ack,src},"no-cartridge memory classes");
                check(ram_store === ((fw && !col) || ff) &&
                      colour_store === (fw && col),"independent store decode matches bus classes");
            end
        end
        $display("MAP PASS checks=%0d",checks);$finish;
    end
endmodule
