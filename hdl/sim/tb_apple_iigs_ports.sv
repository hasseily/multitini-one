// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_ports;
    reg clk=0, load_clk=0;
    always #5 clk=~clk;
    always #7 load_clk=~load_clk;
    reg ea=0,eb=0,wa=0,wb=0;
    reg [3:0] aa=0,ab=0;
    reg [7:0] da=0,db=0;
    wire [7:0] qa,qb;
    bram #(.widthad_a(4)) memory(.clock_a(clk),.enable_a(ea),.wren_a(wa),.address_a(aa),.data_a(da),.q_a(qa),
        .clock_b(load_clk),.enable_b(eb),.wren_b(wb),.address_b(ab),.data_b(db),.q_b(qb));
    reg ce=0,lwe=0;
    reg [3:0] address=0,laddr=0;
    reg [7:0] ldata=0;
    wire [7:0] rom_q;
    apple_iigs_runtime_rom #(.AW(4)) rom(.clock(clk),.ce(ce),.address(address),.q(rom_q),
        .load_clk(load_clk),.load_we(lwe),.load_addr(laddr),.load_data(ldata));
    reg [7:0] pram_addr=0,pram_data=0;
    reg pram_write=0;
    wire [7:0] pram_q;
    prtc rtc(.CLK_14M(clk),.cen(1'b0),.reset(1'b1),.addr(1'b0),.timestamp(33'b0),.din(8'b0),
        .dout(),.onesecond_irq(),.qtrsecond_irq(),.rw(1'b1),.strobe(1'b0),
        .bk_addr(pram_addr),.bk_wr(pram_write),.bk_data(pram_data),.bk_q(pram_q));
    initial begin
        #1;
        if (qa!==0 || qb!==0) $fatal(1,"BRAM zero initialization");
        for (integer i=0;i<256;i=i+1) begin
            pram_addr=i; #1;
            if (pram_q!==0) $fatal(1,"PRAM embeds initial data instead of invalid zero state");
        end
        @(negedge clk); ea=1;wa=1;aa=3;da=8'hA5;
        @(posedge clk); #1;
        if(qa!==8'hA5) $fatal(1,"BRAM port A must be write-first");
        @(negedge clk); ea=0;aa=4;da=8'h22;
        @(posedge clk); #1;
        if(qa!==8'hA5) $fatal(1,"BRAM port A CE must hold output and suppress write");
        @(negedge load_clk); eb=1;ab=3;wb=0;
        @(posedge load_clk); #1;
        if(qb!==8'hA5) $fatal(1,"BRAM second clock synchronous read");
        @(negedge load_clk); wb=1;ab=4;db=8'h5A;
        @(posedge load_clk); #1;
        if(qb!==8'h5A) $fatal(1,"BRAM port B must be write-first");
        @(negedge load_clk); eb=0;wb=0;
        @(negedge clk); ea=1;wa=0;aa=4;
        @(posedge clk); #1;
        if(qa!==8'h5A) $fatal(1,"BRAM cross-clock write/read");
        @(negedge load_clk); lwe=1;laddr=1;ldata=8'h77;
        @(posedge load_clk); #1;
        @(negedge load_clk); laddr=2;ldata=8'h88;
        @(posedge load_clk); #1;
        @(negedge load_clk); lwe=0;
        @(negedge clk); ce=1;address=1;
        @(posedge clk); #1;
        if(rom_q!==8'h77) $fatal(1,"Runtime ROM read after upload");
        @(negedge clk); ce=0;address=2;
        @(posedge clk); #1;
        if(rom_q!==8'h77) $fatal(1,"Runtime ROM read CE hold");
        @(negedge clk); ce=1;
        @(posedge clk); #1;
        if(rom_q!==8'h88) $fatal(1,"Runtime ROM one-edge read latency");
        @(negedge clk); pram_addr=8'h42;pram_data=8'hCD;pram_write=1;
        @(posedge clk); #1;
        if(pram_q!==8'hCD) $fatal(1,"Runtime PRAM backup restore");
        @(negedge clk); pram_write=0;
        $display("PASS: IIgs inferred RAM latency/enables/write-first, runtime ROM and blank/restored PRAM");
        $finish;
    end
    initial begin #100000; $fatal(1,"timeout"); end
endmodule
