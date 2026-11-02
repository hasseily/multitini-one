// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_interrupts;
    reg clk=0,reset_n=0,irq_n=1,nmi_n=1,ce=1,rdy=1;
    always #5 clk=~clk;
    reg [7:0] memory[0:16777215];
    wire [7:0] data_out;
    wire [23:0] address;
    wire we_n,vpa,vda,vpb;
    P65C816 cpu(.CLK(clk),.RST_N(reset_n),.CE(ce),.RDY_IN(rdy),.NMI_N(nmi_n),.IRQ_N(irq_n),.ABORT_N(1'b1),
        .D_IN(memory[address]),.D_OUT(data_out),.A_OUT(address),.WE(we_n),.RDY_OUT(),.VPA(vpa),.VDA(vda),.MLB(),.VPB(vpb),.I_FLAG());
    always @(posedge clk) if(reset_n && ce && rdy && !we_n && vda) memory[address]<=data_out;

    // Exercise the CPU's CE/RDY contract independently of the machine bridge.
    // Interrupt writes and RTI pulls must retire once despite held bus cycles.
    bit stress_stalls=0,native_active=0;
    integer schedule_tick=0,ce_gaps=0,read_stalls=0,vector_stalls=0,stack_stalls=0;
    integer rti_read_stalls=0,rti_ce_gaps=0;
    always @(negedge clk)begin
        if(!reset_n || !stress_stalls)begin ce=1;rdy=1;schedule_tick=0;end
        else begin
            schedule_tick=schedule_tick+1;
            ce=schedule_tick%4!=1;
            rdy=!(we_n && (vpa || vda) && schedule_tick%7<2);
        end
    end
    always @(posedge clk)if(native_active)begin
        if(!ce)begin
            ce_gaps<=ce_gaps+1;
            if(cpu.IR==8'h40)rti_ce_gaps<=rti_ce_gaps+1;
        end
        if(ce && !rdy && we_n && (vpa || vda))begin
            read_stalls<=read_stalls+1;
            if(!vpb)vector_stalls<=vector_stalls+1;
            if(vda && address>=24'h0034e0 && address<=24'h003501)stack_stalls<=stack_stalls+1;
            if(cpu.IR==8'h40 && vda && address>=24'h0034fe && address<=24'h003501)
                rti_read_stalls<=rti_read_stalls+1;
        end
    end
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

    integer cursor,native_cases=0;
    task emit(input reg [7:0] value);begin memory[cursor]=value;cursor=cursor+1;end endtask
    task native_irq_rti(input reg [7:0] widths,input bit stalls,input bit save_owned);
        reg [7:0] expected_p;
        reg [15:0] expected_x,expected_y;
        bit entry_seen,resume_seen,stopped;
        integer polls;
        begin
            @(negedge clk);reset_n=0;irq_n=1;nmi_n=1;stress_stalls=0;native_active=0;
            for(integer i=0;i<65536;i=i+1)memory[i]=8'hea;
            memory[24'h00fffc]=0;memory[24'h00fffd]=8'h80;
            memory[24'h00ffee]=0;memory[24'h00ffef]=8'h90;
            for(integer i=16'h34e0;i<=16'h3501;i=i+1)memory[i]=8'hcc;
            // Native caller: full stack S=$3501 crosses a page in the IRQ
            // frame; D/DBR are nonzero, and execution moves to PBR=$23.
            cursor=24'h008000;
            emit(8'h78);emit(8'h18);emit(8'hfb); // SEI; CLC; XCE
            emit(8'hc2);emit(8'h30); // REP #$30: A/X16
            emit(8'ha9);emit(8'h01);emit(8'h35);emit(8'h1b); // LDA #$3501; TCS
            emit(8'ha9);emit(8'h68);emit(8'h24);emit(8'h5b); // LDA #$2468; TCD
            emit(8'he2);emit(8'h20);emit(8'ha9);emit(8'h5a);emit(8'h48);emit(8'hab); // DBR=$5A
            emit(8'hc2);emit(8'h20);
            emit(8'ha9);emit(8'h6b);emit(8'ha1); // A=$A16B
            emit(8'ha2);emit(8'h7c);emit(8'hb2); // X=$B27C
            emit(8'ha0);emit(8'h8d);emit(8'hc3); // Y=$C38D
            emit(8'h5c);emit(0);emit(8'h80);emit(8'h23); // JML $238000
            expected_p=8'h49|widths; // V,D,C set; N,Z,I clear; requested M/X
            expected_x=widths[4] ? 16'h007c:16'hb27c;
            expected_y=widths[4] ? 16'h008d:16'hc38d;
            cursor=24'h238000;
            emit(8'hc2);emit(8'hff);emit(8'he2);emit(expected_p); // exact native P
            emit(8'hcb);emit(8'hea);emit(8'hdb); // WAI; resume at $238005; STP
            cursor=24'h009000;
            // Hardware owns only PBR/PC/P in the four-byte native IRQ frame.
            // A/X/Y/DBR/D are software-owned. Force 16-bit save/pull widths;
            // RTI finally restores the caller's original M/X bits.
            emit(8'hc2);emit(8'h30);
            if(save_owned)begin emit(8'h48);emit(8'hda);emit(8'h5a);emit(8'h8b);emit(8'h0b);end
            emit(8'he2);emit(8'h20);emit(8'ha9);emit(8'h66);emit(8'h48);emit(8'hab); // DBR=$66
            emit(8'hc2);emit(8'h20);emit(8'ha9);emit(8'hef);emit(8'hbe);emit(8'h5b); // D=$BEEF
            emit(8'ha9);emit(8'h34);emit(8'h12);emit(8'ha2);emit(8'h78);emit(8'h56);
            emit(8'ha0);emit(8'hbc);emit(8'h9a);
            if(save_owned)begin emit(8'h2b);emit(8'hab);emit(8'h7a);emit(8'hfa);emit(8'h68);end
            emit(8'h40); // RTI
            repeat(5)@(negedge clk);reset_n=1;
            polls=0;
            while(!cpu.WAIExec && polls<1000)begin @(negedge clk);polls=polls+1;end
            if(!cpu.WAIExec || cpu.P!=={1'b0,expected_p} || cpu.SP!==16'h3501 ||
               cpu.PBR!==8'h23 || cpu.DBR!==8'h5a || cpu.D!==16'h2468 ||
               cpu.A!==16'ha16b || cpu.X!==expected_x || cpu.Y!==expected_y)
                $fatal(1,"Native caller setup differs P=%03x S=%04x K=%02x B=%02x D=%04x A=%04x X=%04x Y=%04x",cpu.P,cpu.SP,cpu.PBR,cpu.DBR,cpu.D,cpu.A,cpu.X,cpu.Y);
            ce_gaps=0;read_stalls=0;vector_stalls=0;stack_stalls=0;rti_read_stalls=0;rti_ce_gaps=0;
            entry_seen=0;resume_seen=0;stopped=0;native_active=1;stress_stalls=stalls;irq_n=0;
            for(integer cycles=0;cycles<4000;cycles=cycles+1)begin
                @(posedge clk);
                if(ce && rdy && cpu.STATE==0 && vpa && vda)begin
                    if(address==24'h009000)begin
                        if(entry_seen)$fatal(1,"Native IRQ unexpectedly entered twice");
                        entry_seen=1;
                        if(cpu.SP!==16'h34fd || cpu.P!=={1'b0,((expected_p|8'h04)&8'hf7)} ||
                           cpu.DBR!==8'h5a || cpu.D!==16'h2468 ||
                           memory[24'h003501]!==8'h23 || memory[24'h003500]!==8'h80 ||
                           memory[24'h0034ff]!==8'h05 || memory[24'h0034fe]!==expected_p)
                            $fatal(1,"Native IRQ frame/entry differs S=%04x P=%03x frame=%02x%02x%02x%02x B=%02x D=%04x",cpu.SP,cpu.P,memory[24'h003501],memory[24'h003500],memory[24'h0034ff],memory[24'h0034fe],cpu.DBR,cpu.D);
                    end
                    // IRQ entry may perform a discarded opcode fetch at the
                    // interrupted PC. Only a fetch after the ISR is a return.
                    if(entry_seen && address==24'h238005)begin
                        resume_seen=1;
                        if(!entry_seen || cpu.PBR!==8'h23 || cpu.PC!==16'h8005 ||
                           cpu.SP!==16'h3501 || cpu.P!=={1'b0,expected_p})
                            $fatal(1,"Native RTI hardware frame restore differs K:PC=%02x:%04x S=%04x P=%03x",cpu.PBR,cpu.PC,cpu.SP,cpu.P);
                        if(save_owned)begin
                            if(cpu.DBR!==8'h5a || cpu.D!==16'h2468 || cpu.A!==16'ha16b || cpu.X!==expected_x || cpu.Y!==expected_y)
                                $fatal(1,"Native ISR software save/restore differs B=%02x D=%04x A=%04x X=%04x Y=%04x",cpu.DBR,cpu.D,cpu.A,cpu.X,cpu.Y);
                        end else if(cpu.DBR!==8'h66 || cpu.D!==16'hbeef)
                            $fatal(1,"RTI incorrectly restored software-owned DBR/D");
                    end
                end
                #1;
                if(entry_seen)irq_n=1;
                if(cpu.STPExec)begin stopped=1;cycles=4000;end
            end
            @(negedge clk);native_active=0;stress_stalls=0;
            if(!entry_seen || !resume_seen || !stopped)$fatal(1,"Native IRQ/RTI did not resume and stop");
            if(stalls && (ce_gaps==0 || read_stalls==0 || vector_stalls==0 || stack_stalls==0 || rti_read_stalls==0 || rti_ce_gaps==0))
                $fatal(1,"Native stall coverage absent CE=%0d read=%0d vector=%0d stack=%0d RTIread=%0d RTIce=%0d",ce_gaps,read_stalls,vector_stalls,stack_stalls,rti_read_stalls,rti_ce_gaps);
            native_cases=native_cases+1;
            $display("NATIVE_IRQ_RTI widths=%02x stalls=%0d software_save=%0d frame=238005%02x restored=23:8005 S=%04x P=%03x B=%02x D=%04x CEgaps=%0d readstalls=%0d vectorstalls=%0d stackstalls=%0d RTIreadstalls=%0d RTIcegaps=%0d",widths,stalls,save_owned,expected_p,cpu.SP,cpu.P,cpu.DBR,cpu.D,ce_gaps,read_stalls,vector_stalls,stack_stalls,rti_read_stalls,rti_ce_gaps);
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
        for(integer widths=0;widths<4;widths=widths+1)
            for(integer stalls=0;stalls<2;stalls=stalls+1)
                native_irq_rti(8'(widths<<4),stalls!=0,1);
        native_irq_rti(0,0,0);native_irq_rti(0,1,0);
        if(native_cases!=10)$fatal(1,"Native IRQ/RTI case count differs");
        $display("PASS: ten native IRQ/RTI cases restore hardware PBR/PC/P/fullSP, distinguish software DBR/D, and cover CE gaps/RDY read stalls");
        $display("PASS: 65C816 WAI masked IRQ wake, IRQ/NMI entry and RTI, STP interrupt immunity and reset");
        $finish;
    end
    initial begin #1000000;$fatal(1,"Interrupt test timeout");end
endmodule
