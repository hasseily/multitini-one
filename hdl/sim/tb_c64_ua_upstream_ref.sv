// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Original stimulus for the exact upstream test-only VHDL. No product RTL.
module c64_ua_reference_cases #(parameter integer CLOCK_HZ=100000000);
    reg clk=0, reset=1, io_write=0, io_read=0;
    reg [7:0] io_address=0, io_wdata=0;
    wire [7:0] io_rdata, memory_tag;
    wire io_ack, irq, memory_request, new_sample;
    wire [25:0] memory_address;
    wire [17:0] sample_l,sample_r;
    always #(500000000.0/CLOCK_HZ) clk=~clk;
    c64_ua_upstream_bridge #(.g_clock_freq(CLOCK_HZ)) reference(
        .clock(clk),.reset(reset),.io_write(io_write),.io_read(io_read),
        .io_address(io_address),.io_wdata(io_wdata),.io_rdata(io_rdata),
        .io_ack(io_ack),.irq(irq),.memory_request(memory_request),
        .memory_address(memory_address),.memory_tag(memory_tag),
        .sample_l(sample_l),.sample_r(sample_r),.new_sample(new_sample));
    integer trace, cycle=0, scenario=0, fetches=0, mode=0, passed=0;
    reg tracing=0,last_irq=0;
    always @(posedge clk) begin
        cycle++;
        if(tracing && memory_request) begin
            $fwrite(trace,"%0d,%0d,FETCH,%07x,%02x,%0d\n",scenario,cycle,memory_address,memory_tag,irq);
            if(mode==0 || mode==2 || !memory_tag[0]) fetches++;
        end
        if(tracing && irq!=last_irq)
            $fwrite(trace,"%0d,%0d,IRQ,0000000,00,%0d\n",scenario,cycle,irq);
        last_irq=irq;
    end
    task automatic write_reg(input integer address,value);
        @(negedge clk);io_write=1;io_address=address;io_wdata=value;
        @(negedge clk);io_write=0;
    endtask
    task automatic word24(input integer address,value);
        write_reg(address,value>>16);write_reg(address+1,value>>8);write_reg(address+2,value);
    endtask
    task automatic stop;
        write_reg(0,0);repeat(24) @(negedge clk);
        write_reg(31,255);repeat(16) @(negedge clk);
        tracing=0;
    endtask
    task automatic test_case(input integer id,rate,format,kind);
        integer stride, length_value, limit, clocks;
        stop(); scenario=id;mode=format;fetches=0;
        stride=(format&1)?2:1;
        if(format&2)stride=stride*2;
        length_value=kind==3 ? 256 : 8*stride;
        word24(5,0);word24(9,length_value);word24(17,2*stride);
        word24(21,kind==0 || kind==3 ? 0 : length_value);
        write_reg(14,rate>>8);write_reg(15,rate);
        write_reg(1,32);write_reg(2,8);
        $display("REF_CASE %0d rate=%0d mode=%0d kind=%0d length=%0d",id,rate,format,kind,length_value);
        tracing=1;last_irq=irq;
        write_reg(0,5 | ((format&1)?16:0) | ((format&2)?64:0) | ((kind==2)?2:0));
        clocks=0;limit=(kind==1 || kind==2)?20:(kind==3?256:8);
        while(fetches<limit && clocks<400000)begin @(negedge clk);clocks++;end
        if(fetches<limit)$fatal(1,"FAIL: upstream reference missing fetch case=%0d",id);
        if(kind==0 || kind==3)begin
            while(!irq && clocks<500000)begin @(negedge clk);clocks++;end
            if(!irq)$fatal(1,"FAIL: upstream normal completion IRQ case=%0d",id);
        end else begin
            if(irq)$fatal(1,"FAIL: upstream stale-B/loop must not finish case=%0d",id);
            if(kind==2)begin
                // Release with B==length retains the known upstream defect.
                write_reg(0,6 | ((format&1)?16:0) | ((format&2)?64:0));
                limit=fetches+20;
                while(fetches<limit && clocks<500000)begin @(negedge clk);clocks++;end
                if(irq || fetches<limit)$fatal(1,"FAIL: upstream release/B-length quirk case=%0d",id);
            end
        end
        passed++;stop();
    endtask
    initial begin
        trace=$fopen("upstream_trace.csv","w");
        $fwrite(trace,"scenario,cycle,event,address,tag,irq\n");
        repeat(32)@(negedge clk);reset=0;
        for(integer i=0;i<4;i++)begin
            for(integer f=0;f<4;f++)test_case(i*4+f, i==0?1:i==1?281:i==2?283:781,f,0);
        end
        test_case(16,1,0,1);
        test_case(17,1,1,1);
        test_case(18,3,0,2);
        test_case(19,3,1,2);
        test_case(20,1,0,3);
        $fclose(trace);
        $display("PASS: UA exact upstream reference clock=%0d cases=%0d",CLOCK_HZ,passed);
        $finish;
    end
    initial begin #500000000; $fatal(1,"FAIL: upstream reference timeout");end
endmodule
module tb_c64_ua_upstream_ref;
    c64_ua_reference_cases #(.CLOCK_HZ(100000000)) cases();
endmodule
module tb_c64_ua_upstream_ref_50;
    c64_ua_reference_cases #(.CLOCK_HZ(50000000)) cases();
endmodule
module tb_c64_ua_upstream_ref_66;
    c64_ua_reference_cases #(.CLOCK_HZ(66666667)) cases();
endmodule
