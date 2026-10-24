// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_multitini_w5100s_bridge;
    reg clk=0; always #3.75 clk=~clk;
    reg resetn=0,run=0,access_valid=0,eth_int_n=1;
    reg [31:0] access_word=0;
    wire [31:0] status;wire access_error;
    wire [1:0] eth_a;wire eth_rd_n,eth_wr_n,eth_cs_n,eth_rst_n;
    wire [7:0] eth_d_o,eth_d_oe;
    reg [7:0] ram[0:32767];reg [15:0] pointer=0;reg[7:0] mr=0;
    wire [7:0] eth_d_i=eth_a==0 ? mr : eth_a==3 ? ram[pointer[14:0]] : 8'hff;
    multitini_w5100s_bridge #(.RESET_HOLD_US(1),.READY_WAIT_US(2)) dut(.*);
    integer clocks=0,last_setup=0,last_end=-999,last_start=0,bus_count=0;
    reg[1:0] old_a=0;reg[7:0] old_data=0;reg old_cs=1,old_wr=1,old_rst=0;
    always @(posedge clk) begin
        #1;clocks=clocks+1;
        if(eth_a!=old_a || eth_d_o!=old_data)last_setup=clocks;
        if(!eth_cs_n && old_cs)begin
            if(clocks-last_setup<3)$fatal(1,"setup below three clocks");
            if(clocks-last_end<5)$fatal(1,"recovery below five clocks");
            if(!eth_rst_n)$fatal(1,"access during reset");
            if(eth_rd_n==eth_wr_n)$fatal(1,"ambiguous strobe");
            last_start=clocks;
        end
        if(eth_cs_n && !old_cs && run)begin
            if(clocks-last_start!=16)$fatal(1,"strobe length %0d",clocks-last_start);
            last_end=clocks;bus_count=bus_count+1;
            if(!old_wr)begin
                if(eth_d_oe!=8'hff)$fatal(1,"write hold missing");
                case(eth_a)
                0:mr=eth_d_o;
                1:pointer[15:8]=eth_d_o;
                2:pointer[7:0]=eth_d_o;
                3:begin ram[pointer[14:0]]=eth_d_o;pointer=pointer+1;end
                endcase
            end else if(eth_a==3)pointer=pointer+1;
        end
        if(!run && (!eth_cs_n || !eth_rd_n || !eth_wr_n || eth_d_oe))
            $fatal(1,"RUN cancellation does not release bus");
        if(eth_rst_n && !old_rst && clocks<134)$fatal(1,"short reset hold");
        old_a=eth_a;old_data=eth_d_o;old_cs=eth_cs_n;old_wr=eth_wr_n;old_rst=eth_rst_n;
    end
    task tick;begin @(negedge clk);end endtask
    task ready;integer timeout;begin
        timeout=0;while(!status[0])begin tick();timeout=timeout+1;if(timeout>1000)$fatal(1,"READY timeout");end
    end endtask
    task access(input bit wr,input[14:0]addr,input[7:0]data,input[7:0]expected);
        reg[7:0] seq;integer timeout;
        begin
            ready();seq=status[23:16];access_word={7'd0,wr,data,1'b0,addr};access_valid=1;tick();access_valid=0;
            timeout=0;while(status[23:16]==seq)begin tick();timeout=timeout+1;if(timeout>200)$fatal(1,"access timeout");end
            if(!status[2] || status[3])$fatal(1,"completion status %h",status);
            if(!wr && status[15:8]!=expected)$fatal(1,"read %h expected %h",status[15:8],expected);
        end
    endtask
    integer before_count,i;
    initial begin
        for(i=0;i<32768;i=i+1)ram[i]=i[7:0]^8'ha5;
        repeat(3)tick();resetn=1;run=1;ready();
        if(mr!=3 || bus_count!=1)$fatal(1,"indirect bootstrap missing");
        access(0,15'h1234,0,8'h91);before_count=bus_count;
        access(0,15'h1235,0,8'h90);if(bus_count!=before_count+1)$fatal(1,"sequential pointer not reused");
        access(1,15'h4567,8'hc9,0);access(0,15'h4567,0,8'hc9);
        access(1,0,0,0);access(0,0,0,3);
        access(0,15'h1234,0,8'h91);
        ready();before_count=bus_count;access_word=32'h00008000;access_valid=1;tick();access_valid=0;
        if(!access_error || !status[3] || bus_count!=before_count)$fatal(1,"invalid address not rejected");
        access(0,15'h7fff,0,8'h5a);access(0,15'h0001,0,8'ha4);
        eth_int_n=0;repeat(4)tick();if(!status[5])$fatal(1,"INT synchronizer");
        ready();access_word=32'h01c92222;access_valid=1;tick();access_valid=0;
        repeat(8)tick();run=0;tick();if(eth_rst_n||status[0]||status[23:16])$fatal(1,"reset cancellation");
        run=1;ready();if(status[23:16])$fatal(1,"stale completion after restart");
        $display("PASS W5100S bridge: reset, timing, indirect/random/sequential IO, hold, error, INT, cancellation");$finish;
    end
    initial begin #100000;$fatal(1,"watchdog");end
endmodule
