// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Exercise the actual derived machine's lossless opcode-event bridge. The
// primary CPU output boundary is forced to supply isolated accepted fetches;
// P65 instruction correctness is qualified by its separate independent suite.
module tb_gs_native_event;
    reg clk=0;always #8.730 clk=~clk;
    reg [1:0] phase=0;reg native_div=0;
    always @(posedge clk)begin phase<=phase+1'b1;native_div<=(phase==3 || phase==0);end
    wire delayed_native;assign #0.001 delayed_native=native_div;
    bit use_delay;initial use_delay=$test$plusargs("DELAYED_NATIVE");
    wire native_clk=use_delay ? delayed_native : native_div;
    reg reset=1,ce=0,native_commit=0;
    reg [23:0] forced_addr=0;reg forced_vpa=0,forced_vda=0;
    reg forced_blank=1,forced_we_n=1;reg [7:0] forced_data=0;
    iigs #(.EXTERNAL_CPU(1)) dut(.reset(reset),.cold_reset(reset),
        .CPU_CLK(clk),.CPU_CE(ce),.CPU_PUBLISH(phase==0),.CPU_DIN(8'hEA),.NATIVE_COMMIT(native_commit),
        .CLK_14M(native_clk),.CLK_28M(1'b0),.clk_vid(clk),.ce_pix(phase==3),
        .load_rom_clk(clk),.load_rom_we(1'b0),.load_rom_addr(16'b0),.load_rom_data(8'b0),
        .top_din(8'hEA),.mem_stall(1'b0),.host_speed(3'd6),.accel_capable(1'b1),.zip_regs_en(1'b1),
        .osd_spkr_delay(1'b0),.osd_pdl_delay(1'b0),.osd_ctr_delay(1'b0),.osd_cps_follow(1'b0),.osd_irq_delay(1'b0),
        .twgs_present(1'b0),.rom_select(1'b0),.timestamp(33'b0),.ps2_key(11'b0),.ps2_mouse(25'b0),
        .nv_wr(1'b0),.nv_addr(9'b0),.nv_din(8'b0),.HDD_MOUNTED(2'b0),.HDD_ACK(2'b0),
        .HDD_RAM_WE(1'b0),.HDD_RAM_ADDR(9'b0),.HDD_RAM_DI(8'b0),.img_size(64'b0),.img_readonly(1'b1),
        .UART_RXD(1'b1),.UART_CTS(1'b0),.serial_loopback(1'b0),.joystick_0(32'b0),.joystick_1(32'b0),
        .joystick_l_analog_0(16'b0),.joystick_l_analog_1(16'b0));
    integer publications=0,cases=0;
    always @(posedge native_clk)if(!reset && dut.cpu_isr_vbl_published)publications++;
    task set_inten(input [7:0] value);
        while(phase!=2)@(negedge clk);
        forced_addr=24'h00C041;forced_vpa=0;forced_vda=1;forced_we_n=0;forced_data=value;native_commit=1;
        repeat(2)@(negedge clk);
        native_commit=0;forced_addr=0;forced_vda=0;forced_we_n=1;
        if(dut.INTEN!==value)$fatal(1,"native INTEN setup write failed");
    endtask
    task clean;
        @(negedge clk);reset=1;ce=0;native_commit=0;forced_addr=0;forced_vpa=0;forced_vda=0;
        forced_we_n=1;forced_blank=1;
        repeat(12)@(negedge clk);reset=0;repeat(8)@(negedge clk);set_inten(8'h08);publications=0;
        if(dut.irq_pending[3] || dut.cpu_isr_vbl_accum || dut.cpu_isr_vbl_published)$fatal(1,"event reset did not clear");
    endtask
    task pulse(input integer target_phase,input bit accepted);
        while(phase!=target_phase)@(negedge clk);
        ce=accepted;forced_vpa=1;forced_vda=1;forced_addr=24'hFEFC3A;
        @(negedge clk);ce=0;forced_vpa=0;forced_vda=0;forced_addr=0;
    endtask
    task expect_after(input bit expected,input integer count);
        repeat(12)@(negedge clk);
        if(dut.irq_pending[3]!==expected || publications!=count)
            $fatal(1,"native event mismatch pending=%b expected=%b publications=%0d expected=%0d",dut.irq_pending[3],expected,publications,count);
        cases++;
    endtask
    task clear_native;
        while(phase!=2)@(negedge clk);
        forced_addr=24'h00C047;forced_vpa=0;forced_vda=1;native_commit=1;
        @(negedge clk); // parent phase3 edge has generated the native commit
        @(negedge clk);native_commit=0;forced_addr=0;forced_vda=0;
        repeat(8)@(negedge clk);
        if(dut.irq_pending[3])$fatal(1,"C047 native clear failed");
    endtask
    initial begin
        force dut.cpu_addr=forced_addr;
        force dut.cpu_vpa=forced_vpa;
        force dut.cpu_vda=forced_vda;
        force dut.cpu_we_n=forced_we_n;
        force dut.cpu_dout=forced_data;
        force dut.VBlank=forced_blank;
        for(integer p=0;p<4;p++)begin
            clean();pulse(p,1);expect_after(1,1);
            $display("PASS: accepted ISR event phase=%0d native_delay=%0d",p,use_delay);
        end
        // Both events precede their own native consume edges; consecutive
        // publication windows cannot overwrite each other or drop phase3.
        clean();pulse(0,1);pulse(3,1);expect_after(1,2);
        clean();pulse(1,1);pulse(2,1);expect_after(1,1); // coalesced same window
        clean();pulse(3,0);expect_after(0,0); // presented but never accepted
        clean();set_inten(0);pulse(2,1);set_inten(8'h08);expect_after(0,0);
        clean();forced_blank=0;pulse(1,1);forced_blank=1;expect_after(0,0);
        clean();pulse(3,1);forced_blank=0;expect_after(1,1); // last VBlank tick
        clean();pulse(2,1);set_inten(0);expect_after(1,1); // enable falls later
        clean();pulse(0,1);expect_after(1,1);
        pulse(2,1); // already pending: must not survive the later C047 clear
        clear_native();expect_after(0,1);
        clean();pulse(1,1);reset=1;repeat(12)@(negedge clk);reset=0;publications=0;expect_after(0,0);
        $display("PASS: native ISR event total=%0d phases0..3/consecutive/coalesced/CE/eligibility/VBlank-boundary/clear/reset delay=%0d",cases,use_delay);
        $finish;
    end
endmodule
