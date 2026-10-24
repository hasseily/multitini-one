// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_iec9;
    parameter integer CLOCK_HZ=1000000;
    logic clk=0;
    always #(500000000.0/CLOCK_HZ) clk=~clk;
    logic reset=1,run=0,quiesce=0,atn=1,host_clk=1,host_data=1;
    wire dev_clk,dev_data;
    wire bus_clk=host_clk&&dev_clk;
    wire bus_data=host_data&&dev_data;
    wire [31:0] event_data;
    wire event_valid,reply_ready,busy,quiescent;
    wire [15:0] token;
    logic event_ready=1,reply_valid=0;
    logic [31:0] reply_data=0;
    logic [31:0] events[0:255];
    integer count=0;
    multitini_c64_iec9 #(.CLOCK_HZ(CLOCK_HZ)) dut(
        .clk(clk),.hard_reset(reset),.run(run),.quiesce(quiesce),.atn(atn),
        .bus_clk(bus_clk),.bus_data(bus_data),.clk_o(dev_clk),.data_o(dev_data),
        .event_data(event_data),.event_valid(event_valid),.event_ready(event_ready),
        .reply_data(reply_data),.reply_valid(reply_valid),.reply_ready(reply_ready),
        .busy(busy),.quiescent(quiescent),.token(token));
    always @(posedge clk) if(event_valid&&event_ready) begin
        events[count]=event_data;count=count+1;
        $display("EVENT %0d %08x",count,event_data);
    end
    task automatic us(input integer n);
        repeat((64'(CLOCK_HZ)*n+999999)/1000000) @(negedge clk);
    endtask
    task automatic wait_data(input bit level);
        integer limit;begin
            limit=0;while(bus_data!==level && limit<3000) begin us(1);limit++;end
            if(bus_data!==level) $fatal(1,"DATA timeout state=%0d target=%b",dut.state,level);
        end
    endtask
    task automatic wait_clock(input bit level);
        integer limit;begin
            limit=0;while(bus_clk!==level && limit<10000) begin us(1);limit++;end
            if(bus_clk!==level) $fatal(1,"CLK timeout state=%0d target=%b",dut.state,level);
        end
    endtask
    task automatic send_byte(input logic [7:0] data,input bit eoi,input bit ack);
        begin
            host_data=1;host_clk=0;us(20);host_clk=1;wait_data(1);
            if(eoi) begin wait_data(0);wait_data(1);end
            else us(20);
            host_clk=0;us(20);
            for(integer b=0;b<8;b++) begin
                host_data=data[b];us(30);host_clk=1;us(30);host_clk=0;us(3);
            end
            host_data=1;
            if(ack) wait_data(0);
            else begin us(1100);if(!bus_data)$fatal(1,"foreign device frame falsely ACKed");end
            us(30);
        end
    endtask
    task automatic command_start;
        // Unmodified KERNAL can assert ATN while CLK is still high, then
        // pull it low before releasing it for the first command's handshake.
        host_clk=1;host_data=1;atn=0;us(20);host_clk=0;us(110);
    endtask
    task automatic command_end;
        atn=1;us(40);
    endtask
    task automatic talk_start(input logic [3:0] channel);
        command_start();send_byte(8'h49,0,1);send_byte(8'h60|channel,0,1);
        host_data=0;atn=1;host_clk=1;wait_clock(0);us(1);
    endtask
    task automatic post_reply(input logic [15:0] tag,input logic [7:0] data,input bit eoi,input bit error);
        begin
            reply_data={tag,6'd0,error,eoi,data};reply_valid=1;
            do @(posedge clk); while(!reply_ready);
            @(negedge clk);reply_valid=0;
        end
    endtask
    task automatic receive_byte(output logic [7:0] data,output bit eoi);
        integer elapsed;begin
            host_data=0;host_clk=1;wait_clock(1);host_data=1;
            elapsed=0;eoi=0;
            while(bus_clk && elapsed<240) begin us(1);elapsed++;end
            if(bus_clk) begin eoi=1;host_data=0;us(80);host_data=1;end
            wait_clock(0);
            for(integer b=0;b<8;b++) begin wait_clock(1);data[b]=bus_data;wait_clock(0);end
            host_data=0;us(65);
        end
    endtask
    task automatic expect_event(input integer index,input integer kind,input integer channel,input integer data);
        if(events[index][15:12]!==4'(kind) || events[index][11:8]!==4'(channel) || events[index][7:0]!==8'(data))
            $fatal(1,"event[%0d]=%08x expected %x/%x/%x",index,events[index],kind,channel,data);
    endtask
    logic [7:0] got;
    bit got_eoi;
    integer n;
    logic [15:0] old_token;
    initial begin
        us(5);reset=0;run=1;us(30);expect_event(0,0,0,0);
        command_start();send_byte(8'h29,0,1);send_byte(8'hf2,0,1);command_end();
        send_byte(8'h41,0,1);send_byte(8'h42,1,1);
        command_start();send_byte(8'h3f,0,1);command_end();host_clk=1;us(10);
        expect_event(1,1,2,8'hf2);expect_event(2,3,2,8'h41);
        expect_event(3,4,2,8'h42);expect_event(4,5,2,8'h3f);
        if(!dev_data||!dev_clk)$fatal(1,"unlisten did not release bus");
        // Foreign primary must not be frame-ACKed, including device 8/10.
        command_start();send_byte(8'h2a,0,0);command_end();
        command_start();send_byte(8'h28,0,0);command_end();
        n=count;talk_start(2);us(5);expect_event(n,6,2,0);
        fork
            begin post_reply(token,8'h96,0,0);end
            begin receive_byte(got,got_eoi);if(got!=8'h96||got_eoi)$fatal(1,"first read wrong %02x EOI%b",got,got_eoi);end
        join
        us(120);
        expect_event(n+1,10,2,8'h96);expect_event(n+2,9,2,0);
        fork
            begin post_reply(token,8'h5a,1,0);end
            begin receive_byte(got,got_eoi);if(got!=8'h5a||!got_eoi)$fatal(1,"EOI read wrong %02x EOI%b",got,got_eoi);end
        join
        expect_event(n+3,10,2,8'h5a);
        command_start();send_byte(8'h5f,0,1);command_end();
        // SD latency holds CLK low, so it cannot be confused with EOI.
        talk_start(2);us(5);old_token=token;us(4000);
        if(dev_clk!==0)$fatal(1,"storage latency falsely signalled ready/EOI");
        command_start();send_byte(8'h5f,0,1);command_end();
        post_reply(old_token,8'hee,0,0);us(10);
        if(!dev_clk||!dev_data)$fatal(1,"stale reply drove bus");
        // A cancelled reply arriving during a *new* read must not satisfy it.
        talk_start(2);us(5);post_reply(old_token,8'hee,0,0);us(120);
        if(dev_clk!==0)$fatal(1,"stale response satisfied a new request");
        n=count;
        fork
            begin post_reply(token,8'hc3,0,0);end
            begin
                host_data=0;host_clk=1;wait_clock(1);host_data=1;wait_clock(0);
                for(integer b=0;b<8;b++)begin wait_clock(1);got[b]=bus_data;wait_clock(0);end
                if(got!=8'hc3)$fatal(1,"replacement request returned stale data");
                // Acknowledge and begin ATN on the same native edge.
                host_data=0;host_clk=0;atn=0;us(100);
            end
        join
        expect_event(n,10,2,8'hc3);
        send_byte(8'h5f,0,1);command_end();
        expect_event(n+1,7,2,8'h5f);
        // Empty/error stream: released wires permit KERNAL sender timeout.
        talk_start(2);us(5);post_reply(token,0,0,1);host_data=1;us(700);
        if(!dev_clk||!dev_data)$fatal(1,"empty stream drove a fabricated byte");
        command_start();send_byte(8'h5f,0,1);command_end();
        // Preserve an ACKed event while quiesced, then drain it exactly once.
        command_start();send_byte(8'h29,0,1);send_byte(8'h6f,0,1);command_end();
        event_ready=0;n=count;
        // Byte admission was already granted by the prior listener handshake.
        event_ready=1;host_clk=1;us(5);event_ready=0;
        host_clk=0;us(20);
        for(integer b=0;b<8;b++)begin host_data=(8'h33>>b)&1;us(30);host_clk=1;us(30);host_clk=0;us(3);end
        host_data=1;wait_data(0);quiesce=1;us(10);
        if(quiescent||!event_valid||!dev_clk||!dev_data)$fatal(1,"quiesce lost accepted event");
        event_ready=1;us(10);if(!quiescent||count!=n+1)$fatal(1,"quiesce failed to drain");
        expect_event(n,3,15,8'h33);
        old_token=token;run=0;us(5);run=1;quiesce=0;us(20);
        if(token==old_token)$fatal(1,"restart reused token");
        post_reply(old_token,8'hff,0,0);us(10);
        if(!dev_clk||!dev_data)$fatal(1,"restart accepted stale reply");
        $display("PASS IEC9 protocol CLOCK_HZ=%0d events=%0d",CLOCK_HZ,count);$finish;
    end
    initial begin #300000000;$fatal(1,"overall timeout");end
endmodule
