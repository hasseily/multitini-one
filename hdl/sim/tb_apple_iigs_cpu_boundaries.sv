// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Independent directed expectations follow WDC W65C816S datasheet sections
// 7.1 and 7.2.2; they intentionally distinguish 44 old SingleStep goldens.
module tb_apple_iigs_cpu_boundaries;
    reg clk=0,reset_n=0;
    always #5 clk=~clk;
    reg [7:0] memory[0:16777215];
    wire [7:0] data_out;
    wire [23:0] address;
    wire we_n,vda;
    integer cursor;
    P65C816 cpu(.CLK(clk),.RST_N(reset_n),.CE(1'b1),.RDY_IN(1'b1),.NMI_N(1'b1),.IRQ_N(1'b1),.ABORT_N(1'b1),
        .D_IN(memory[address]),.D_OUT(data_out),.A_OUT(address),.WE(we_n),.RDY_OUT(),.VPA(),.VDA(vda),.MLB(),.VPB(),.I_FLAG());
    always @(posedge clk)if(reset_n && !we_n && vda)memory[address]<=data_out;
    task emit(input reg [7:0] value);begin memory[cursor]=value;cursor=cursor+1;end endtask
    task start;
        begin
            memory[24'h00FFFC]=0;memory[24'h00FFFD]=8'h80;
            repeat(5)@(negedge clk);reset_n=1;
        end
    endtask
    task await_stop;
        reg done;
        begin
            done=0;
            for(integer cycles=0;cycles<300;cycles=cycles+1)begin
                @(negedge clk);
                if(cpu.STPExec)begin done=1;cycles=300;end
            end
            if(!done)$fatal(1,"Boundary program failed to stop");
        end
    endtask
    initial begin
        for(integer index=0;index<16777216;index=index+1)memory[index]=0;
        cursor=16'h8000;
        emit(8'h78); // SEI
        emit(8'h18);emit(8'hFB); // CLC,XCE: native
        emit(8'hC2);emit(8'h30); // REP: 16-bit A/X
        emit(8'hA9);emit(0);emit(8'hF4);emit(8'h5B); // D=$F400
        emit(8'hE2);emit(8'h20); // 8-bit A
        emit(8'hA9);emit(8'h66);emit(8'h48);emit(8'hAB); // DBR=$66
        emit(8'hC2);emit(8'h20); // 16-bit A
        emit(8'hA9);emit(8'h09);emit(8'h31); // A=$3109
        emit(8'hA2);emit(8'h4F);emit(0); // X=$004F
        emit(8'hE2);emit(8'h10); // 8-bit index
        emit(8'h38);emit(8'hFB); // SEC,XCE: emulation, carry now0
        emit(8'hE1);emit(8'hB0); // SBC ($B0,X): pointer wraps F4FF -> F400
        emit(8'h8F);emit(0);emit(2);emit(0); // STA $000200
        emit(8'hDB);
        memory[24'h00F4FF]=8'h2F;memory[24'h00F400]=8'h3E;memory[24'h00F500]=8'h7A;
        memory[24'h663E2F]=8'hB3;memory[24'h667A2F]=8'h22;
        start();await_stop();
        if(memory[24'h000200]!==8'h55 || cpu.P[0]!==0)$fatal(1,"Emulation indirect pointer escaped direct page");
        @(negedge clk);reset_n=0;cursor=16'h8000;
        emit(8'h78);emit(8'hA2);emit(0);emit(8'h9A); // SEI,LDX0,TXS: S=$0100
        emit(8'hFC);emit(0);emit(8'h90);emit(8'hDB); // JSR($9000,X)
        memory[24'h009000]=0;memory[24'h009001]=8'h91;memory[24'h009100]=8'hDB;
        memory[24'h0001FF]=0;memory[24'h0000FF]=0;
        start();await_stop();
        $display("JSR boundary data S=%x [100]=%x [0ff]=%x [1ff]=%x PC=%x",cpu.SP,memory[24'h000100],memory[24'h0000FF],memory[24'h0001FF],cpu.PC);
        if(memory[24'h000100]!==8'h80 || memory[24'h0000FF]!==8'h06 || memory[24'h0001FF]!==0 || cpu.SP!==16'h01FE)
            $fatal(1,"JSR(a,X) failed documented extended stack crossing");
        $display("PASS: WDC emulation indirect-pointer page boundary and JSR(a,X) extended stack boundary");
        $finish;
    end
endmodule
