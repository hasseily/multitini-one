// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Production REU and verbatim production arbitration process. BA is prescribed
// independently; actual VIC BA generation has its own VICE differential suite.
module bus_timing;
  reg clk=0; always #5 clk=!clk;
  reg reset=1,enable=1,io_valid=0,io_write=0,ff00_write=0;
  reg [4:0] io_addr=0,phase=0;
  reg [7:0] io_wdata=0;
  wire [7:0] io_rdata;
  wire dma_req,dma_valid,dma_write,dma_idle,dma_peek,dma_last_write,dma_ack;
  wire [15:0] dma_addr; wire [7:0] dma_wdata,dma_rdata;
  wire [23:0] memory_addr,memory_hint_addr;
  wire memory_write,memory_valid,memory_hint_read;
  wire [63:0] memory_wdata,memory_rdata;wire [7:0] memory_wstrb;
  reg slow_tail=0,tail_seen=0,response_held=0;integer response_delay=0;
  wire memory_ready=!response_held;
  wire memory_rvalid=response_held ? response_delay==0 : memory_valid&&!(slow_tail&&tail_seen);
  wire memory_fault=0;wire irq,busy,fault;
  wire owned,active,access_valid;
  integer frame=0,first=-1,case_id=0,ba_start=-1,ba_end=-1,op=0,count=0,mismatch=-1;
  wire ba=first<0 || frame-first<ba_start || frame-first>=ba_end;
  reg [7:0] c64[0:65535],ram[0:65535];
  assign dma_rdata=c64[dma_addr];
  for(genvar g=0;g<8;g=g+1)assign memory_rdata[g*8+:8]=ram[(memory_addr[15:0]+g)&65535];
  multitini_c64_reu dut(.*);
  production_arbiter arb(.clk32(clk),.reset(reset),.phase(phase),.baLoc(ba),.ba_reu(ba),
    .dma_req(dma_req),.dma_valid(dma_valid),.dma_idle(dma_idle),.dma_peek(dma_peek),.dma_last_write(dma_last_write),
    .dma_we(dma_write),.dma_ack(dma_ack),.owned(owned),.active(active),.access_valid(access_valid));
  integer trace,acks=0;
  always @(posedge clk)begin
    phase<=phase+1'b1;
    if(phase==31)frame<=frame+1;
    if(reset)begin tail_seen<=0;response_held<=0;response_delay<=0;end
    else begin
      if(dma_ack&&dma_peek)tail_seen<=1;
      if(memory_valid&&memory_ready&&slow_tail&&tail_seen)begin response_held<=1;response_delay<=40;end
      else if(response_held)begin if(response_delay==0)response_held<=0;else response_delay<=response_delay-1;end
      if(tail_seen&&response_held&&access_valid)$fatal(1,"Delayed tail issued another machine access");
    end
    if(memory_valid&&memory_ready&&memory_write)for(integer j=0;j<8;j=j+1)if(memory_wstrb[j])ram[(memory_addr[15:0]+j)&65535]<=memory_wdata[j*8+:8];
    if(dma_ack&&!dma_idle)begin
      if(first<0)first=frame;
      $fdisplay(trace,"%0d,%0d,%s,%04x,%02x",case_id,frame-first,dma_write?"W":"R",dma_addr,dma_write?dma_wdata:dma_rdata);
      if(dma_write)c64[dma_addr]<=dma_wdata;
      acks=acks+1;
    end
    if(frame>10000)$fatal(1,"timeout");
  end
  task wr(input integer a,input integer v);
    @(negedge clk);io_valid=1;io_write=1;io_addr=a;io_wdata=v;
    @(negedge clk);io_valid=0;io_write=0;
  endtask
  task run_case(input integer id,input integer operation,input integer length,input integer bad,input integer steal_begin,input integer steal_end);
    reset=1;repeat(3)@(negedge clk);first=-1;acks=0;case_id=id;op=operation;count=length;mismatch=bad;ba_start=steal_begin;ba_end=steal_end;
    for(integer j=0;j<65536;j=j+1)begin c64[j]=j^(j>>8)^8'h5a;ram[j]=j^(j>>8)^8'ha4;end
    for(integer j=0;j<length;j=j+1)ram[j]=c64[16'h2000+j]^(j==bad ? 1:0);
    reset=0;wr(2,0);wr(3,8'h20);wr(4,0);wr(5,0);wr(6,1);wr(7,length);wr(8,0);wr(1,8'h90|operation);
    wait(dma_req);wait(first>=0);wait(!dma_req);wait(!active);io_addr=0;#1;
    $fdisplay(trace,"%0d,%0d,END,0000,%02x",case_id,frame-first,io_rdata);
    repeat(35)@(negedge clk);
  endtask
  initial begin
    trace=$fopen("rtl.csv","w");$fdisplay(trace,"case,cycle,access,address,data");
    slow_tail=$test$plusargs("SLOW_TAIL");
    if(slow_tail)begin
      run_case(2200,3,2,0,-1,-1);
      if(acks!=2)$fatal(1,"Delayed tail repeated ACK count=%0d",acks);
      $display("PASS: delayed verify tail no duplicate ACK or bus access");$fclose(trace);$finish;
    end
    for(integer o=0;o<4;o=o+1)for(integer n=1;n<=6;n=n+1)if(n<=3||n==6)
      for(integer b=1;b<=8;b=b+1)if(b<=3||b==5||b==8)run_case(1000+o*100+n*10+b,o,n,-1,b,b+3);
    for(integer n=2;n<=5;n=n+1)if(n!=4)for(integer b=0;b<n;b=b+1)
      for(integer t=-1;t<=5;t=t+1)if(t==-1||t==1||t==2||t==3||t==5)run_case(2000+n*100+b*10+t+1,3,n,b,t,t<0?-1:t+3);
    $fclose(trace);$display("PASS: VICE bus timing");$finish;
  end
endmodule
