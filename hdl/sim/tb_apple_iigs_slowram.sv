// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_apple_iigs_slowram;
    reg clock_a=0, clock_b=0;
    always #5 clock_a=~clock_a;
    initial begin #2; forever #7 clock_b=~clock_b; end
    reg enable_a=0, enable_b=0, wren_a=0, wren_b=0;
    reg [16:0] address_a=0, address_b=0;
    reg [7:0] data_a=0, data_b=0;
    wire [7:0] q_a,q_b;
    apple_iigs_slowram dut(.*);
    byte model [0:131071];
    byte last_a=0, last_b=0;
    integer checks_a=0,checks_b=0,writes_a=0,writes_b=0,reads_a=0,reads_b=0;

    function automatic byte pattern(input integer address, input byte salt);
        // Each bank differs even at matching low addresses; the low pattern
        // also distinguishes every possible single-address-bit alias.
        return 8'(address ^ (address>>7) ^ (address>>14)*29 ^ salt);
    endfunction

    task automatic cycle_a(input bit en, input bit wr, input integer address, input byte value);
        @(negedge clock_a);
        enable_a=en;wren_a=wr;address_a=17'(address);data_a=value;
        #1;
        if(q_a!==last_a) $fatal(1,"A output changed before edge at %05x: %02x/%02x",address,q_a,last_a);
        @(posedge clock_a);
        if(en) begin
            if(wr) begin model[address]=value;last_a=value;writes_a++;end
            else begin last_a=model[address];reads_a++;end
        end
        #1;
        if(q_a!==last_a) $fatal(1,"A latency/CE/write-first at %05x: %02x/%02x en%0d wr%0d",address,q_a,last_a,en,wr);
        checks_a++;
    endtask

    task automatic cycle_b(input bit en, input bit wr, input integer address, input byte value);
        @(negedge clock_b);
        enable_b=en;wren_b=wr;address_b=17'(address);data_b=value;
        #1;
        if(q_b!==last_b) $fatal(1,"B output changed before edge at %05x: %02x/%02x",address,q_b,last_b);
        @(posedge clock_b);
        if(en) begin
            if(wr) begin model[address]=value;last_b=value;writes_b++;end
            else begin last_b=model[address];reads_b++;end
        end
        #1;
        if(q_b!==last_b) $fatal(1,"B latency/CE/write-first at %05x: %02x/%02x en%0d wr%0d",address,q_b,last_b,en,wr);
        checks_b++;
    endtask

    initial begin
        for(integer i=0;i<131072;i++)model[i]=0;
        #1;
        if(q_a!==0 || q_b!==0)$fatal(1,"Nonzero power-up output");
        // The unrelated clocks exercise both ports concurrently, on disjoint
        // bytes. Same-byte cross-clock write collisions are intentionally absent.
        fork
            begin for(integer i=0;i<65536;i++)cycle_a(1,0,i,0);enable_a=0;end
            begin for(integer i=65536;i<131072;i++)cycle_b(1,0,i,0);enable_b=0;end
        join
        fork
            begin for(integer i=0;i<65536;i++)cycle_a(1,1,i,pattern(i,8'h53));enable_a=0;end
            begin for(integer i=65536;i<131072;i++)cycle_b(1,1,i,pattern(i,8'h53));enable_b=0;end
        join
        // Read every byte through the opposite port. This catches bank/port
        // aliasing and checks visibility after writes independently of q bypass.
        fork
            begin for(integer i=65536;i<131072;i++)cycle_a(1,0,i,0);enable_a=0;end
            begin for(integer i=0;i<65536;i++)cycle_b(1,0,i,0);enable_b=0;end
        join
        // Rewrite every byte through the other port, then cross-read again.
        fork
            begin for(integer i=65536;i<131072;i++)cycle_a(1,1,i,pattern(i,8'ha6));enable_a=0;end
            begin for(integer i=0;i<65536;i++)cycle_b(1,1,i,pattern(i,8'ha6));enable_b=0;end
        join
        fork
            begin for(integer i=0;i<65536;i++)cycle_a(1,0,i,0);enable_a=0;end
            begin for(integer i=65536;i<131072;i++)cycle_b(1,0,i,0);enable_b=0;end
        join
        // Explicit first/last bytes around all seven internal boundaries plus
        // outer ends, disabled bank changes, and disabled writes on both ports.
        for(integer bank=0;bank<8;bank++)begin
            cycle_a(1,1,bank*16384,8'(bank+8'h31));enable_a=0;
            cycle_b(1,1,bank*16384+16383,8'(bank+8'h91));enable_b=0;
            cycle_a(0,1,((bank+1)%8)*16384,8'hff);enable_a=0;
            cycle_b(0,1,((bank+5)%8)*16384+16383,8'hff);enable_b=0;
            cycle_a(1,0,bank*16384+16383,0);enable_a=0;
            cycle_b(1,0,bank*16384,0);enable_b=0;
        end
        for(integer bank=0;bank<8;bank++)begin
            cycle_a(1,0,bank*16384,0);enable_a=0;
            cycle_b(1,0,bank*16384+16383,0);enable_b=0;
        end
        if(reads_a!=196624 || reads_b!=196624 || writes_a!=131080 || writes_b!=131080)
            $fatal(1,"Unexpected coverage counts");
        $display("PASS: banked slowram full128KiB zero/init/alias/cross-port, reads=%0d writes=%0d checks=%0d; eight-bank boundaries, independent clocks, one-edge read/write-first and disabled CE",reads_a+reads_b,writes_a+writes_b,checks_a+checks_b);
        $finish;
    end
    initial begin #20000000;$fatal(1,"slowram timeout");end
endmodule
