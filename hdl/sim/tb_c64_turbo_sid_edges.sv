// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Literal expected engine samples from the reviewed edge-stage contract.
// Controls are checked before each native edge; real oscillator/envelope
// behavior is checked after that edge. No DUT state is forced or initialized.
module tb_c64_turbo_sid_edges;
    reg clk=0, reset=1, ce_1m=0; always #5 clk=~clk;
    reg [1:0] cs=0,fio_cs=0;
    reg we=0,fio_we=0;
    reg [4:0] addr=0,fio_addr=5'h12;
    reg [7:0] data_in=0,fio_wdata=0;
    integer checks=0,cases=0,clocks_since_ce=0;
    reg old_gate;
    sid_top #(.DUAL(1),.FAST_PORT(1),.EDGE_STAGE(1)) dut(
        .clk(clk),.reset(reset),.ce_1m(ce_1m),.cs(cs),.we(we),.addr(addr),.data_in(data_in),.data_out(),
        .fc_offset_l(13'd0),.fc_offset_r(13'd0),.pot_x_l(8'h12),.pot_y_l(8'h34),.pot_x_r(8'h56),.pot_y_r(8'h78),
        .ext_in_l(18'd0),.ext_in_r(18'd0),.audio_l(),.audio_r(),.filter_en(2'b11),.mode(2'b00),.cfg(4'd0),
        .ld_clk(clk),.ld_addr(12'd0),.ld_data(16'd0),.ld_wr(1'b0),
        .fio_cs(fio_cs),.fio_we(fio_we),.fio_latch(1'b0),.fio_latch_src(2'b00),.fio_addr(fio_addr),.fio_wdata(fio_wdata),
        .fast_rd_cs(2'b00),.fast_rd_addr(5'd0),.fast_rd_data());
    task step;
        @(posedge clk);#1;clocks_since_ce=clocks_since_ce+1;@(negedge clk);#1;
    endtask
    task native(input [4:0] a,input [7:0] d);
        cs=1;we=1;addr=a;data_in=d;step();cs=0;we=0;
    endtask
    task start_case(input [7:0] initial_control);
        reset=1;ce_1m=1;cs=0;fio_cs=0;fio_we=0;repeat(2)step();
        reset=0;ce_1m=0;old_gate=0;native(5'h0e,8'h31);native(5'h0f,8'h24);native(5'h12,initial_control);
        clocks_since_ce=32;cases=cases+1;
    endtask
    task write_fast(input [7:0] d);
        fio_cs=1;fio_we=1;fio_wdata=d;step();fio_cs=0;fio_we=0;
    endtask
    task sample(input [7:0] expected,input bit simultaneous,input [7:0] d);
        while(clocks_since_ce<32)step();
        ce_1m=1;fio_cs=simultaneous?1:0;fio_we=simultaneous;fio_wdata=d;
        if(dut.Voice_3_Control[0] !== expected)
            $fatal(1,"edge sample case=%0d check=%0d expected=%02x got=%02x",cases,checks,expected,dut.Voice_3_Control[0]);
        step();
        if(expected[3] && dut.chip[0].v3.oscillator !== 0) $fatal(1,"test pulse did not reset actual oscillator");
        if(!old_gate && expected[0] && dut.chip[0].v3.adsr.state !== 1) $fatal(1,"gate rise did not enter ATTACK");
        if(old_gate && !expected[0] && dut.chip[0].v3.adsr.state !== 0) $fatal(1,"gate fall did not enter RELEASE");
        old_gate=expected[0];checks=checks+1;ce_1m=0;fio_cs=0;fio_we=0;clocks_since_ce=0;
    endtask
    task ce(input [7:0] expected);sample(expected,0,0);endtask
    initial begin
        @(negedge clk);#1;
        start_case('h41);ce('h41);write_fast('h40);write_fast('h41);ce('h40);ce('h41);ce('h41);
        start_case('h40);ce('h40);write_fast('h41);write_fast('h40);ce('h41);ce('h40);ce('h40);
        start_case('h41);ce('h41);write_fast('h49);write_fast('h41);ce('h49);ce('h41);ce('h41);
        start_case('h40);ce('h40);write_fast('h41);write_fast('h40);write_fast('h21);ce('h41);ce('h21);ce('h21);
        start_case('h41);ce('h41);write_fast('h40);write_fast('h41);ce('h40);write_fast('h40);ce('h41);ce('h40);ce('h40);
        start_case('h41);ce('h41);write_fast('h40);write_fast('h41);sample('h40,1,'h40);ce('h41);ce('h40);ce('h40);
        start_case('h41);ce('h41);write_fast('h40);write_fast('h41);ce('h40);ce('h41);write_fast('h40);write_fast('h41);ce('h40);ce('h41);ce('h41);
        start_case('h41);ce('h41);write_fast('h40);write_fast('h41);ce('h40);write_fast('h40);write_fast('h41);ce('h41);ce('h41);ce('h41);
        start_case('h41);ce('h41);write_fast('h49);write_fast('h41);ce('h49);write_fast('h49);write_fast('h41);ce('h41);ce('h41);ce('h41);
        start_case('h41);ce('h41);write_fast('h40);write_fast('h41);sample('h40,1,'h40);write_fast('h41);ce('h41);ce('h41);ce('h41);
        start_case('h41);ce('h41);write_fast('h40);write_fast('h41);ce('h40);write_fast('h40);ce('h41);write_fast('h41);write_fast('h40);ce('h40);ce('h40);ce('h40);
        start_case('h41);ce('h41);write_fast('h40);write_fast('h41);native('h12,'h40);ce('h40);ce('h40);ce('h40);
        start_case('h41);ce('h41);write_fast('h40);write_fast('h41);reset=1;step();reset=0;old_gate=0;ce('h00);ce('h00);ce('h00);
        start_case('h41);ce('h41);write_fast('h40);repeat(100)step();write_fast('h41);ce('h40);ce('h41);ce('h41);
        $display("PASS SID edges cases=%0d engine_samples=%0d",cases,checks);$finish;
    end
    initial begin #1000000;$fatal(1,"SID edges timeout");end
endmodule
