// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_multitini_audio_capture;
    reg clk=0,resetn=0,debug_enable=0,run=0,clear=0,sample_tick=0,pop=0;
    reg signed[23:0]sample_l=0,sample_r=0;
    wire[31:0]data,status;
    always #5 clk=~clk;
    multitini_audio_capture dut(.*);
    reg[31:0]expected[512];
    integer head=0,tail=0,count=0,checks=0,pushes=0,pops=0;
    reg overflow=0;
    reg take,put;
    reg[31:0]rng=32'h493772ac;
    always @(posedge clk)begin
        take=pop&&debug_enable&&count!=0;
        put=debug_enable&&run&&sample_tick&&(count!=512||take);
        if(debug_enable&&count!=0 && data!==expected[head])$fatal(1,"FIFO head ordering");
        if(!resetn||clear||!debug_enable)begin head=0;tail=0;count=0;overflow=0;end
        else begin
            if(take)begin head=(head+1)%512;count--;pops++;end
            if(put)begin expected[tail]={sample_l[23:8],sample_r[23:8]};tail=(tail+1)%512;count++;pushes++;end
            if(run&&sample_tick&&!put)overflow=1;
        end
        #1;
        if(status[9:0]!==10'(count)||status[10]!==overflow)$fatal(1,"FIFO count/overflow");
        if((!debug_enable||count==0)&&data!==0)$fatal(1,"disabled/empty data must be zero");
        if(debug_enable&&count!=0&&data!==expected[head])$fatal(1,"prefetched head or simultaneous push/pop");
        checks++;
    end
    task tick;
        @(negedge clk);rng={rng[30:0],rng[31]^rng[21]^rng[1]^rng[0]};sample_l=rng[23:0];sample_r=~rng[23:0];
    endtask
    initial begin
        repeat(4)tick();resetn=1;run=1;sample_tick=1;pop=1;
        repeat(20)tick();debug_enable=1;pop=0;
        repeat(520)tick(); // Full and overflow preserve all existing samples.
        pop=1;repeat(600)tick(); // Full simultaneous push/pop and wrap.
        sample_tick=0;repeat(520)tick();
        sample_tick=1;repeat(20)tick(); // Single-element bypass.
        for(integer i=0;i<5000;i++)begin
            tick();sample_tick=rng[0];pop=rng[1];run=rng[2]||rng[3];
            clear=(i%177)==0;debug_enable=(i%211)>3;
        end
        tick();clear=0;run=0;sample_tick=0;pop=1;debug_enable=1;repeat(520)tick();
        if(count!=0||pushes<1100||pops<1000)$fatal(1,"capture coverage missing");
        $display("PASS audio_capture checks=%0d pushes=%0d pops=%0d",checks,pushes,pops);$finish;
    end
endmodule
