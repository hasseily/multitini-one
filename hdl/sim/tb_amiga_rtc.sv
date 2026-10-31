// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Real stock/accelerated CPUs execute the SysInfo-style masked RTC read loop.
// Only the reset timer and one-second divider are shortened/seeded. The native
// RTC logic, MMIO bridge, CPU, clock gate and production memory are unmodified.
// Packed timestamp vectors and rollover expectations come from the interface
// contract, independently of the generated RTL implementation.
module tb_amiga_rtc #(parameter integer CPU_MODE=0);
    reg clk=0;always #17.62 clk=~clk;
    reg reset=1,guest_reset=1,pause_native=0;
    wire chip_clk,chip_tick,c1,c3,en7,en7n,run_enable;
    wire [23:1] chip_addr;
    wire [15:0] chip_wdata,chip_wdata2,chip_rdata,chip_rdata2;
    wire [47:0] chip_rdata48;
    wire chip_oe_n,chip_we_n,chip_ub_n,chip_lb_n,chip_long;
    wire cpu_req,cpu_ub_n,cpu_lb_n,cpu_long,cpu_ready,cpu_reset_n;
    wire [28:1] cpu_addr;
    wire [15:0] cpu_wdata,cpu_wdata2,cpu_rdata,cpu_rdata2;
    wire [1:0] cpu_state;
    reg io_uio=0,io_strobe=0;reg [15:0] io_din=0;
    wire [4:0] cpucfg;
    wire [23:0] memory_addr;
    wire memory_write,memory_valid,memory_busy,memory_fault;
    wire [63:0] memory_wdata;
    wire [7:0] memory_wstrb;
    reg [63:0] response_data=0;
    reg response_valid=0,response_busy=0;
    reg [7:0] response_delay=0;
    reg [23:0] held_addr;
    reg held_write;
    reg [63:0] held_data;
    reg [7:0] held_mask;
    reg [63:0] chip_store=0,fast_store=0,blit_store=0,repeat_store=0;
    reg[64:0] test_rtc=0;
    integer rtc_reads=0;
    always @(posedge chip_clk)if(dut.chipset.cpu_rd && dut.chipset.sel_rtc && dut.chipset.clk7_en)rtc_reads=rtc_reads+1;
    integer transactions=0,holds=0,cycles;
    integer completions=0;
    always @(posedge chip_clk)if(cpu_req && cpu_ready && completions<48)begin
        completions=completions+1;
        $display("CPU completion addr=%h state=%b rdata=%h din=%h udslds=%b%b",{cpu_addr,1'b0},cpu_state,cpu_rdata,dut.cpu.cpu_din,cpu_ub_n,cpu_lb_n);
    end
    always @(posedge chip_clk)if(CPU_MODE==3 && dut.cpu.clkena_p_throttled && dut.cpu.cpu_req && completions<48)begin
        completions=completions+1;
        $display("TG consume addr=%h state=%b din=%h chipaddr=%h chipdata=%h ready=%b",dut.cpu.cpu_addr,cpu_state,dut.cpu.cpu_din,{chip_addr,1'b0},chip_rdata,dut.cpu.chipready);
    end

    multitini_amiga_machine dut(
        .clk_service28(clk),.reset(reset),.guest_reset(guest_reset),.run_enable(run_enable && !pause_native),
        .clk_chip(chip_clk),.chip_tick(chip_tick),.chip_c1(c1),.chip_c3(c3),.chip_clk7_en(en7),.chip_clk7n_en(en7n),
        .chip_addr(chip_addr),.chip_wdata(chip_wdata),.chip_wdata2(chip_wdata2),
        .chip_oe_n(chip_oe_n),.chip_we_n(chip_we_n),.chip_ub_n(chip_ub_n),.chip_lb_n(chip_lb_n),.chip_long(chip_long),
        .chip_rdata(chip_rdata),.chip_rdata2(chip_rdata2),.chip_rdata48(chip_rdata48),
        .cpu_req(cpu_req),.cpu_addr(cpu_addr),.cpu_wdata(cpu_wdata),.cpu_wdata2(cpu_wdata2),
        .cpu_state(cpu_state),.cpu_ub_n(cpu_ub_n),.cpu_lb_n(cpu_lb_n),.cpu_long(cpu_long),
        .cpu_rdata(cpu_rdata),.cpu_rdata2(cpu_rdata2),.cpu_ready(cpu_ready),.cpu_ready_long(cpu_ready),
        .io_uio(io_uio),.io_fpga(1'b0),.io_strobe(io_strobe),.io_din(io_din),.io_wait(),.io_dout(),
        .ide_address(5'd0),.ide_read(1'b0),.ide_write(1'b0),.ide_wdata(16'd0),
        .joy1(16'hffff),.joy2(16'hffff),.mouse_buttons(3'd0),.kms_level(1'b0),
        .kbd_mouse_type(2'd0),.kbd_mouse_data(8'd0),.rtc(test_rtc),.cpucfg(cpucfg),.cpu_reset_n(cpu_reset_n));
    multitini_amiga_memory memory_i(
        .clk(clk),.reset(reset),.chip_c1(c1 && !guest_reset),.chip_addr(chip_addr),
        .chip_wdata(chip_wdata),.chip_wdata2(chip_wdata2),.chip_oe_n(chip_oe_n),.chip_we_n(chip_we_n),
        .chip_ub_n(chip_ub_n),.chip_lb_n(chip_lb_n),.chip_long(chip_long),
        .chip_rdata(chip_rdata),.chip_rdata2(chip_rdata2),.chip_rdata48(chip_rdata48),
        .cpu_req(cpu_req && cpu_reset_n && !guest_reset),.cpu_addr(cpu_addr),.cpu_wdata(cpu_wdata),.cpu_wdata2(cpu_wdata2),
        .cpu_state(cpu_state),.cpu_ub_n(cpu_ub_n),.cpu_lb_n(cpu_lb_n),.cpu_long(cpu_long),
        .cpu_rdata(cpu_rdata),.cpu_rdata2(cpu_rdata2),.cpu_ready(cpu_ready),
        .load_valid(1'b0),.load_addr(24'd0),.load_data(8'd0),.load_ready(),.load_done(),
        .run_enable(run_enable),.busy(memory_busy),.fault(memory_fault),
        .memory_addr(memory_addr),.memory_write(memory_write),.memory_wdata(memory_wdata),.memory_wstrb(memory_wstrb),
        .memory_valid(memory_valid),.memory_ready(!response_busy),.memory_rdata(response_data),
        .memory_rvalid(response_valid),.memory_fault(1'b0));

    function automatic [15:0] romword(input [19:0] position);
        case(position)
            0:romword=16'h001f;
            1:romword=16'hf000;
            2:romword=16'h00f8;
            3:romword=16'h0008;
            4:romword=16'h13fc;
            5:romword=16'h0002;
            6:romword=16'h00bf;
            7:romword=16'he001;
            8:romword=16'h46fc;
            9:romword=16'h2700;
            10:romword=16'h45f9;
            11:romword=16'h00dc;
            12:romword=16'h0001;
            13:romword=16'h740f;
            14:romword=16'hc412;
            15:romword=16'h0c02;
            16:romword=16'h0009;
            17:romword=16'h67f6;
            18:romword=16'h13fc;
            19:romword=16'h0007;
            20:romword=16'h00dc;
            21:romword=16'h0001;
            22:romword=16'h13fc;
            23:romword=16'h000f;
            24:romword=16'h00dc;
            25:romword=16'h003d;
            26:romword=16'h23fc;
            27:romword=16'h5041;
            28:romword=16'h5353;
            29:romword=16'h0000;
            30:romword=16'h1000;
            31:romword=16'h60fe;
            default:romword=16'h4e71;
        endcase
    endfunction
    function automatic [63:0] read_line(input [23:0] address);
        reg [15:0] word_value;integer n;
        begin
            read_line=0;
            if(address==24'h001000)read_line=chip_store;
            else if(address==24'h001008)read_line=blit_store;
            else if(address==24'h001010)read_line=repeat_store;
            else if(address==24'ha00000)read_line=fast_store;
            else if(address>=24'h780000 && address<24'h800000)
                for(n=0;n<4;n=n+1)begin
                    word_value=romword((address-24'h780000)/2+n);
                    read_line[16*n+:16]={word_value[7:0],word_value[15:8]};
                end
        end
    endfunction
    integer lane;
    always @(posedge clk)begin
        response_valid<=0;
        if(!reset && !run_enable)holds<=holds+1;
        if(response_busy)begin
            if(response_delay!=0)response_delay<=response_delay-1'b1;
            else begin
                if(held_write)begin
                    for(lane=0;lane<8;lane=lane+1)if(held_mask[lane])begin
                        if(held_addr==24'h001000)chip_store[lane*8+:8]<=held_data[lane*8+:8];
                        else if(held_addr==24'h001008)blit_store[lane*8+:8]<=held_data[lane*8+:8];
                        else if(held_addr==24'h001010)repeat_store[lane*8+:8]<=held_data[lane*8+:8];
                        else if(held_addr==24'ha00000)fast_store[lane*8+:8]<=held_data[lane*8+:8];
                    end
                end
                response_data<=read_line(held_addr);response_valid<=1;response_busy<=0;
            end
        end else if(memory_valid)begin
            if(transactions<40)$display("DDR tx%0d %h write=%b mask=%h data=%h",transactions,memory_addr,memory_write,memory_wstrb,memory_wdata);
            held_addr<=memory_addr;held_write<=memory_write;held_data<=memory_wdata;held_mask<=memory_wstrb;
            response_delay<=4+(transactions%13);response_busy<=1;transactions<=transactions+1;
        end
    end
    task automatic config_cpu;
        @(negedge chip_clk);io_uio=1;io_din=16'h00f4;io_strobe=1;
        @(negedge chip_clk);io_strobe=0;
        @(negedge chip_clk);io_din=CPU_MODE==0 ? 16'h0000 : 16'h001f;io_strobe=1;
        @(negedge chip_clk);io_strobe=0;
        repeat(8)@(negedge chip_clk);io_uio=0;
        repeat(8)@(negedge chip_clk);
    endtask
    localparam [63:0] STAMP58=64'h4006261031235858;
    localparam [63:0] STAMP59=64'h4006261031235859;
    localparam [63:0] STAMP07=64'h4000261101000007;
    localparam [63:0] STAMP12=64'h4000261101000012;
    integer native_edges=0,paused_edges;
    reg [31:0] paused_divider;
    reg [63:0] paused_time;
    always @(posedge chip_clk)native_edges=native_edges+1;
    task automatic timestamp(input [63:0] value);
        @(negedge clk);test_rtc={~test_rtc[64],value};
        repeat(8)@(negedge clk);
        if(dut.chipset.rtc_reg!==value)$fatal(1,"RTC full timestamp bits changed: expected%h got%h",value,dut.chipset.rtc_reg);
    endtask
    task automatic second_boundary(input [7:0] expected);
        @(negedge chip_clk);dut.chipset.rtc_count=32'd28375159;
        @(negedge chip_clk);
        if(dut.chipset.rtc_reg!=={STAMP58[63:8],expected})
            $fatal(1,"RTC seconds fallback mismatch expected%h got%h",expected,dut.chipset.rtc_reg);
    endtask
    initial begin
        repeat(8)@(negedge clk);reset=0;
        repeat(32)@(negedge chip_clk);config_cpu();
        if(dut.chipset.rtc_reg!==64'd0)$fatal(1,"RTC power-on state is not deterministic");
        timestamp(STAMP58);
        second_boundary(8'h59);second_boundary(8'h00);second_boundary(8'h01);
        $display("PASS nativeRTC mode%0d complete timestamp and58-59-00-01 fallback preserve calendar/control bytes",CPU_MODE);
        timestamp(STAMP59);
        force dut.chipset.CONTROL1.rst_cnt=3'd4;guest_reset=0;
        for(cycles=0;cycles<100000 && rtc_reads<80;cycles=cycles+1)@(negedge clk);
        if(rtc_reads<80 || chip_store[31:0]==32'h53534150)$fatal(1,"Actual guest did not pollRTC seconds9");
        // Stopping the entire native timeline also stops the RTC divider.
        @(negedge clk);pause_native=1;repeat(4)@(negedge clk);
        paused_edges=native_edges;paused_divider=dut.chipset.rtc_count;paused_time=dut.chipset.rtc_reg;
        repeat(64)@(negedge clk);
        if(native_edges!=paused_edges || dut.chipset.rtc_count!==paused_divider || dut.chipset.rtc_reg!==paused_time)
            $fatal(1,"NativeRTC advanced during a native-clock pause");
        pause_native=0;repeat(8)@(negedge chip_clk);
        // No host refresh: the next native second must release the exact
        // masked read/compare/branch opcodes used by SysInfo3.24.
        second_boundary(8'h00);
        for(cycles=0;cycles<100000 && chip_store[31:0]!=32'h53534150;cycles=cycles+1)@(negedge clk);
        if(chip_store[31:0]!=32'h53534150)$fatal(1,"SysInfoRTC poll did not escape after59 rollover");
        if(dut.chipset.rtc_reg!=={STAMP58[63:8],8'h00})
            $fatal(1,"GuestRTC seconds or mode write corrupted the host-owned timestamp");
        $display("PASS actual CPU mode%0d SysInfo poll escapes on59 rollover; guest seconds/mode writes stay harmless",CPU_MODE);
        // An update presented while native clocks are held must wait for
        // native acceptance and then preserve every byte, including seconds.
        @(negedge clk);pause_native=1;repeat(4)@(negedge clk);
        paused_time=dut.chipset.rtc_reg;test_rtc={~test_rtc[64],STAMP07};
        repeat(32)@(negedge clk);
        if(dut.chipset.rtc_reg!==paused_time)$fatal(1,"RTC consumed a timestamp without a native edge");
        pause_native=0;repeat(8)@(negedge chip_clk);
        if(dut.chipset.rtc_reg!==STAMP07)$fatal(1,"RTC paused update lost timestamp bits");
        // Data without a toggle is not a new timestamp. A simultaneous
        // refresh/second boundary is resolved in favor of the host snapshot.
        @(negedge clk);test_rtc[63:0]=STAMP12;repeat(8)@(negedge chip_clk);
        if(dut.chipset.rtc_reg!==STAMP07)$fatal(1,"RTC loaded data before its toggle");
        @(negedge chip_clk);dut.chipset.rtc_count=32'd28375159;test_rtc[64]=~test_rtc[64];
        @(negedge chip_clk);
        if(dut.chipset.rtc_reg!==STAMP12 || dut.chipset.rtc_count!==0)$fatal(1,"RTC refresh did not win at a second boundary");
        guest_reset=1;repeat(32)@(negedge chip_clk);
        if(dut.chipset.rtc_reg!==STAMP12)$fatal(1,"Guest reset discardedRTC state");
        $display("PASS nativeRTC mode%0d pause/resume, complete updates, toggle-only commit and guest-reset persistence",CPU_MODE);
        $finish;
    end
endmodule
module tb_amiga_rtc_stock;tb_amiga_rtc #(.CPU_MODE(0)) test();endmodule
module tb_amiga_rtc_turbo;tb_amiga_rtc #(.CPU_MODE(3)) test();endmodule
