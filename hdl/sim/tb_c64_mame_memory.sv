// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Production decoder/ROM/RAM primitives; synthetic peripheral and cartridge
// data. The runner exposes internal decode wires without changing their logic.
// This is an isolated transaction test, not CPU/VIC arbitration timing proof.
module tb_c64_mame_memory;
    reg clk=0;
    always #5 clk=~clk;
    reg [1:0] bios=0;
    reg owner=1, aec=1, cpuWe=0, game=1, exrom=1;
    reg [2:0] bank=7;
    reg [15:0] address=0, vicaddr=0;
    reg [7:0] data=0, floating=0;
    reg [1:0] mode=0;
    reg [13:0] rom_addr=0;
    reg [7:0] rom_data=0;
    reg rom_wr=0, accept=0, probe=0;
    reg loader_we=0;
    reg [15:0] loader_addr=0;
    reg [7:0] loader_data=0;
    reg color_load=0;
    reg [9:0] color_addr=0;
    reg [3:0] color_input=0;
    wire [7:0] ram_q,ram_dout,ram_data,cpu_q,vic_q;
    wire [3:0] color_q;
    wire [15:0] system_addr;
    wire system_we,cs_vic,cs_sid,cs_color,cs_cia1,cs_cia2,cs_ram,cs_ioe,cs_iof,cs_roml,cs_romh,cs_umaxh;
    wire [14:0] observed_decode;
    wire [14:0] public_select={1'b0,cs_umaxh,cs_romh,cs_roml,cs_iof,cs_ioe,cs_cia2,cs_cia1,cs_color,cs_sid,cs_vic,4'b0};
    wire [7:0] cart_rom=8'h96 ^ system_addr[7:0] ^ {3'b0,system_addr[12:8]};
    wire [7:0] cart_io=8'h69 ^ system_addr[7:0];
    assign ram_data=(cs_roml||cs_romh||cs_umaxh)?cart_rom:
                    ((cs_ioe||cs_iof)&&mode==1)?cart_io:ram_q;
    fpga64_buslogic dut (
        .clk(clk),.reset(1'b0),.bios(bios),.cpuHasBus(owner),.aec(aec),.ramData(ram_data),
        .bankSwitch(bank),.game(game),.exrom(exrom),.io_rom(mode==1),.io_ext(mode==2),
        .io_data(8'h3c ^ system_addr[7:0]),.c64rom_addr(rom_addr),.c64rom_data(rom_data),.c64rom_wr(rom_wr),
        .cpuWe(cpuWe),.cpuAddr(address),.cpuData(data),.vicAddr(vicaddr),
        .vicData(8'ha5 ^ {2'b0,system_addr[5:0]}),.sidData(8'h54 ^ {3'b0,system_addr[4:0]}),
        .colorData(color_q),.cia1Data(8'hc1 ^ {4'b0,system_addr[3:0]}),.cia2Data(8'hc2 ^ {4'b0,system_addr[3:0]}),
        .lastVicData(floating),.io_enable(1'b1),.systemWe(system_we),.systemAddr(system_addr),
        .dataToCpu(cpu_q),.dataToVic(vic_q),.cs_vic(cs_vic),.cs_sid(cs_sid),.cs_color(cs_color),
        .cs_cia1(cs_cia1),.cs_cia2(cs_cia2),.cs_ram(cs_ram),.cs_ioE(cs_ioe),.cs_ioF(cs_iof),
        .cs_romL(cs_roml),.cs_romH(cs_romh),.cs_UMAXromH(cs_umaxh),.observed_decode(observed_decode));
    // This helper is generated from the exact production fpga64 ramDout
    // assignment; the runner checks extraction and hashes the source.
    c64_memory_write_data write_data(.systemWe(system_we),.systemAddr(system_addr),
        .lastVicDi(floating),.cpuDo(data),.ramDout(ram_dout));
    multitini_c64_local_ram ram(.clk(clk),.loader_we(loader_we),.loader_addr(loader_addr),.loader_data(loader_data),
        .machine_ce(probe||(accept&&cs_ram)),.machine_we(!probe&&system_we),.machine_store(1'b1),
        .machine_addr(probe?address:system_addr),.machine_data(ram_dout),.machine_q(ram_q));
    spram #(.DATA_WIDTH(4),.ADDR_WIDTH(10)) color_ram(.clk(clk),
        .addr(color_load?color_addr:system_addr[9:0]),.data(color_load?color_input:data[3:0]),
        .we(color_load||(accept&&system_we&&cs_color)),.q(color_q));

    integer f,out,rc,i,id,in_owner,in_write,in_addr,in_vic,in_bank,in_game,in_exrom,in_ba,in_float,in_data,in_mode,count=0;
    integer captured_result,captured_flags,captured_public,captured_ram_request,captured_addr,captured_we;
    initial begin
        // Original synthetic patterns only; no system ROM images.
        for(i=0;i<65536;i=i+1) begin
            @(negedge clk); loader_we=1;loader_addr=i;loader_data=i^(i>>8);
        end
        @(negedge clk);loader_we=0;
        for(i=0;i<16384;i=i+1) begin
            @(negedge clk);rom_wr=1;bios=0;rom_addr=i;
            rom_data=(i<8192?8'hb1:8'he2)^(i&8191)^((i&8191)>>8);
        end
        for(i=0;i<4096;i=i+1) begin
            @(negedge clk);bios=1;rom_addr=i;rom_data=8'hc3^i^(i>>8);
        end
        @(negedge clk);rom_wr=0;
        for(i=0;i<1024;i=i+1) begin
            @(negedge clk);color_load=1;color_addr=i;color_input=4'hb^i^(i>>4);
        end
        @(negedge clk);color_load=0;
        f=$fopen("stimulus.txt","r");out=$fopen("hdl_observed.csv","w");
        if(!f||!out)$fatal(1,"Cannot open memory fixture files");
        $fwrite(out,"id,result,ram_after,color_after,decode,public_select,ram_request,system_address,system_write\n");
        while(!$feof(f)) begin
            rc=$fscanf(f,"%d %d %d %h %h %h %h %h %h %h %h %h\n",id,in_owner,in_write,in_addr,in_vic,in_bank,in_game,in_exrom,in_ba,in_float,in_data,in_mode);
            if(rc!=12)$fatal(1,"Malformed stimulus at row %0d",count);
            @(negedge clk);accept=0;probe=0;owner=in_owner;cpuWe=in_write;address=in_addr;vicaddr=in_vic;
            bank=in_bank;game=in_game;exrom=in_exrom;floating=in_float;data=in_data;mode=in_mode;
            // Restore isolated write targets; read cases use their current
            // synthetic value, restored here as well to prevent dependencies.
            loader_we=1;loader_addr=in_addr;loader_data=in_addr^(in_addr>>8);
            color_load=1;color_addr=in_addr&1023;color_input=4'hb^in_addr^(in_addr>>4);
            @(negedge clk);loader_we=0;color_load=0;accept=1;
            @(posedge clk);#1;
            captured_result=in_write?-1:(in_owner?cpu_q:vic_q);
            captured_flags=observed_decode;captured_public=public_select;captured_ram_request=cs_ram;
            captured_addr=system_addr;captured_we=system_we;
            @(negedge clk);accept=0;probe=1;
            @(posedge clk);#1;
            $fwrite(out,"%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d,%0d\n",id,captured_result,ram_q,color_q,
                captured_flags,captured_public,captured_ram_request,captured_addr,captured_we);
            count=count+1;
            @(negedge clk);probe=0;
        end
        $fclose(f);$fclose(out);
        $display("PASS: C64 memory HDL observations %0d isolated transactions",count);
        $finish;
    end
    initial begin #20000000;$fatal(1,"memory fixture timeout");end
endmodule
