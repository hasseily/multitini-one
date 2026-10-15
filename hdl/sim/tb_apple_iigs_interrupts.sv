// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_interrupts;
    reg clk=0,reset_n=0,irq_n=1,nmi_n=1;
    always #5 clk=~clk;
    reg [7:0] memory[0:65535];
    wire [7:0] data_out;
    wire [23:0] address;
    wire we_n,vpa,vda;
    P65C816 cpu(.CLK(clk),.RST_N(reset_n),.CE(1'b1),.RDY_IN(1'b1),.NMI_N(nmi_n),.IRQ_N(irq_n),.ABORT_N(1'b1),
        .D_IN(memory[address[15:0]]),.D_OUT(data_out),.A_OUT(address),.WE(we_n),.RDY_OUT(),.VPA(vpa),.VDA(vda),.MLB(),.VPB(),.I_FLAG());
    always @(posedge clk) if(reset_n && !we_n && vda) memory[address[15:0]]<=data_out;
    task prepare(input bit masked,input integer target,input reg [7:0] marker);
        begin
            @(negedge clk);reset_n=0;irq_n=1;nmi_n=1;
            for(integer i=0;i<65536;i=i+1)memory[i]=8'hEA;
            memory[16'hFFFC]=8'h00;memory[16'hFFFD]=8'h80;
            memory[16'hFFFA]=8'h00;memory[16'hFFFB]=8'h90;
            memory[16'hFFFE]=8'h00;memory[16'hFFFF]=8'h90;
            // SEI/CLI; WAI; LDA #$77; STA $0200; STP.
            memory[16'h8000]=masked ? 8'h78 : 8'h58;memory[16'h8001]=8'hCB;
            memory[16'h8002]=8'hA9;memory[16'h8003]=8'h77;
            memory[16'h8004]=8'h8D;memory[16'h8005]=8'h00;memory[16'h8006]=8'h02;memory[16'h8007]=8'hDB;
            // Interrupt handler stores an independent marker, then RTI.
            memory[16'h9000]=8'hA9;memory[16'h9001]=marker;
            memory[16'h9002]=8'h8D;memory[16'h9003]=target;memory[16'h9004]=8'h02;memory[16'h9005]=8'h40;
            memory[16'h0200]=0;memory[16'h0201]=0;memory[16'h0202]=0;
            repeat(5)@(negedge clk);reset_n=1;
            for(integer cycles=0;cycles<100;cycles=cycles+1) begin
                @(negedge clk);
                if(cpu.WAIExec)cycles=100;
            end
            if(!cpu.WAIExec)$fatal(1,"WAI never entered");
            repeat(20) begin
                @(negedge clk);
                if(!cpu.WAIExec || memory[16'h0200]!=0)$fatal(1,"WAI woke with IRQ/NMI inactive");
            end
        end
    endtask
    task finish_program(input integer target,input reg [7:0] marker);
        reg complete;
        reg [23:0] held;
        begin
            complete=0;
            for(integer cycles=0;cycles<200;cycles=cycles+1) begin
                @(negedge clk);
                if(address[15:8]==8'h90) begin irq_n=1;nmi_n=1;end
                if(cpu.STPExec)begin complete=1;cycles=200;end
            end
            if(!complete || memory[16'h0200]!==8'h77 || memory[target]!==marker)
                $fatal(1,"Interrupt resume/RTI failed target=%x value=%x",target,memory[target]);
            held=address;irq_n=0;nmi_n=0;
            repeat(20)begin
                @(negedge clk);
                if(!cpu.STPExec || address!==held)$fatal(1,"STP responded to IRQ/NMI");
            end
        end
    endtask
    initial begin
        prepare(1,1,8'h55);irq_n=0;
        finish_program(16'h0201,0); // masked IRQ wakes WAI without vectoring
        prepare(0,1,8'h55);irq_n=0;
        finish_program(16'h0201,8'h55);
        prepare(1,2,8'h66);nmi_n=0;
        finish_program(16'h0202,8'h66);
        @(negedge clk);reset_n=0;
        @(posedge clk);#1;
        if(cpu.STPExec || cpu.WAIExec)$fatal(1,"Reset failed to clear STP/WAI");
        $display("PASS: 65C816 WAI masked IRQ wake, IRQ/NMI entry and RTI, STP interrupt immunity and reset");
        $finish;
    end
    initial begin #100000;$fatal(1,"Interrupt test timeout");end
endmodule
