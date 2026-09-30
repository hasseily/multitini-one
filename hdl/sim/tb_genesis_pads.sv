// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_genesis_pads;
    reg clk=0,reset=1;
    always #5 clk=~clk;
    reg [1:0] six_button=0;
    reg [11:0] buttons[0:1];
    reg [6:0] port_in[0:1],port_dir[0:1];
    wire [6:0] port_out[0:1];
    genvar p;
    generate for(p=0;p<2;p=p+1)begin:pads
        pad_io #(.CLOCK_MULTIPLIER(2)) pad_i(
            .clk(clk),.reset(reset),.MODE(six_button[p]),.SMS(1'b0),
            .P_RIGHT(buttons[p][0]),.P_LEFT(buttons[p][1]),.P_DOWN(buttons[p][2]),.P_UP(buttons[p][3]),
            .P_A(buttons[p][4]),.P_B(buttons[p][5]),.P_C(buttons[p][6]),.P_START(buttons[p][7]),
            .P_MODE(buttons[p][8]),.P_X(buttons[p][9]),.P_Y(buttons[p][10]),.P_Z(buttons[p][11]),
            .GUN_EN(1'b0),.GUN_TYPE(1'b0),.GUN_SENSOR(1'b0),
            .GUN_A(1'b0),.GUN_B(1'b0),.GUN_C(1'b0),.GUN_START(1'b0),
            .MOUSE_EN(1'b0),.MOUSE_FLIPY(1'b0),.MOUSE(25'd0),
            .port_in(port_in[p]),.port_dir(port_dir[p]),.port_out(port_out[p]));
    end endgenerate
    function automatic [5:0] normal(input integer player,input bit th);
        normal=th ? {~buttons[player][6],~buttons[player][5],~buttons[player][0],
                     ~buttons[player][1],~buttons[player][2],~buttons[player][3]} :
                    {~buttons[player][7],~buttons[player][4],2'b00,
                     ~buttons[player][2],~buttons[player][3]};
    endfunction
    function automatic [5:0] extra(input integer player);
        extra={~buttons[player][6],~buttons[player][5],~buttons[player][8],
               ~buttons[player][9],~buttons[player][10],~buttons[player][11]};
    endfunction
    task automatic select_th(input integer player,input bit th,input [5:0] expected);
        begin
            @(negedge clk);port_in[player][6]=th;
            repeat(10)@(negedge clk);
            if(port_out[player][5:0]!==expected)$fatal(1,"pad%0d TH%0d got%h expected%h",player,th,port_out[player][5:0],expected);
        end
    endtask
    task automatic extended_sequence(input integer player);
        begin
            select_th(player,0,normal(player,0));select_th(player,1,normal(player,1));
            select_th(player,0,normal(player,0));select_th(player,1,normal(player,1));
            select_th(player,0,{~buttons[player][7],~buttons[player][4],4'b0000});
            select_th(player,1,extra(player));
        end
    endtask
    initial begin
        buttons[0]=12'ha59;buttons[1]=12'h5a6;
        port_in[0]=7'h40;port_in[1]=7'h40;port_dir[0]=7'h3f;port_dir[1]=7'h3f;
        repeat(4)@(negedge clk);reset=0;repeat(10)@(negedge clk);
        for(integer player=0;player<2;player=player+1)begin
            for(integer pulse=0;pulse<5;pulse=pulse+1)begin
                select_th(player,0,normal(player,0));select_th(player,1,normal(player,1));
            end
        end
        six_button=1;extended_sequence(0);
        if(port_out[1][5:0]!==normal(1,1))$fatal(1,"player1 handshake changed player2");
        select_th(0,0,{~buttons[0][7],~buttons[0][4],4'b1111});
        select_th(0,1,normal(0,1));
        extended_sequence(0);
        // At107MHz the upstream53MHz pad timeout requires doubled counters.
        repeat(120000)@(negedge clk);
        if(port_out[0][5:0]!==extra(0))$fatal(1,"six-button timeout ran twice too fast");
        repeat(45000)@(negedge clk);
        if(port_out[0][5:0]!==normal(0,1))$fatal(1,"six-button timeout did not reset handshake");
        six_button=2;extended_sequence(1);
        if(port_out[0][5:0]!==normal(0,1))$fatal(1,"player2 handshake changed player1");
        // Releasing TH to its pull-up must respect the physical pad filter.
        six_button=0;repeat(10)@(negedge clk);
        select_th(0,0,normal(0,0));port_dir[0]=7'h7f;
        repeat(2000)@(negedge clk);
        if(port_out[0][5:0]!==normal(0,0))$fatal(1,"TH pull-up filter too short");
        repeat(1000)@(negedge clk);
        if(port_out[0][5:0]!==normal(0,1))$fatal(1,"TH pull-up filter did not release");
        reset=1;repeat(4)@(negedge clk);reset=0;buttons[0]=0;buttons[1]=0;
        port_dir[0]=7'h3f;port_in[0]=7'h40;port_in[1]=7'h40;
        repeat(10)@(negedge clk);
        if(port_out[0]!==7'h7f || port_out[1]!==7'h7f)$fatal(1,"reset/release left stuck buttons");
        $display("PASS Genesis pads: independent3/6-button TH protocol, extensions, timeout and release");
        $finish;
    end
    initial begin #3000000;$fatal(1,"Genesis pad timeout");end
endmodule
