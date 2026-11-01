// Original shared-port AXI timing fixture. SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_amiga_ide_block_axi;
    reg control_clk=0,clk=0,resetn=0;
    always #3.759 control_clk=~control_clk;
    always #17.621 clk=~clk;
    reg [31:0] awaddr=0,wdata=0,araddr=0;
    reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
    wire awready,wready,bvalid,arready,rvalid;
    wire [1:0] bresp,rresp;
    wire [31:0] rdata;
    amiga_ide_block_top_slice dut(.*);
    reg [31:0] expected[0:127];
    integer collisions=0,reads=0,writes=0;
    always @(posedge control_clk)if(resetn)begin
        if(dut.reg_wr_en && dut.amiga_block_wr_select && dut.reg_wr_addr>=16'hd200)begin
            expected[dut.reg_wr_addr[8:2]]<=dut.reg_wr_data;writes<=writes+1;
        end
        if(arvalid && !arready && dut.reg_wr_en && dut.amiga_block_wr_select && dut.reg_wr_addr>=16'hd200)
            collisions<=collisions+1;
    end
    task send_write(input integer index,input [31:0] value,input integer hold_response);
        integer n;
        begin
            @(negedge control_clk);awaddr=32'hd200+index*4;wdata=value;awvalid=1;wvalid=1;
            n=0;do begin @(posedge control_clk);n=n+1;if(n>1000)$fatal(1,"AXI AW/W timeout");end while(!(awready && wready));
            @(negedge control_clk);awvalid=0;wvalid=0;
            n=0;while(!bvalid)begin @(posedge control_clk);#1;n=n+1;if(n>1000)$fatal(1,"AXI B timeout");end
            repeat(hold_response)begin @(posedge control_clk);#1;if(!bvalid || bresp!=0)$fatal(1,"AXI B response changed under backpressure");end
            @(negedge control_clk);bready=1;
            @(posedge control_clk);#1;
            @(negedge control_clk);bready=0;
        end
    endtask
    task checked_read(input integer index,input integer hold_response);
        integer n;reg [31:0] want;
        begin
            @(negedge control_clk);araddr=32'hd200+index*4;arvalid=1;
            n=0;do begin @(posedge control_clk);n=n+1;if(n>1000)$fatal(1,"AXI AR timeout");end while(!arready);
            want=expected[index];
            @(negedge control_clk);arvalid=0;
            n=0;while(!rvalid)begin @(posedge control_clk);#1;n=n+1;if(n>1000)$fatal(1,"AXI R timeout");end
            if(rresp!=0 || rdata!==want)$fatal(1,"AXI stage read index%0d expected%h got%h",index,want,rdata);
            repeat(hold_response)begin
                @(posedge control_clk);#1;
                if(!rvalid || rresp!=0 || rdata!==want)$fatal(1,"AXI RDATA changed under backpressure");
            end
            @(negedge control_clk);rready=1;
            @(posedge control_clk);#1;
            @(negedge control_clk);rready=0;reads=reads+1;
        end
    endtask
    task collision(input integer write_index,input integer read_index,input [31:0] value);
        integer n;reg [31:0] want;
        begin
            // AR becomes valid on the cycle after AW/W acceptance, exactly
            // when the registered aperture write commits to the shared RAM
            // address port. The production top must defer that AR prime.
            @(negedge control_clk);awaddr=32'hd200+write_index*4;wdata=value;awvalid=1;wvalid=1;
            do @(posedge control_clk);while(!(awready && wready));
            @(negedge control_clk);awvalid=0;wvalid=0;araddr=32'hd200+read_index*4;arvalid=1;
            n=0;do begin @(posedge control_clk);n=n+1;if(n>1000)$fatal(1,"collision AR timeout");end while(!arready);
            want=expected[read_index];
            @(negedge control_clk);arvalid=0;
            while(!rvalid)begin @(posedge control_clk);#1;end
            if(rresp!=0 || rdata!==want)$fatal(1,"AXI stage read collision index%0d expected%h got%h",read_index,want,rdata);
            repeat(4)begin @(posedge control_clk);#1;if(rdata!==want || !rvalid)$fatal(1,"collision stalled RDATA changed");end
            @(negedge control_clk);rready=1;bready=1;
            @(posedge control_clk);#1;
            @(negedge control_clk);rready=0;bready=0;reads=reads+1;
        end
    endtask
    integer n;
    initial begin
        repeat(12)@(posedge control_clk);@(negedge control_clk);resetn=1;
        repeat(100)@(posedge control_clk);
        for(n=0;n<128;n=n+1)send_write(n,32'h35178ac9^(n*32'h1020107),n%4);
        // Every address in a nonsequential permutation, including both ends.
        for(n=0;n<128;n=n+1)checked_read((n*73+127)%128,n%6);
        for(n=0;n<24;n=n+1)collision(n,127-n,32'hfedc0000+n);
        // Writes may proceed while a previously captured RDATA is stalled;
        // the accepted read value must remain immutable through RREADY.
        fork
            begin for(integer k=0;k<16;k=k+1)checked_read((k*11)%128,12);end
            begin for(integer k=0;k<16;k=k+1)send_write(64+k,32'hdead0000+k,3);end
        join
        checked_read(0,0);checked_read(127,7);
        if(collisions<24 || reads!=170 || writes!=168)$fatal(1,"AXI coverage missing collisions%0d reads%0d writes%0d",collisions,reads,writes);
        $display("PASS AMIGA BULK IDE AXI productiontopguard/earlyRAMprime firstlast/random/24collisions/backpressure170reads168writes");$finish;
    end
    initial begin #3000000;$fatal(1,"AXI block watchdog");end
endmodule
