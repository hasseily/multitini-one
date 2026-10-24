// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_uci_transport;
    reg host_clk=0,core_clk=0;always #5 host_clk=~host_clk;always #7 core_clk=~core_clk;
    reg host_resetn=0,core_resetn=0,host_op_valid=0,core_op_ready=0;
    reg [31:0] host_op=0;wire [31:0] core_op;wire host_op_ready,core_op_valid;
    reg [8:0] host_read_addr=0,host_write_addr=0;reg [31:0] host_write_data=0;
    reg [3:0] host_write_strobe=0;wire [31:0] host_read_data;
    reg [10:0] core_addr=0;reg core_write=0;reg [7:0] core_write_data=0;wire [7:0] core_read_data;
    integer checks=0,waits;
    multitini_c64_uci_transport dut(.*);
    task tick(input integer n);repeat(n)begin @(posedge host_clk);#1;end endtask
    task check(input bit ok,input string message);checks++;if(!ok)$fatal(1,"UCI transport %s",message);endtask
    task host_write(input [8:0] address,input [31:0] data,input [3:0] lanes);
        @(negedge host_clk);host_write_addr=address;host_write_data=data;host_write_strobe=lanes;
        tick(1);@(negedge host_clk);host_write_strobe=0;tick(1);
    endtask
    task core_read(input [10:0] address,input [7:0] expected);
        @(negedge core_clk);core_addr=address;@(posedge core_clk);#1;
        check(core_read_data===expected,"host words visible as ordered core bytes");
    endtask
    initial begin
        tick(20);host_resetn=1;core_resetn=1;tick(30);
        host_write(9'h0,32'h44332211,15);host_write(9'h1,32'h88776655,15);
        core_read(0,8'h11);core_read(1,8'h22);core_read(2,8'h33);core_read(3,8'h44);core_read(7,8'h88);
        host_write(0,32'haabbccdd,4'b0101);core_read(0,8'hdd);core_read(1,8'h22);core_read(2,8'hbb);core_read(3,8'h44);
        @(negedge host_clk);host_read_addr=0;tick(1);check(host_read_data===32'h44bb22dd,"synchronous word read latency");
        @(negedge host_clk);host_read_addr=1;tick(1);check(host_read_data===32'h88776655,"consecutive prefetched address");
        for(integer i=0;i<4;i++)begin
            @(negedge core_clk);core_addr=11'h380+i;core_write_data=8'h90+i;core_write=1;
            @(posedge core_clk);#1;
        end
        @(negedge core_clk);core_write=0;@(negedge host_clk);host_read_addr=9'he0;tick(2);
        check(host_read_data===32'h93929190,"reply bytes visible to host");
        check(host_op_ready,"op mailbox ready");
        @(negedge host_clk);host_op=32'h12345678;host_op_valid=1;tick(1);@(negedge host_clk);host_op_valid=0;
        waits=0;while(!core_op_valid && waits<100)begin tick(1);waits++;end
        check(core_op_valid && core_op===32'h12345678,"tagged op crossing");
        host_op=32'hffffffff;tick(10);check(core_op_valid && core_op===32'h12345678 && !host_op_ready,"payload stable under destination backpressure");
        core_op_ready=1;tick(20);check(host_op_ready && !core_op_valid,"exactly one accepted op");
        core_op_ready=0;@(negedge host_clk);host_op=32'haaaa5555;host_op_valid=1;tick(1);@(negedge host_clk);host_op_valid=0;
        core_resetn=0;tick(20);core_resetn=1;core_op_ready=1;tick(50);
        check(host_op_ready && !core_op_valid,"reset drains pending old operation");
        core_read(0,8'hdd);check(core_read_data===8'hdd,"reset preserves stale command tail memory");
        $display("PASS C64 UCI transport checks=%0d",checks);$finish;
    end
    initial begin #200000;$fatal(1,"UCI transport timeout");end
endmodule
