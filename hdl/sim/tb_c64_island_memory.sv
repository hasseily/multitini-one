// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_island_memory;
    reg clk_cpu=0,clk_native=0;
    always #5 clk_cpu=~clk_cpu;
    always @(posedge clk_cpu) clk_native<=~clk_native;
    reg cpu_ce=0,cpu_ram_we=0,cpu_colour_we=0;
    reg [15:0] cpu_addr=0;
    reg [7:0] cpu_wdata=0;
    wire [7:0] ram_low_q,ram_high_q,rom_q,char_q;
    wire [3:0] colour_q;
    reg loader_we=0,machine_ce=0,machine_we=0,machine_store=0;
    reg [15:0] loader_addr=0,machine_addr=0;
    reg [7:0] loader_data=0,machine_data=0;
    wire [7:0] machine_q;
    reg peek_request=0,machine_paused=0;
    reg [15:0] peek_addr=0;
    wire peek_valid;
    wire [7:0] peek_data;
    reg [9:0] native_colour_addr=0;
    reg native_colour_we=0;
    reg [3:0] native_colour_data=0;
    wire [3:0] native_colour_q;
    reg [13:0] rom_load_addr=0;
    reg [7:0] rom_load_data=0;
    reg rom_load_basic_kernal=0,rom_load_char=0;
    multitini_c64_island_memory dut(.*);
    integer checks=0;
    reg [7:0] expected[0:65535];
    reg [7:0] held;
    function automatic [7:0] pattern(input integer a);
        pattern=((a*37)^(a>>8)^8'hb7)&255;
    endfunction
    task automatic check(input bit ok,input string why);
        checks=checks+1;
        if(!ok) $fatal(1,"MEMORY FAIL %s cpu=%04x machine=%04x q=%02x",why,cpu_addr,machine_addr,machine_q);
    endtask
    task automatic native_edge; @(posedge clk_native); #0.01; endtask
    task automatic cpu_edge; @(negedge clk_cpu); #0.01; endtask
    initial begin
        for(integer a=0;a<65536;a=a+1) begin
            @(negedge clk_native);loader_we=1;loader_addr=a;loader_data=pattern(a);
            expected[a]=pattern(a);native_edge();
        end
        @(negedge clk_native);loader_we=0;
        for(integer a=0;a<65536;a=a+1) begin
            @(posedge clk_cpu);cpu_ce=1;cpu_addr=a;
            cpu_edge();
            check((a<32768?ram_low_q:ram_high_q)===expected[a],"loader to CPU all addresses");
        end
        @(posedge clk_cpu);cpu_ce=0;
        for(integer a=0;a<65536;a=a+257) begin
            @(negedge clk_native);machine_ce=1;machine_we=0;machine_addr=a;
            native_edge();check(machine_q===expected[a],"native sparse read");
            held=machine_q;
            @(negedge clk_native);loader_we=1;loader_addr=a^16'h8000;loader_data=8'h62;
            expected[a^16'h8000]=8'h62;native_edge();
            check(machine_q===held,"loader cannot change guest read response");
            @(negedge clk_native);loader_we=0;machine_ce=0;
            native_edge();check(machine_q===held,"post-loader idle response");
        end
        @(negedge clk_native);machine_ce=1;machine_we=1;machine_store=0;
        machine_addr=16'h8001;machine_data=8'ha3;
        native_edge();check(machine_q===8'ha3,"mapper-blocked write echo");
        @(negedge clk_native);machine_we=0;
        native_edge();check(machine_q===expected[16'h8001],"mapper-blocked write not stored");
        @(negedge clk_native);machine_ce=0;
        for(integer a=0;a<65536;a=a+251) begin
            @(posedge clk_cpu);cpu_ce=1;cpu_ram_we=0;cpu_addr=a;
            cpu_edge();held=(a<32768?ram_low_q:ram_high_q);
            @(posedge clk_cpu);cpu_ram_we=1;cpu_wdata=pattern(a)^8'hd5;
            cpu_edge();check((a<32768?ram_low_q:ram_high_q)===held,"CPU write NO_CHANGE");
            expected[a]=cpu_wdata;
            @(posedge clk_cpu);cpu_ram_we=0;cpu_ce=0;
            @(negedge clk_native);machine_ce=1;machine_we=0;machine_addr=a;
            native_edge();check(machine_q===expected[a],"CPU write visible to native read");
            @(negedge clk_native);machine_ce=0;peek_request=1;machine_paused=0;peek_addr=a;
            native_edge();check(!peek_valid,"peek rejected while running");
            @(negedge clk_native);machine_paused=1;
            native_edge();check(peek_valid && peek_data===expected[a],"paused peek");
            @(negedge clk_native);peek_request=0;machine_paused=0;
        end
        // Load independent ROM copies, including all upper address bits.
        for(integer a=0;a<16384;a=a+1) begin
            @(negedge clk_native);rom_load_basic_kernal=1;rom_load_char=(a<4096);
            rom_load_addr=a;rom_load_data=pattern(a)^8'h59;
            native_edge();
        end
        @(negedge clk_native);rom_load_basic_kernal=0;rom_load_char=0;
        for(integer a=0;a<16384;a=a+1) begin
            @(posedge clk_cpu);cpu_ce=1;cpu_addr={1'b1,a[13],1'b1,a[12:0]};
            cpu_edge();check(rom_q===(pattern(a)^8'h59),"BASIC/KERNAL copy mapping");
            check(char_q===(pattern(a&4095)^8'h59),"character copy mapping");
        end
        @(posedge clk_cpu);cpu_ce=0;
        for(integer a=0;a<1024;a=a+1) begin
            @(negedge clk_native);native_colour_we=1;native_colour_addr=a;native_colour_data=a^5;
            native_edge();check(native_colour_q===((a^5)&15),"native colour WRITE_FIRST");
            @(negedge clk_native);native_colour_we=0;
            @(posedge clk_cpu);cpu_ce=1;cpu_addr=16'hd800+a;
            cpu_edge();check(colour_q===((a^5)&15),"native to CPU colour");
            @(posedge clk_cpu);cpu_colour_we=1;cpu_wdata=a^9;
            cpu_edge();check(colour_q===((a^5)&15),"CPU colour NO_CHANGE");
            @(posedge clk_cpu);cpu_ce=0;cpu_colour_we=0;
            native_edge();check(native_colour_q===((a^9)&15),"CPU to native colour");
            cpu_edge();check(colour_q===((a^9)&15),"colour prefetch without CPU grant");
        end
        $display("MEMORY PASS checks=%0d",checks);$finish;
    end
    initial begin #5000000;$fatal(1,"memory timeout");end
endmodule
