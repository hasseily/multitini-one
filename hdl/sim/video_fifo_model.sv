// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Behavioral FIFO for directed simulations. Production uses Xilinx XPM; this
// model includes independent clocks, two-stage pointer visibility and reset busy.
module xpm_fifo_async #(
    parameter FIFO_MEMORY_TYPE="block", FIFO_WRITE_DEPTH=256,
    parameter WRITE_DATA_WIDTH=64, READ_DATA_WIDTH=64, READ_MODE="fwft",
    parameter FIFO_READ_LATENCY=0,CDC_SYNC_STAGES=2,DOUT_RESET_VALUE="0",
    parameter ECC_MODE="no_ecc",FULL_RESET_VALUE=0,PROG_FULL_THRESH=240,
    parameter PROG_EMPTY_THRESH=16,RD_DATA_COUNT_WIDTH=9,WR_DATA_COUNT_WIDTH=9,
    parameter RELATED_CLOCKS=0,SIM_ASSERT_CHK=1,USE_ADV_FEATURES="0000",WAKEUP_TIME=0
)(input rst,wr_clk,wr_en,input [WRITE_DATA_WIDTH-1:0]din,
    output full,output reg wr_rst_busy,output reg overflow,output prog_full,
    output [WR_DATA_COUNT_WIDTH-1:0]wr_data_count,output almost_full,output reg wr_ack,
    input rd_clk,rd_en,output [READ_DATA_WIDTH-1:0]dout,output empty,
    output reg rd_rst_busy,output reg underflow,output prog_empty,
    output [RD_DATA_COUNT_WIDTH-1:0]rd_data_count,output almost_empty,output data_valid,
    input sleep,injectsbiterr,injectdbiterr,output sbiterr,dbiterr);
    localparam RATIO=WRITE_DATA_WIDTH/READ_DATA_WIDTH;
    localparam DEPTH=FIFO_WRITE_DEPTH*RATIO;
    reg [READ_DATA_WIDTH-1:0] memory[0:DEPTH-1];
    integer wp=0,rp=0,rp_meta=0,rp_sync=0,wp_meta=0,wp_sync=0;
    integer wreset=3,rreset=3,k;
    assign full=wp-rp_sync>DEPTH-RATIO;
    assign empty=rp==wp_sync;
    assign dout=empty?0:memory[rp%DEPTH];
    assign wr_data_count=(wp-rp_sync)/RATIO,rd_data_count=wp_sync-rp;
    assign prog_full=wr_data_count>=PROG_FULL_THRESH,almost_full=wr_data_count>=FIFO_WRITE_DEPTH-1;
    assign prog_empty=rd_data_count<=PROG_EMPTY_THRESH,almost_empty=rd_data_count<=1;
    assign data_valid=!empty && !rd_rst_busy,sbiterr=0,dbiterr=0;
    initial begin wr_rst_busy=1;rd_rst_busy=1;overflow=0;underflow=0;wr_ack=0;end
    always @(posedge wr_clk or posedge rst)begin
        if(rst)begin wp<=0;rp_meta<=0;rp_sync<=0;wreset<=3;wr_rst_busy<=1;overflow<=0;wr_ack<=0;end
        else begin
            rp_meta<=rp;rp_sync<=rp_meta;overflow<=0;wr_ack<=0;
            if(wreset>0)wreset<=wreset-1;else wr_rst_busy<=0;
            if(wr_en && !wr_rst_busy)begin
                if(full)overflow<=1;
                else begin
                    for(k=0;k<RATIO;k=k+1)memory[(wp+k)%DEPTH]<=din[k*READ_DATA_WIDTH+:READ_DATA_WIDTH];
                    wp<=wp+RATIO;wr_ack<=1;
                end
            end
        end
    end
    always @(posedge rd_clk or posedge rst)begin
        if(rst)begin rp<=0;wp_meta<=0;wp_sync<=0;rreset<=3;rd_rst_busy<=1;underflow<=0;end
        else begin
            wp_meta<=wp;wp_sync<=wp_meta;underflow<=0;
            if(rreset>0)rreset<=rreset-1;else rd_rst_busy<=0;
            if(rd_en && !rd_rst_busy)begin
                if(empty)underflow<=1;else rp<=rp+1;
            end
        end
    end
endmodule
