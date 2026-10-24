// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_uci;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,block_reset=1,bus_valid=0,bus_write=0,op_valid=0,ff00_write=0;
    reg [2:0] bus_addr=0;reg [7:0] bus_wdata=0;reg [31:0] op=0;
    wire [7:0] rdata,ram_wdata;wire [10:0] ram_addr;wire ram_we,freeze,irq;
    wire [31:0] status,status2;reg [7:0] mem[0:2047];reg [7:0] ram_rdata;
    integer checks=0;
    reg [31:0] prior_reply_op;
    multitini_c64_uci dut(.clk(clk),.hard_reset(reset),.block_reset(block_reset),
        .enabled(1'b1),.suspended(1'b0),.bus_valid(bus_valid),.bus_write(bus_write),
        .ff00_write(ff00_write),
        .bus_addr(bus_addr),.bus_wdata(bus_wdata),.rdata(rdata),.bus_id(5'd31),
        .freeze_allowed(1'b1),.irq_allowed(1'b1),.op_data(op),.op_valid(op_valid),
        .op_ready(),.status(status),.status2(status2),.ram_addr(ram_addr),
        .ram_we(ram_we),.ram_wdata(ram_wdata),.ram_rdata(ram_rdata),
        .ram_read_done(4'd0),.freeze(freeze),.irq(irq));
    always @(posedge clk)begin
        if(ram_we)mem[ram_addr]<=ram_wdata;
        ram_rdata<=mem[ram_addr];
    end
    task check(input bit condition,input string message);
        begin checks=checks+1;if(!condition)$fatal(1,"UCI %s status=%h status2=%h",message,status,status2);end
    endtask
    task tick(input integer n);repeat(n)begin @(posedge clk);#1;end endtask
    task put(input [2:0] a,input [7:0] d);
        begin @(negedge clk);bus_addr=a;bus_wdata=d;bus_write=1;bus_valid=1;
            tick(1);@(negedge clk);bus_valid=0;bus_write=0;tick(4);end
    endtask
    task get(input [2:0] a,input [7:0] expected);
        begin @(negedge clk);bus_addr=a;bus_write=0;#1;check(rdata===expected,"read byte");
            bus_valid=1;tick(1);@(negedge clk);bus_valid=0;tick(6);end
    endtask
    task host(input [3:0] code,input [9:0] bytes,input [7:0] textlen);
        begin @(negedge clk);op={status[24:22],status[21:18],1'b0,textlen,2'b0,bytes,code};
            op_valid=1;tick(1);@(negedge clk);op_valid=0;tick(16);end
    endtask
    initial begin
        tick(3);reset=0;block_reset=0;tick(3);
        get(3,31);get(5,8'hc9);get(6,0);get(7,0);
        put(5,1);put(5,4);put(5,3);put(5,0);put(4,8'ha1);
        check(status[1:0]==1 && status[17:8]==4 && status2[30] && freeze,"PUSH latches exact command and freeze");
        check(mem[0]==1 && mem[3]==0,"command bytes in BRAM");
        put(5,8'hff);check(status2[22] && mem[0]==1,"busy writes dropped");
        put(4,8'h81);check(status[4] && freeze,"rejected PUSH preserves existing request");
        mem['h380]=8'h11;mem['h381]=8'h22;mem['h700]=8'h30;mem['h701]=8'h4b;
        host(5,2,2);check(status[1:0]==3 && !freeze && irq,"MORE publishes queues and IRQ");
        get(5,8'h49);get(6,8'h11);
        check(!irq,"response read acknowledges IRQ");get(5,8'hc9);
        get(7,8'h30);get(6,8'h22);get(7,8'h4b);get(6,0);
        check(status2[9:0]==2 && status2[18:10]==2,"consumed counters independent");
        put(4,2);check(status[1:0]==1 && status[3] && !irq,"MORE DATA_ACC waits next block");
        host(2,0,0);check(!status[3],"NEXT ack");
        mem['h380]=8'h33;host(4,1,0);get(6,8'h33);put(4,2);
        check(status[1:0]==0,"LAST DATA_ACC returns idle");
        put(5,1);put(4,8'h41);
        check(!freeze && status[6],"TRIGGER arms without freezing immediately");
        @(negedge clk);ff00_write=1;tick(1);@(negedge clk);ff00_write=0;tick(1);
        check(freeze && !status[6],"accepted FF00 consumes trigger and freezes");
        host(4,0,0);check(!freeze && !status[6],"reply releases deferred freeze");put(4,2);
        @(negedge clk);ff00_write=1;tick(1);@(negedge clk);ff00_write=0;tick(1);
        check(!freeze,"FF00 without armed trigger cannot freeze");
        put(5,1);put(4,8'h41);host(4,0,0);put(4,2);
        @(negedge clk);ff00_write=1;tick(1);@(negedge clk);ff00_write=0;tick(1);
        check(!freeze && !status[6],"reply cancels trigger before FF00");
        // A host may not observe the completed LAST until the next PUSH.
        // Preserve the exact partially consumed response throughout this gap.
        put(5,1);put(5,4);put(4,1);
        mem['h380]=8'h41;mem['h381]=8'h42;mem['h700]=8'h30;
        host(4,4,2);get(6,8'h41);get(6,8'h42);get(7,8'h30);put(4,2);
        check(status2[9:0]==2 && status2[18:10]==1,"LAST ack retains partial consumed counts");
        prior_reply_op={status[24:22],status[21:18],1'b0,8'd0,2'd0,10'd1,4'd4};
        put(5,3);put(5,1);
        check(status2[9:0]==2 && status2[18:10]==1,"new command fill retains prior consumed counts");
        put(4,1);
        check(status[1:0]==1 && status2[9:0]==2 && status2[18:10]==1,"new PUSH retains prior consumed counts for ARM cleanup");
        @(negedge clk);op=prior_reply_op;op_valid=1;tick(1);
        @(negedge clk);op_valid=0;tick(2);
        check(status2[21] && status2[9:0]==2,"old sequence reply cannot erase consumed count");
        mem['h380]=8'h43;host(4,1,0);
        check(status2[9:0]==0 && status2[18:10]==0,"next reply publication starts fresh consumed counts");
        get(6,8'h43);put(4,2);
        // More than eight idle ABORT writes must remain visible to a slow host.
        repeat(20)put(4,4);
        check(status[29],"idle ABORT is sticky, never wraps away");
        host(6,0,0);check(!status[29],"explicit idle ABORT acknowledgment");
        put(5,1);put(4,1);put(4,4);check(status[2],"busy ABORT retained");
        host(4,1,0);check(status2[21] && status[2],"late reply rejected during abort");
        host(3,0,0);check(status[1:0]==0 && !status[2],"ABORT cleanup ack");
        for(integer i=0;i<897;i=i+1)put(5,i[7:0]);
        check(status[17:8]==896 && status2[23],"command overflow bounded");
        put(4,4);host(6,0,0);put(5,1);put(4,1);
        @(negedge clk);op={status[24:22],status[21:18],1'b0,8'd0,2'd0,10'd1,4'd4};
        block_reset=1;tick(2);block_reset=0;op_valid=1;tick(1);op_valid=0;tick(2);
        check(status[1:0]==0 && status2[21],"stale epoch cannot publish after guest reset");
        $display("PASS C64 UCI endpoint %0d checks",checks);$finish;
    end
    initial begin #2000000;$fatal(1,"UCI timeout");end
endmodule
