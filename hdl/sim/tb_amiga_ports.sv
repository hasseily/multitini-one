// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module BUFG(input wire I,output wire O); assign O=I; endmodule
module BUFGCE(input wire I,CE,output wire O);
    reg enabled=0;
    always_latch if(!I) enabled=CE;
    assign O=I && enabled;
endmodule

module tb_amiga_ports;
    reg clk=0;
    always #17.62 clk=~clk;
    reg reset=1,guest_reset=1,run_enable=1;
    wire chip_clk,chip_tick;
    reg io_uio=0,io_fpga=0,io_strobe=0;
    reg [15:0] io_din=0;
    wire io_wait;
    wire [15:0] io_dout;
    wire [4:0] cpucfg;
    wire [6:0] memcfg;
    wire cpu_reset_n;
    reg [4:0] ide_address=0;
    reg ide_write=0;
    reg [15:0] ide_wdata=0;
    integer chip_edges=0;
    integer tick_edges=0;
    integer before_pause;
    always @(posedge chip_clk) if(!reset)chip_edges=chip_edges+1;
    always @(posedge clk) if(chip_tick && !reset) tick_edges=tick_edges+1;

    multitini_amiga_machine dut(
        .clk_service28(clk),.reset(reset),.guest_reset(guest_reset),.run_enable(run_enable),
        .clk_chip(chip_clk),.chip_tick(chip_tick),
        .chip_rdata(16'd0),.chip_rdata2(16'd0),.chip_rdata48(48'd0),
        .cpu_rdata(16'd0),.cpu_rdata2(16'd0),.cpu_ready(1'b0),.cpu_ready_long(1'b0),
        .io_uio(io_uio),.io_fpga(io_fpga),.io_strobe(io_strobe),.io_din(io_din),
        .io_wait(io_wait),.io_dout(io_dout),
        .ide_address(ide_address),.ide_write(ide_write),.ide_read(1'b0),.ide_wdata(ide_wdata),
        .joy1(16'hffff),.joy2(16'hffff),.mouse_buttons(3'd0),
        .kms_level(1'b0),.kbd_mouse_type(2'd0),.kbd_mouse_data(8'd0),.rtc(65'd0),
        .cpucfg(cpucfg),.memcfg(memcfg),.cpu_reset_n(cpu_reset_n));

    reg [3:0] ra=0,rb=0;
    reg [7:0] da=0,db=0;
    reg wa=0,wb=0,ea=1,eb=1,csa=1,csb=1;
    wire [7:0] qa,qb;
    dpram #(4,8) ram(.clock(clk),.address_a(ra),.data_a(da),.enable_a(ea),.wren_a(wa),.q_a(qa),.cs_a(csa),
                    .address_b(rb),.data_b(db),.enable_b(eb),.wren_b(wb),.q_b(qb),.cs_b(csb));
    task automatic uio_config(input [7:0] command,input [15:0] payload);
        @(negedge chip_clk); io_uio=1;io_din={8'd0,command};io_strobe=1;
        @(negedge chip_clk); io_strobe=0;
        @(negedge chip_clk); io_din=payload;io_strobe=1;
        @(negedge chip_clk); io_strobe=0;
        repeat(2) @(negedge chip_clk);
        io_uio=0;
        repeat(8) @(negedge chip_clk);
    endtask
    task automatic presence;
        @(negedge chip_clk);io_fpga=1;io_din=16'h1033;io_strobe=1;
        @(negedge chip_clk);io_strobe=0;
        repeat(16)@(negedge chip_clk);
        if(io_wait)$fatal(1,"Native floppy status did not complete");
        io_fpga=0;
        ide_address=6;ide_wdata=16'h0399;ide_write=1;
        @(negedge chip_clk);ide_write=0;
        repeat(8)@(negedge chip_clk);
        if(dut.chipset.PAULA1.pf1.disk_present!==4'h3 || dut.chipset.PAULA1.pf1.disk_writable!==4'h3)
            $fatal(1,"Mounted floppy state ignored while guest reset held");
        if(dut.chipset.GAYLE1.ide0.present!==2'h3)$fatal(1,"Mounted IDE state ignored under reset");
        guest_reset=0;
        repeat(32)@(negedge chip_clk);
        if(dut.chipset.PAULA1.pf1.disk_present!==4'h3 || dut.chipset.GAYLE1.ide0.present!==2'h3)
            $fatal(1,"Guest reset discarded mounted media state");
        guest_reset=1;
    endtask

    initial begin
        repeat(8) @(negedge clk);
        reset=0;
        // Portable RAM: write-first, other-port read, enable hold and CS mask.
        ra=3;da=8'ha5;wa=1;
        @(negedge clk);
        if(qa!==8'ha5) $fatal(1,"RAM port A is not write-first");
        wa=0;rb=3;
        @(negedge clk);
        if(qb!==8'ha5) $fatal(1,"RAM port B read mismatch");
        ea=0;ra=4;
        @(negedge clk);
        if(qa!==8'ha5) $fatal(1,"RAM enable did not hold output");
        csa=0;#1;
        if(qa!==8'hff) $fatal(1,"RAM chip-select mask mismatch");

        repeat(32) @(negedge chip_clk);
        if(cpucfg!==0 || memcfg!==7'h33) $fatal(1,"Unsafe power-on machine configuration");
        if(cpu_reset_n!==0) $fatal(1,"Guest reset released during configuration");
        presence();
        uio_config(8'hf3,16'h0008);
        uio_config(8'hf5,16'h0033);
        uio_config(8'hf7,16'h0004);
        uio_config(8'hf8,16'h0007);
        uio_config(8'hf4,16'h001f);
        if(cpucfg!==5'h03 || memcfg!==7'h33) $fatal(1,"Accelerated ECS configuration failed cpu=%h mem=%h t_cpu=%h reset=%b",cpucfg,memcfg,dut.chipset.USERIO1.t_cpu_config,dut.chipset.reset);
        if(dut.chipset.ide_fast!==0) $fatal(1,"Gayle unavailable in accelerated mode");
        if(dut.cpu.ac_toccata!==0 || dut.cpu.ac_a2065!==0) $fatal(1,"Phantom expansion card advertised");
        uio_config(8'hf4,16'h0000);
        if(cpucfg!==0) $fatal(1,"Stock 68000 mode unavailable");

        @(posedge clk);#1;run_enable=0;
        repeat(2) @(negedge clk);
        before_pause=chip_edges;
        repeat(100) @(negedge clk);
        if(chip_edges!=before_pause) $fatal(1,"Machine timeline advanced during memory hold");
        @(posedge clk);#1;run_enable=1;
        repeat(100) @(negedge clk);
        if(chip_edges<=before_pause+20) $fatal(1,"Machine clock did not resume at divide-by-four");
        if(chip_edges!=tick_edges) $fatal(1,"Root memory tick does not match native chip edges: %d/%d",chip_edges,tick_edges);
        $display("PASS Amiga portable RAM, stock/accelerated configuration and timeline pause/resume");
        $finish;
    end
    initial begin #1000000;$fatal(1,"Amiga source smoke timeout");end
endmodule
