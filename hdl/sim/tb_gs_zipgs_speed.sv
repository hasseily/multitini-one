// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_gs_zipgs_speed;
    reg clk=0;always #5 clk=~clk;
    reg reset=1,wr_stb=0;reg [2:0] wr_addr=0,rd_addr=0,host_speed=0;reg [7:0] wr_data=0;
    wire [7:0] rd_data[3];wire [2:0] speed_code[3];wire [2:0] accel_en,zip_unlocked;
    generate for(genvar i=0;i<3;i++)begin : cards
        zipgs_regs #(.MAX_SPEED_CODE(i+4)) dut(.clk(clk),.reset(reset),.wr_stb(wr_stb),.wr_addr(wr_addr),.wr_data(wr_data),
            .rd_addr(rd_addr),.rd_data(rd_data[i]),.mtr_slow(1'b0),.host_speed(host_speed),
            .host_spkr_en(1'b1),.host_pdl_en(1'b1),.host_ctr_en(1'b1),.host_cps_en(1'b0),.host_irq_en(1'b1),
            .zip_unlocked(zip_unlocked[i]),.accel_en(accel_en[i]),.speed_code(speed_code[i]),
            .cache_disable(),.slot_delay(),.spkr_delay_en(),.pdl_delay_en(),.ctr_delay_en(),.cps_follow_en(),.irq_delay_en());
    end endgenerate
    // Independent fixed table from floor((16-s)/16 * rated CPU MHz).
    function integer expected(input integer maximum,input integer s);
        if(maximum==4)begin
            if(s==0)expected=4;else if(s<=8)expected=3;else if(s<=10)expected=2;else if(s<=12)expected=1;else expected=0;
        end else if(maximum==5)begin
            if(s==0)expected=5;else if(s<=8)expected=4;else if(s<=12)expected=3;else if(s==13)expected=2;else if(s==14)expected=1;else expected=0;
        end else begin
            if(s==0)expected=6;else if(s<=8)expected=5;else if(s<=12)expected=4;else if(s<=14)expected=3;else expected=1;
        end
    endfunction
    task write_reg(input [2:0] a,input [7:0] d);
        @(negedge clk);wr_stb=1;wr_addr=a;wr_data=d;@(negedge clk);wr_stb=0;
    endtask
    initial begin
        repeat(4)@(negedge clk);reset=0;
        repeat(4)write_reg(2,'h50);
        if(zip_unlocked!==3'b111)$fatal(1,"Zip unlock failed");
        write_reg(3,0);
        for(integer s=0;s<16;s++)begin
            write_reg(5,8'(s<<4));
            for(integer i=0;i<3;i++)if(speed_code[i]!=expected(i+4,s))
                $fatal(1,"software percentage mismatch max=%0d nibble=%0d code=%0d expected=%0d",i+4,s,speed_code[i],expected(i+4,s));
        end
        write_reg(5,0);write_reg(2,0);
        if(accel_en!==0)$fatal(1,"software disable failed");
        write_reg(3,0);
        for(integer i=0;i<3;i++)if(speed_code[i]!=i+4)$fatal(1,"software 100%% did not restore installed maximum");
        for(integer h=1;h<=7;h++)begin
            @(negedge clk);host_speed=h[2:0];repeat(2)@(negedge clk);
            for(integer i=0;i<3;i++)if(speed_code[i]!=(h>i+4 ? i+4 : h))$fatal(1,"host capability clamp failed");
        end
        // Wall-clock counter remains CLK14 based; CPU rate never participates.
        rd_addr=3;
        begin reg old_toggle;integer elapsed;
            old_toggle=rd_data[2][7];elapsed=0;
            while(rd_data[2][7]==old_toggle)begin @(negedge clk);elapsed++;end
            old_toggle=rd_data[2][7];elapsed=0;
            while(rd_data[2][7]==old_toggle)begin @(negedge clk);elapsed++;end
            if(elapsed!=7331)$fatal(1,"Zip calibration clock changed: %0d",elapsed);
        end
        $display("PASS: ZipGS all48 percentage mappings, host0..7 capability clamp, unlock/disable/restore, native7331-tick calibration");
        $finish;
    end
endmodule
