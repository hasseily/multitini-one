// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_cpu_stall;
    reg clk=0,reset_n=0,rdy=1;
    always #5 clk=~clk;
    reg [7:0] memory[0:65535];
    wire [7:0] data_out;
    wire [23:0] address;
    wire we_n,vpa,vda,mlb,vpb;
    P65C816 cpu(.CLK(clk),.RST_N(reset_n),.CE(1'b1),.RDY_IN(rdy),.NMI_N(1'b1),.IRQ_N(1'b1),.ABORT_N(1'b1),
        .D_IN(memory[address[15:0]]),.D_OUT(data_out),.A_OUT(address),.WE(we_n),.RDY_OUT(),.VPA(vpa),.VDA(vda),.MLB(mlb),.VPB(vpb),.I_FLAG());
    integer writes=0;
    reg stalled_read=0,stalled_write=0;
    reg completed=0;
    reg [23:0] held_address;
    reg [7:0] held_data;
    reg held_we;
    reg [3:0] held_status;
    always @(posedge clk) if(reset_n && rdy && !we_n && vda) begin
        memory[address[15:0]]<=data_out;
        if(address==24'h000200) writes=writes+1;
    end
    task hold_cycle;
        begin
            rdy=0; held_address=address;held_data=data_out;held_we=we_n;held_status={vpa,vda,mlb,vpb};
            repeat(8) begin
                @(posedge clk); #1;
                if(address!==held_address || data_out!==held_data || we_n!==held_we || {vpa,vda,mlb,vpb}!==held_status)
                    $fatal(1,"CPU changed complete bus cycle while RDY low");
            end
            @(negedge clk); rdy=1;
        end
    endtask
    initial begin
        for(integer i=0;i<65536;i=i+1) memory[i]=8'hEA;
        memory[16'hFFFC]=8'h00;memory[16'hFFFD]=8'h80;
        // Original tiny program: LDA #$A5; STA $0200; LDA $0200;
        // CMP #$A5; BNE fail; STP. No Apple firmware/content involved.
        memory[16'h8000]=8'hA9;memory[16'h8001]=8'hA5;
        memory[16'h8002]=8'h8D;memory[16'h8003]=8'h00;memory[16'h8004]=8'h02;
        memory[16'h8005]=8'hAD;memory[16'h8006]=8'h00;memory[16'h8007]=8'h02;
        memory[16'h8008]=8'hC9;memory[16'h8009]=8'hA5;
        memory[16'h800A]=8'hD0;memory[16'h800B]=8'h04;
        memory[16'h800C]=8'hDB; // STP on success
        memory[16'h8010]=8'hA9;memory[16'h8011]=8'hFF;
        memory[16'h8012]=8'h8D;memory[16'h8013]=8'h01;memory[16'h8014]=8'h02;memory[16'h8015]=8'hDB;
        memory[16'h0201]=0;
        repeat(5) @(negedge clk); reset_n=1;
        for(integer cycles=0;cycles<300;cycles=cycles+1) begin
            @(negedge clk);
            if(!stalled_write && !we_n && vda && address==24'h000200) begin
                stalled_write=1;hold_cycle();
            end
            if(!stalled_read && we_n && vda && address==24'h000200) begin
                stalled_read=1;hold_cycle();
            end
            if(cpu.STPExec) begin
                if(!stalled_read || !stalled_write || writes!=1 || memory[16'h0200]!==8'hA5 || memory[16'h0201]!==0)
                    $fatal(1,"CPU stalled program outcome failed: read=%d write=%d writes=%d",stalled_read,stalled_write,writes);
                $display("PASS: IIgs 65C816 held read/write/address/data/status through RDY stalls and resumed exactly once");
                completed=1;
                cycles=300;
            end
        end
        if(!completed) $fatal(1,"CPU failed to complete original program");
        $finish;
    end
endmodule
