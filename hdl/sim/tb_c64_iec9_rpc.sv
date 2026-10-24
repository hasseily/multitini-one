// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// A clock-step interface for a real-ROM software CPU driving the production
// IEC endpoint. The Python side supplies only external released-high lines.
module tb_c64_iec9_rpc;
    parameter integer CLOCK_HZ=31524725;
    reg clk=0,hard_reset=1,run=0;
    reg atn=1,external_clk=1,external_data=1;
    wire data_o,clk_o,busy,event_valid,reply_ready;
    wire [31:0] event_data;
    wire [15:0] token;
    reg [31:0] reply_data=0;
    reg reply_valid=0;
    integer count,scan,steps,a,c,d,rv,r;
    reg [31:0] word;
    reg [2047:0] line;
    multitini_c64_iec9 #(.CLOCK_HZ(CLOCK_HZ)) dut(
        .clk(clk),.hard_reset(hard_reset),.run(run),.quiesce(1'b0),.quiescent(),
        .atn(atn),.bus_clk(external_clk & clk_o),.bus_data(external_data & data_o),
        .data_o(data_o),.clk_o(clk_o),.event_data(event_data),
        .event_valid(event_valid),.event_ready(1'b1),.reply_data(reply_data),
        .reply_valid(reply_valid),.reply_ready(reply_ready),.busy(busy),.token(token)
    );
    always @(posedge clk) begin
        if(event_valid) $display("E %08x",event_data);
        if(reply_valid && reply_ready) reply_valid<=0;
    end
    task tick;
        begin #5;clk=1;#5;clk=0;end
    endtask
    initial begin
        repeat(4)tick();hard_reset=0;
        $display("READY");$fflush();
        forever begin
            scan=$fgets(line,32'h80000000);
            if(scan==0) $finish;
            scan=$sscanf(line,"%d %d %d %d %d %h %d",steps,a,c,d,rv,word,r);
            if(scan!=7) begin
                if($feof(32'h80000000)) $finish;
                $fatal(1,"invalid RPC command");
            end
            if(steps<0) $finish;
            if(reply_valid && rv) $fatal(1,"RPC overwrote a pending response");
            atn=a;external_clk=c;external_data=d;run=r;
            if(rv)begin reply_valid=1;reply_data=word;end
            for(count=0;count<steps;count=count+1) tick();
            $display("R %0d %0d %0d %0d %0d %02x %02x %0d",data_o,clk_o,token,reply_valid,dut.state,dut.shift,dut.command_primary,dut.state==dut.TX_ACK);$fflush();
        end
    end
endmodule
