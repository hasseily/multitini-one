// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Same accepted C64 bus operations; each host API publishes the same buffers.
// Reference internals are never changed or replaced by a behavioral model.
module tb_c64_uci_oracle;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,bus_valid=0,bus_write=0,op_valid=0,ff00_write=0;
    reg [2:0] bus_addr=0;reg [7:0] bus_wdata=0;
    reg host_read=0,host_write=0;reg [3:0] host_addr=0;
    reg [7:0] host_wdata=0;reg [31:0] op_data=0;
    wire [7:0] actual,expected,host_rdata,ram_wdata,ref_wdata;
    wire [10:0] ram_addr,ref_addr;
    wire ram_we,ref_we,ref_ce,ref_selected,ref_irq,ref_freeze,ref_host_irq,host_ack;
    wire irq,freeze;wire [31:0] status,status2;
    reg [7:0] ram_rdata,ref_rdata;
    reg [7:0] memory[0:2047],reference_memory[0:2047];
    integer checks=0,cases=0,differences=0,trace_file;
    bit wrong_irq_clear=0,irq_clear_seen=0;
    string scenario;
    multitini_c64_uci dut(.clk(clk),.hard_reset(reset),.block_reset(reset),
        .enabled(1'b1),.suspended(1'b0),.ff00_write(ff00_write),.bus_valid(bus_valid),.bus_write(bus_write),
        .bus_addr(bus_addr),.bus_wdata(bus_wdata),.rdata(actual),.bus_id(5'd31),
        .freeze_allowed(1'b1),.irq_allowed(1'b1),.op_data(op_data),.op_valid(op_valid),
        .op_ready(),.status(status),.status2(status2),.ram_addr(ram_addr),.ram_we(ram_we),
        .ram_wdata(ram_wdata),.ram_rdata(ram_rdata),.ram_read_done(4'd0),.freeze(freeze),.irq(irq));
    c64_uci_upstream_bridge oracle(.clk(clk),.reset(reset),.bus_valid(bus_valid),
        .bus_write(bus_write),.bus_addr(bus_addr),.bus_wdata(bus_wdata),.bus_rdata(expected),
        .bus_selected(ref_selected),.irq(ref_irq),.freeze(ref_freeze),.host_irq(ref_host_irq),
        .ff00_write(ff00_write),.host_read(host_read),.host_write(host_write),.host_addr(host_addr),
        .host_wdata(host_wdata),.host_rdata(host_rdata),.host_ack(host_ack),.ram_addr(ref_addr),
        .ram_rdata(ref_rdata),.ram_wdata(ref_wdata),.ram_ce(ref_ce),.ram_we(ref_we));
    always @(posedge clk)begin
        if(ram_we)memory[ram_addr]<=ram_wdata;
        ram_rdata<=memory[ram_addr];
        if(ref_ce)begin
            if(ref_we)reference_memory[ref_addr]<=ref_wdata;
            ref_rdata<=reference_memory[ref_addr];
        end
    end
    task tick(input integer n);repeat(n)begin @(posedge clk);#1;end endtask
    task demand(input bit condition,input string message);
        checks++;if(!condition)$fatal(1,"UCI_ORACLE %s: %s actual=%02x expected=%02x status=%08x",scenario,message,actual,expected,status);
    endtask
    task equal_byte(input string label_text);
        $fdisplay(trace_file,"%s,%s,%0d,%02x,%02x,%0d,%0d,%0d,%0d",scenario,label_text,bus_addr,actual,expected,irq,ref_irq,freeze,ref_freeze);
        demand(actual===expected,label_text);
    endtask
    task ref_write(input [3:0] a,input [7:0] d);
        @(negedge clk);host_addr=a;host_wdata=d;host_write=1;tick(1);
        demand(host_ack===1'b1,"upstream host write ack");
        @(negedge clk);host_write=0;tick(3);
    endtask
    task ref_read(input [3:0] a,output [7:0] d);
        @(negedge clk);host_addr=a;host_read=1;tick(1);
        demand(host_ack===1'b1,"upstream host read ack");d=host_rdata;
        @(negedge clk);host_read=0;tick(2);
    endtask
    task host_op(input [3:0] code,input integer bytes,input integer textlen);
        @(negedge clk);op_data=0;op_data[3:0]=code;op_data[14:4]=11'(bytes);
        op_data[24:16]=9'(textlen);op_data[28:25]=status[21:18];op_data[31:29]=status[24:22];
        op_valid=1;tick(1);@(negedge clk);op_valid=0;tick(16);
    endtask
    task inspect(input [2:0] address,input string label_text);
        @(negedge clk);bus_addr=address;tick(8);equal_byte(label_text);
    endtask
    task snapshot(input string label_text);
        inspect(4,label_text);
        if(wrong_irq_clear&&irq_clear_seen&&ref_irq===1'b0)
            demand((irq|1'b1)===ref_irq,"NEGATIVE CONTROL wrong IRQ clearing");
        demand(irq===ref_irq,"C64 IRQ level");demand(freeze===ref_freeze,"FREEZE level");
        demand(ref_selected===1'b1,"upstream slot decode");inspect(5,"IRQ-identification byte");
    endtask
    task put(input [2:0] address,input [7:0] data);
        @(negedge clk);bus_addr=address;bus_wdata=data;bus_valid=1;bus_write=1;
        tick(1);@(negedge clk);bus_valid=0;bus_write=0;tick(8);
    endtask
    task get(input [2:0] address,input [7:0] value);
        @(negedge clk);bus_addr=address;tick(8);
        equal_byte("accepted read");demand(actual===value,"independent fixture byte");
        @(negedge clk);bus_valid=1;tick(1);
        @(negedge clk);bus_valid=0;tick(8);
        if(address==6||address==7)irq_clear_seen=1;
    endtask
    task fresh(input string name);
        scenario=name;cases++;irq_clear_seen=0;
        @(negedge clk);reset=1;bus_valid=0;bus_write=0;op_valid=0;
        host_read=0;host_write=0;tick(3);@(negedge clk);reset=0;tick(4);
        for(integer i=0;i<2048;i++)begin memory[i]=8'ha5;reference_memory[i]=8'ha5;end
        ref_write(0,8'h46);ref_write(1,1);ref_write(1,8'h9f);ref_write(11,0);
        snapshot("reset idle");inspect(3,"bus id");
    endtask
    task fill_command(input integer length);
        for(integer i=0;i<length;i++)put(5,8'(i*37+11));
        for(integer i=0;i<length;i++)begin
            demand(memory[i]===reference_memory[i],"command RAM equality");
            demand(memory[i]===8'(i*37+11),"command RAM fixture");
        end
    endtask
    task command_size(input integer expected_length,input bit legacy_last_byte);
        reg [7:0] lo,hi;integer size;
        ref_read(14,lo);ref_read(15,hi);size={hi[2:0],lo};
        demand(status[17:8]==expected_length,"exact DUT command length");
        if(legacy_last_byte)begin
            demand(size==895&&expected_length==896,"pinned upstream 896 command length off-by-one");
            differences++;$display("EXPECTED_DIFFERENCE command_896_length upstream=%0d DUT=%0d",size,expected_length);
        end else demand(size==expected_length,"upstream command length");
    endtask
    task acknowledge_command;
        ref_write(2,1);host_op(1,0,0);snapshot("host command ack");
    endtask
    task trigger_state(input bit armed,input bit held);
        reg [7:0] observed;
        ref_read(2,observed);
        demand(observed[6]===armed&&status[6]===armed,"FF00 trigger latch matches");
        demand(observed[7]===held&&freeze===held&&ref_freeze===held,"FF00 freeze matches");
    endtask
    task ff00;
        @(negedge clk);ff00_write=1;tick(1);@(negedge clk);ff00_write=0;tick(8);
    endtask
    task publish(input bit more,input integer length,input integer textlen);
        for(integer i=0;i<length;i++)begin memory[896+i]=8'(i*19+7);reference_memory[896+i]=8'(i*19+7);end
        for(integer i=0;i<textlen;i++)begin memory[1792+i]=8'(i*13+3);reference_memory[1792+i]=8'(i*13+3);end
        ref_write(12,8'(length));ref_write(13,8'(length>>8));ref_write(10,8'(textlen));
        ref_write(2,more?8'h30:8'h10);host_op(more?5:4,length,textlen);snapshot("published reply");
    endtask
    initial begin
        wrong_irq_clear=$test$plusargs("wrong_irq_clear");
        trace_file=$fopen(wrong_irq_clear?"negative.csv":"comparison.csv","w");
        demand(trace_file!=0,"trace opened");
        $fdisplay(trace_file,"case,event,register,dut,upstream,dut_irq,upstream_irq,dut_freeze,upstream_freeze");
        for(integer n=0;n<6;n++)begin
            integer length;
            case(n)0:length=0;1:length=1;2:length=2;3:length=31;4:length=255;default:length=895;endcase
            fresh($sformatf("command_%0d",length));fill_command(length);command_size(length,0);
            put(4,1);snapshot("PUSH");acknowledge_command();publish(0,0,0);
            put(4,2);snapshot("LAST accepted");
        end
        fresh("command_896");fill_command(896);command_size(896,1);
        put(4,1);snapshot("PUSH");acknowledge_command();publish(0,0,0);put(4,2);snapshot("LAST accepted");
        fresh("response_895_status_255");fill_command(2);put(4,8'ha1);snapshot("freeze PUSH");
        acknowledge_command();publish(0,895,255);
        for(integer i=0;i<895;i++)get(6,8'(i*19+7));
        for(integer i=0;i<255;i++)get(7,8'(i*13+3));
        snapshot("maximum common queues empty");get(6,0);get(7,0);put(4,2);snapshot("LAST accepted");
        fresh("response_896");put(5,1);put(4,1);acknowledge_command();publish(0,896,0);
        for(integer i=0;i<896;i++)get(6,8'(i*19+7));
        @(negedge clk);bus_addr=4;tick(8);
        demand(actual==8'h20&&expected==8'ha0,"pinned upstream final response pointer saturates");
        @(negedge clk);bus_addr=6;tick(8);
        demand(actual==0&&expected==8'(895*19+7),"pinned upstream repeats 896th reply byte");
        differences++;$display("EXPECTED_DIFFERENCE response_896_tail upstream repeats last byte; DUT empty");
        put(4,2);snapshot("LAST accepted after bounded maximum");
        for(integer more=0;more<2;more++)for(integer request_irq=0;request_irq<2;request_irq++)
        for(integer clear=0;clear<5;clear++)begin
            fresh($sformatf("irq_%0d_more_%0d_clear_%0d",request_irq,more,clear));
            put(5,1);put(4,request_irq?8'h21:1);snapshot("PUSH IRQ request");acknowledge_command();
            publish(more!=0,clear==2?0:2,clear==3?0:2);
            case(clear)
                0:get(6,7);
                1:get(7,3);
                2:get(6,0);
                3:get(7,0);
                4:begin put(4,2);irq_clear_seen=1;end
            endcase
            snapshot("IRQ acknowledged");
            if(clear!=4)begin put(4,2);snapshot("DATA_ACC");end
            if(more)begin ref_write(2,2);host_op(2,0,0);snapshot("MORE host ack");
                publish(0,1,1);get(6,7);get(7,3);put(4,2);snapshot("second LAST accepted");end
        end
        fresh("rejected_push_normal_bits");put(5,1);put(4,1);put(4,1);snapshot("BUSY PUSH rejected");
        put(4,8);snapshot("clear error");acknowledge_command();publish(0,2,2);
        put(4,1);snapshot("LAST PUSH rejected");put(4,8);snapshot("clear LAST error");
        put(4,3);snapshot("DATA_ACC plus PUSH uses old state");put(4,8);snapshot("clear combined error");
        fresh("busy_abort");put(5,1);put(4,1);put(4,4);snapshot("active ABORT");
        ref_write(2,8'h87);host_op(3,0,0);snapshot("abort host cleanup");
        fresh("idle_abort_fixed");put(5,1);put(4,4);
        @(negedge clk);bus_addr=4;tick(8);
        demand(actual==0&&expected==4&&status[29],"fixed idle ABORT uses separate sticky diagnostic");
        differences++;$display("EXPECTED_DIFFERENCE idle_abort upstream exposed abort; DUT separate sticky bit");
        fresh("busy_write_fixed");put(5,8'h11);put(4,1);put(5,8'hff);
        demand(memory[1]==8'ha5&&reference_memory[1]==8'hff&&status2[22],"fixed command busy write is dropped");
        differences++;$display("EXPECTED_DIFFERENCE busy_write upstream mutates command; DUT drops");
        snapshot("normal status unaffected by fixed busy write");
        fresh("overflow_fixed");fill_command(896);put(5,8'hf0);
        demand(memory[895]==8'(895*37+11)&&reference_memory[895]==8'hf0&&status2[23],"fixed overflow cannot replace last byte");
        differences++;$display("EXPECTED_DIFFERENCE overflow upstream overwrites last byte; DUT bounds command");
        fresh("ff00_deferred_trigger");put(5,1);put(4,8'h41);snapshot("trigger arms without freeze");
        trigger_state(1,0);inspect(4,"ordinary reads do not trigger");trigger_state(1,0);
        ff00();snapshot("FF00 asserts freeze");trigger_state(0,1);
        acknowledge_command();trigger_state(0,1);publish(0,1,1);trigger_state(0,0);
        ff00();snapshot("consumed trigger cannot refire");trigger_state(0,0);
        fresh("ff00_immediate_and_deferred");put(5,1);put(4,8'hc1);trigger_state(1,1);
        ff00();snapshot("immediate freeze retained");trigger_state(0,1);
        acknowledge_command();publish(0,0,0);trigger_state(0,0);
        fresh("ff00_validate_cancels_trigger");put(5,1);put(4,8'h41);trigger_state(1,0);
        acknowledge_command();publish(0,0,0);trigger_state(0,0);
        ff00();snapshot("published reply cancels deferred trigger");trigger_state(0,0);
        fresh("ff00_reset_cancels_trigger");put(5,1);put(4,8'h41);trigger_state(1,0);
        @(negedge clk);reset=1;tick(3);@(negedge clk);reset=0;tick(4);
        ref_write(0,8'h46);ref_write(1,1);ref_write(1,8'h9f);ref_write(11,0);
        ff00();snapshot("guest reset cancels deferred trigger");trigger_state(0,0);
        fresh("ff00_publication_priority");put(5,1);put(4,8'h41);acknowledge_command();
        // Align the reference host VALIDATE with the DUT's actual publication
        // edge, after its intentionally different host/prefetch pipeline.
        @(negedge clk);op_data=0;op_data[3:0]=4;op_data[28:25]=status[21:18];
        op_data[31:29]=status[24:22];op_valid=1;tick(1);
        @(negedge clk);op_valid=0;host_addr=2;host_wdata=8'h10;host_write=1;ff00_write=1;
        tick(1);demand(!freeze&&!ref_freeze,"VALIDATE wins coincident FF00 trigger");
        @(negedge clk);host_write=0;ff00_write=0;tick(16);
        snapshot("FF00 and publication same edge");trigger_state(0,0);
        $fclose(trace_file);$display("PASS C64 UCI oracle cases=%0d checks=%0d intentional_differences=%0d",cases,checks,differences);$finish;
    end
    initial begin #5000000;$fatal(1,"UCI_ORACLE timeout");end
endmodule
