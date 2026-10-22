// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Expected values come from the compiled, unchanged MAME m6510 port methods.
module tb_c64_mame_port;
    logic clk=0,reset=0,write_enable=0,register_select=0,decay_tick=0;
    logic [7:0] write_data=0,external_input=8'h17;
    wire [7:0] read_data,output_pins;
    integer file_handle,fields,operation,value,external_pins,expected_ddr,expected_read,expected_output;
    integer target,ticks=0,checks=0;
    logic [7:0] held_read,held_output;
    always #5 clk=!clk;
    multitini_c64_6510_port dut(.*);

    task automatic check(input logic okay,input string detail);
        if(!okay) $fatal(1,"FAIL: MAME 6510 port event=%0d native_tick=%0d %s",checks,ticks,detail);
    endtask

    initial begin
        file_handle=$fopen("port_vectors.txt","r");
        if(!file_handle) $fatal(1,"FAIL: missing actual MAME port vectors");
        while(!$feof(file_handle)) begin
            fields=$fscanf(file_handle,"%d %d %h %h %h %h %h\n",target,operation,value,
                           external_pins,expected_ddr,expected_read,expected_output);
            if(fields!=7) $fatal(1,"FAIL: malformed port-vector row");
            check(target>=ticks,"native time must be monotonic");
            while(ticks<target) begin
                @(negedge clk);decay_tick=1;
                @(posedge clk);#1;ticks++;
            end
            @(negedge clk);decay_tick=0;write_enable=0;reset=0;
            external_input=external_pins[7:0];write_data=value[7:0];
            if(operation==0)reset=1;
            else if(operation==1 || operation==2)begin
                write_enable=1;register_select=(operation==2);
            end
            @(posedge clk);#1;
            @(negedge clk);write_enable=0;reset=0;register_select=0;
            #1;check(read_data===expected_ddr[7:0],$sformatf("DDR actual=%02h expected=%02h",read_data,expected_ddr));
            register_select=1;
            #1;check(read_data===expected_read[7:0],$sformatf("port actual=%02h expected=%02h",read_data,expected_read));
            check(output_pins===expected_output[7:0],$sformatf("pins actual=%02h expected=%02h",output_pins,expected_output));
            // Extra FPGA clocks and CPU activity without a native tick cannot
            // age the floating pins. Turbo and RDY therefore share real time.
            held_read=read_data;held_output=output_pins;
            repeat(5)begin
                @(posedge clk);#1;
                check(read_data===held_read && output_pins===held_output,"no decay without native tick");
            end
            checks++;
        end
        $fclose(file_handle);
        check(checks>=40 && ticks>=2100000,"expiry/re-drive/reset coverage completed");
        $display("PASS: C64 MAME 6510 port %0d reference events across %0d native ticks",checks,ticks);
        $finish;
    end
    initial begin #30000000;$fatal(1,"FAIL: MAME port test timeout");end
endmodule
