// SPDX-License-Identifier: GPL-3.0-only
// Real production NMOS CPU plus the island NMI delivery helper. The memory
// is an original synthetic ROM/RAM fixture; no system or demo bytes are used.
`timescale 1ns/1ps
module tb_c64_island_interrupts;
    reg clk=0,reset=1,turbo=0,t65_enable=0,live_nmi_n=1,exact_nmi_n=1,irq_n=1;
    always #5 clk=~clk;
    wire t65_nmi_n,exact_switch_safe,pending_rearm,carry_active,used_hold,nmi_ack;
    wire [15:0] addr;
    wire [7:0] data_out,port_out;
    wire write;
    reg [7:0] memory[0:65535];
    wire [7:0] data_in=memory[addr];
    integer checks=0,ticks=0,nmi_writes=0,irq_writes=0,i;
    multitini_c64_nmi_delivery delivery(.*);
    cpu_6510 cpu(.clk(clk),.enable(t65_enable),.bus_start(1'b0),.reset(reset),
        .nmi_n(t65_nmi_n),.nmi_ack(nmi_ack),.irq_n(irq_n),.rdy(1'b1),.port_tick(1'b0),
        .di(data_in),.\do (data_out),.addr(addr),.we(write),.diIO(8'h17),.doIO(port_out));
    always @(posedge clk) begin
        ticks<=ticks+1;
        if(!reset && t65_enable && write)begin
            memory[addr]<=data_out;
            if(addr==16'h0200)nmi_writes<=nmi_writes+1;
            if(addr==16'h0201)irq_writes<=irq_writes+1;
        end
        if(ticks>10000)$fatal(1,"ISLAND_INTERRUPTS timeout addr=%x NMI=%d IRQ=%d",addr,memory[16'h200],memory[16'h201]);
    end
    task automatic step;@(posedge clk);#0.01;endtask
    task automatic check(input bit ok,input string why);
        checks++;if(!ok)$fatal(1,"ISLAND_INTERRUPTS FAIL %s",why);
    endtask
    task automatic loop_ready;
        wait(addr==16'h0801 && !write);repeat(16)step();
    endtask
    task automatic expect_nmi(input integer value);
        wait(memory[16'h200]==value);repeat(40)step();
        check(memory[16'h200]==value,"NMI delivered exactly once");
        check(nmi_writes==value*2,"NMOS RMW dummy and final writes retained");
    endtask
    initial begin
        for(i=0;i<65536;i++)memory[i]=8'hea;
        memory[16'h200]=0;memory[16'h201]=0;
        memory[16'hfffc]=0;memory[16'hfffd]=8'h08;
        memory[16'hfffa]=0;memory[16'hfffb]=8'h10;
        memory[16'hfffe]=0;memory[16'hffff]=8'h20;
        memory[16'h0800]=8'h58; // CLI; NOP; JMP $0801
        memory[16'h0801]=8'hea;memory[16'h0802]=8'h4c;memory[16'h0803]=1;memory[16'h0804]=8'h08;
        memory[16'h1000]=8'hee;memory[16'h1001]=0;memory[16'h1002]=2;memory[16'h1003]=8'h40;
        memory[16'h2000]=8'hee;memory[16'h2001]=1;memory[16'h2002]=2;memory[16'h2003]=8'h40;
        repeat(10)step();@(negedge clk);reset=0;t65_enable=1;
        loop_ready();@(negedge clk);turbo=1;t65_enable=0;
        // Short fall from HIGH history while the CPU is halted.
        live_nmi_n=0;repeat(4)step();@(negedge clk);live_nmi_n=1;repeat(5)step();
        @(negedge clk);t65_enable=1;expect_nmi(1);
        // Establish a held LOW sample, service it, then pulse HIGH/LOW wholly
        // in a second halt. This is the original lost-edge counterexample.
        @(negedge clk);live_nmi_n=0;expect_nmi(2);
        @(negedge clk);t65_enable=0;live_nmi_n=1;repeat(3)step();
        @(negedge clk);live_nmi_n=0;repeat(3)step();
        @(negedge clk);t65_enable=1;step();check(pending_rearm,"LOW history first shows HIGH");
        // Forced exact on this boundary carries the pending fall, with no
        // additional turbo enable. B0 already reflects the held LOW line.
        @(negedge clk);turbo=0;exact_nmi_n=0;expect_nmi(3);
        check(!carry_active,"carry settled on matching exact B0");
        repeat(50)step();check(memory[16'h200]==3,"held LOW does not retrigger");
        @(negedge clk);exact_nmi_n=1;live_nmi_n=1;repeat(5)step();
        // IRQ remains a sampled level: a held request survives a CPU halt.
        @(negedge clk);turbo=1;t65_enable=0;irq_n=0;repeat(7)step();
        @(negedge clk);t65_enable=1;wait(addr==16'h2000);
        @(negedge clk);irq_n=1;wait(memory[16'h201]==1);repeat(50)step();
        check(memory[16'h201]==1 && irq_writes==2,"IRQ/RTI once with NMOS RMW writes");
        $display("ISLAND_INTERRUPTS PASS checks=%0d nmi=3 irq=1",checks);$finish;
    end
endmodule
