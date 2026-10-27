// SPDX-License-Identifier: GPL-3.0-only
// Original replay fixture. The runner supplies explicitly selected Lorenz
// public-domain vectors and program bytes into an ignored build directory.
`timescale 1ns/1ps
module tb_c64_cia_timers #(parameter integer CPU_MODE=0,TIMER=0,ROWS=20832,NO_NMI=0,DIRECTED=0);
    reg clk=0,reset_n=0;
    always #5 clk=~clk;
    reg [4:0] phase=0;
    always @(posedge clk)if(!reset_n)phase<=0;else phase<=phase+1'b1;
    reg probe_p=0,probe_n=0,probe_cs_n=1,probe_rw=1;
    reg [3:0] probe_rs=0;reg [7:0] probe_data=0;
    wire p=CPU_MODE?phase==31:probe_p;
    wire n=CPU_MODE?phase==28:probe_n;
    wire [15:0] cpu_addr;wire [7:0] cpu_do,cpu_port;wire cpu_we,nmi_ack;
    wire selected=cpu_addr[15:8]==(CPU_MODE==2?8'hdd:8'hdc);
    wire cs_n=CPU_MODE?!selected:probe_cs_n;
    wire rw=CPU_MODE?!cpu_we:probe_rw;
    wire [3:0] rs=CPU_MODE?cpu_addr[3:0]:probe_rs;
    wire [7:0] data=CPU_MODE?cpu_do:probe_data;
    wire [7:0] q;wire irq_n;
    reg [7:0] memory[0:65535];
    wire [7:0] cpu_di=selected?q:memory[cpu_addr];
    reg [55:0] vectors[0:20831];
    integer errors=0,row=0,nmi_total=0,nmi_row=0,cycles=0;
    integer directed_checks=0;
    reg old_nmi_ack=0;
    reg [7:0] got_count,got_icr,got_cr;
    localparam [3:0] REG_LO=TIMER?6:4,REG_HI=TIMER?7:5,REG_CR=TIMER?15:14;
    localparam [15:0] HALT=CPU_MODE==2?16'h0974:16'h0969;
    mos6526 cia(.mode(1'b0),.clk(clk),.phi2_p(p),.phi2_n(n),.res_n(reset_n),
        .cs_n(cs_n),.rw(rw),.rs(rs),.db_in(data),.db_out(q),
        .pa_in(8'hff),.pb_in(8'hff),.flag_n(1'b1),.tod(1'b0),
        .sp_in(1'b1),.cnt_in(1'b1),.irq_n(irq_n));
    generate if(CPU_MODE!=0)begin:processor
        cpu_6510 cpu(.clk(clk),.enable(p),.bus_start(1'b0),.reset(!reset_n),
            .nmi_n(CPU_MODE==2&&!NO_NMI?irq_n:1'b1),.nmi_ack(nmi_ack),
            .irq_n(CPU_MODE==1?irq_n:1'b1),.rdy(1'b1),.port_tick(p),
            .di(cpu_di),.\do (cpu_do),.addr(cpu_addr),.we(cpu_we),
            .diIO(8'h17),.doIO(cpu_port));
        always @(posedge clk)begin
            if(!reset_n)old_nmi_ack<=0;
            else begin
                old_nmi_ack<=nmi_ack;
                if(nmi_ack&&!old_nmi_ack)begin nmi_total++;nmi_row++;end
                if(n&&cpu_we&&!selected)memory[cpu_addr]<=cpu_do;
            end
        end
    end else begin
        assign cpu_addr=0;assign cpu_do=0;assign cpu_we=0;assign nmi_ack=0;assign cpu_port=0;
    end endgenerate
    // Register access precedes the native timer tick by three core clocks,
    // matching the registered CPUC+1 and CPUF+1 enables of the machine.
    task automatic cyc(input bit enable_bus,input bit writing,input [3:0] address,input [7:0] value);
        probe_cs_n=!enable_bus;probe_rw=!writing;probe_rs=address;probe_data=value;
        for(integer tick=0;tick<32;tick++)begin
            probe_n=tick==28;probe_p=tick==31;
            @(posedge clk);#1;@(negedge clk);
        end
        probe_n=0;probe_p=0;
    endtask
    task automatic idle(input integer count);repeat(count)cyc(0,0,0,0);endtask
    task automatic wr(input [3:0] address,input [7:0] value);cyc(1,1,address,value);endtask
    task automatic check_directed(input bit ok,input string why);
        directed_checks++;
        if(!ok)begin errors++;$display("CIA_DIRECTED_MISMATCH %s",why);end
    endtask
    task automatic reset_device;
        @(negedge clk);reset_n=0;probe_cs_n=1;probe_p=0;probe_n=0;
        repeat(128)@(negedge clk);reset_n=1;idle(4);
    endtask
    task automatic cascade_and_reset;
        reset_device();
        wr(15,0);idle(3);wr(6,3);wr(7,0);wr(15,8'h51);idle(5);
        cyc(1,0,6,0);check_directed(q===3,"cascade initial B counter");
        wr(14,0);idle(3);wr(5,0);idle(3);wr(13,8'h81);idle(3);
        wr(4,6);idle(5);wr(14,8'h10);idle(3);cyc(1,0,13,0);idle(3);
        wr(14,8'h11);idle(7);wr(4,8'h14);idle(3);wr(14,0);
        idle(3);cyc(1,0,4,0);got_count=q;
        idle(3);cyc(1,0,13,0);got_icr=q;
        idle(3);cyc(1,0,14,0);got_cr=q;
        check_directed({got_count,got_icr,got_cr}===24'h108100,"cascade A result and interrupt");
        cyc(1,0,6,0);check_directed(q===2,"exactly one A underflow clocks B");
        // Reset while both timer reload pipelines have recently been active.
        wr(14,8'h10);idle(1);wr(15,8'h10);idle(1);reset_device();
        for(integer address=4;address<8;address++)begin
            cyc(1,0,address[3:0],0);check_directed(q===8'hff,"reset timer counter bytes");
        end
        cyc(1,0,14,0);check_directed(q===0,"reset CRA");
        cyc(1,0,15,0);check_directed(q===0,"reset CRB");
        cyc(1,0,13,0);check_directed(q===0,"reset ICR");
        $display("CIA_DIRECTED_RESULT checks=%0d",directed_checks);
    endtask
    task automatic result;
        if({got_count,got_icr,got_cr}!==vectors[row][23:0])begin
            $display("CIA_MISMATCH mode=%0d timer=%0d row=%0d init=%h got=%h expected=%h nmi=%0d",CPU_MODE,TIMER,row,vectors[row][55:24],{got_count,got_icr,got_cr},vectors[row][23:0],nmi_row);
            errors++;
        end
    endtask
    initial begin
        $readmemh("vectors.hex",vectors,0,ROWS-1);
        if(CPU_MODE!=0)begin
            for(integer a=0;a<65536;a++)memory[a]=8'hea;
            $readmemh("program.hex",memory);
            // Synthetic reset and NMI trampoline (SEI; JMP($0318)). The CIA2
            // one-instruction RTI handler remains at $08F7. No system ROM.
            memory[16'hfffc]=0;memory[16'hfffd]=2;
            memory[16'hfffa]=8'h43;memory[16'hfffb]=8'hfe;
            memory[16'hfe43]=8'h78;memory[16'hfe44]=8'h6c;
            memory[16'hfe45]=8'h18;memory[16'hfe46]=3;
        end
        for(row=0;row<ROWS;row++)begin
            @(negedge clk);reset_n=0;probe_cs_n=1;probe_p=0;probe_n=0;nmi_row=0;
            if(CPU_MODE==0)begin
                repeat(4)@(negedge clk);reset_n=1;idle(4);
                wr(REG_CR,0);idle(3);wr(REG_HI,0);idle(3);wr(13,TIMER?8'h82:8'h81);idle(3);
                wr(REG_LO,vectors[row][55:48]);idle(5);wr(REG_CR,8'h10);idle(3);cyc(1,0,13,0);idle(3);
                wr(REG_CR,vectors[row][39:32]);idle(7);wr(REG_LO,vectors[row][47:40]);
                idle(3);wr(REG_CR,vectors[row][31:24]);
                idle(3);cyc(1,0,REG_LO,0);got_count=q;
                idle(3);cyc(1,0,13,0);got_icr=q;
                idle(3);cyc(1,0,REG_CR,0);got_cr=q;
            end else begin
                memory[16'h08ed]=vectors[row][55:48];memory[16'h08ef]=vectors[row][47:40];
                memory[16'h021e]=vectors[row][39:32];memory[16'h0220]=vectors[row][31:24];
                memory[16'h08f1]=8'hcc;memory[16'h08f2]=8'hcc;memory[16'h08f3]=8'hcc;
                repeat(128)@(negedge clk);reset_n=1;cycles=0;
                while(!(cpu_addr==HALT&&!cpu_we)&&cycles<64000)begin @(negedge clk);cycles++;end
                if(cycles>=64000)$fatal(1,"CIA_CPU_TIMEOUT mode=%0d timer=%0d row=%0d pc=%h",CPU_MODE,TIMER,row,cpu_addr);
                got_count=memory[16'h08f1];got_icr=memory[16'h08f2];got_cr=memory[16'h08f3];
            end
            result();
        end
        if(DIRECTED&&CPU_MODE==0&&TIMER==0)cascade_and_reset();
        $display("CIA_RESULT mode=%0d timer=%0d rows=%0d errors=%0d nmi=%0d",CPU_MODE,TIMER,ROWS,errors,nmi_total);
        if(errors)$fatal(1,"CIA_VECTOR_FAILURE");
        $display("CIA_VECTORS_PASS");$finish;
    end
endmodule
