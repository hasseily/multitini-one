// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_genesis_media_stream;
    reg source_clk=0,control_clk=0,source_resetn=0,control_resetn=0;
    always #5 source_clk=~source_clk;
    always #3.5 control_clk=~control_clk;
    reg [7:0] source_data=0;
    reg source_last=0,source_valid=0;
    reg [31:0] source_id=1,source_generation=12;
    wire source_ready;
    reg complete=0,pop=0;
    reg [31:0] complete_id=0,complete_generation=0;
    wire [7:0] data;
    wire last,valid;
    multitini_genesis_media_stream dut(.*);
    task send(input [31:0] id,generation,input [7:0] value,input bit final_byte);
        begin
            @(negedge source_clk);source_id=id;source_generation=generation;source_data=value;source_last=final_byte;source_valid=1;
            do @(posedge source_clk);while(!source_ready);
            @(negedge source_clk);source_valid=0;
        end
    endtask
    task receive(input [7:0] value,input bit final_byte);
        begin
            wait(valid);@(negedge control_clk);
            if(data!==value || last!==final_byte)$fatal(1,"Save retry received stale or malformed byte: %h",data);
            pop=1;@(negedge control_clk);pop=0;
        end
    endtask
    task finish_request(input [31:0] id,generation);
        begin
            @(negedge control_clk);complete_id=id;complete_generation=generation;complete=1;
            @(negedge control_clk);complete=0;
        end
    endtask
    task no_stale;
        begin
            repeat(60)begin @(negedge control_clk);if(valid)$fatal(1,"Completed request byte remained visible");end
        end
    endtask
    initial begin
        repeat(5)@(negedge control_clk);source_resetn=1;control_resetn=1;
        send(1,12,8'h11,0);receive(8'h11,0);
        // Firmware fails before consuming a byte already waiting in MMIO.
        send(1,12,8'h22,0);wait(valid);finish_request(1,12);no_stale();
        // The source has not yet observed the completion CDC and launches a
        // late old byte. It must drain without a firmware POP.
        send(1,12,8'h33,1);no_stale();
        send(2,12,8'ha5,0);receive(8'ha5,0);
        send(2,12,8'h5a,1);receive(8'h5a,1);finish_request(2,12);
        // IDs may repeat after generation changes; generation is part of the tag.
        send(2,13,8'hc3,1);receive(8'hc3,1);finish_request(2,13);no_stale();
        // Completion can beat the byte across the two independent mailboxes.
        finish_request(3,13);send(3,13,8'hff,1);no_stale();
        send(4,13,8'h7e,1);receive(8'h7e,1);
        $display("PASS Genesis tagged media CDC: queued/late failure bytes drained, retry order and generations preserved");
        $finish;
    end
    initial begin #100000;$fatal(1,"Media stream timeout");end
endmodule
