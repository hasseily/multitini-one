// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Dedicated IIgs RAM/ROM aperture on HP2. One outstanding transaction; AW
// and W are independent. Completion means DDR acknowledged the write.
module multitini_apple_iigs_ddr_path (
    input wire core_clk,core_resetn,control_clk,control_resetn,
    input wire [23:0] core_memory_addr,
    input wire core_memory_write,
    input wire [63:0] core_memory_wdata,
    input wire [7:0] core_memory_wstrb,
    input wire core_memory_valid,
    output wire core_memory_ready,
    output wire [63:0] core_memory_rdata,
    output wire core_memory_rvalid,
    output wire core_memory_fault,
    output wire quiescent,
    output wire [31:0] araddr,output wire [3:0] arlen,
    output wire [2:0] arsize,output wire [1:0] arburst,
    output wire arvalid,input wire arready,
    input wire [63:0] rdata,input wire [1:0] rresp,
    input wire rlast,rvalid,output wire rready,
    output wire [31:0] awaddr,output wire [3:0] awlen,
    output wire [2:0] awsize,output wire [1:0] awburst,
    output wire awvalid,input wire awready,
    output wire [63:0] wdata,output wire [7:0] wstrb,
    output wire wlast,wvalid,input wire wready,
    input wire [1:0] bresp,input wire bvalid,output wire bready
);
    wire [23:0] req_addr;
    wire req_write,req_valid;
    wire [63:0] req_data;
    wire [7:0] req_strobe;
    reg [63:0] response_data;
    reg response_valid,fault;
    reg [31:0] address_q;
    reg [63:0] data_q;
    reg [7:0] strobe_q;
    reg aw_pending,w_pending;
    localparam IDLE=0,READ_ADDR=1,READ_DATA=2,WRITE_SEND=3,WRITE_RESP=4;
    reg [2:0] state;
    wire req_ready=state==IDLE && !response_valid;
    wire cdc_core_busy,cdc_memory_busy,core_busy_control;
    // The outstanding/request summary contains combinational core logic.
    // Register the level before crossing into the control clock, and retain
    // the launch register through physical optimization so glitches cannot
    // enter the synchronizer. Reset remains busy until the source is running.
    (* DONT_TOUCH="TRUE" *) reg core_busy_q;
    reg [3:0] quiet_count;
    multitini_memory_cdc cdc_i(
        .core_clk(core_clk),.core_resetn(core_resetn),
        .core_request_addr(core_memory_addr),.core_request_write(core_memory_write),
        .core_request_wdata(core_memory_wdata),.core_request_wstrb(core_memory_wstrb),
        .core_request_valid(core_memory_valid),.core_request_ready(core_memory_ready),
        .core_response_rdata(core_memory_rdata),.core_response_valid(core_memory_rvalid),
        .memory_clk(control_clk),.memory_resetn(control_resetn),
        .memory_request_addr(req_addr),.memory_request_write(req_write),
        .memory_request_wdata(req_data),.memory_request_wstrb(req_strobe),
        .memory_request_valid(req_valid),.memory_request_ready(req_ready),
        .memory_response_rdata(response_data),.memory_response_valid(response_valid),
        .core_busy(cdc_core_busy),.memory_busy(cdc_memory_busy));
    cdc_bit_sync fault_sync_i(.clk(core_clk),.resetn(core_resetn),.din(fault),.dout(core_memory_fault));
    always @(posedge core_clk)begin
        if(!core_resetn)core_busy_q<=1;
        else core_busy_q<=cdc_core_busy;
    end
    cdc_bit_sync busy_sync_i(.clk(control_clk),.resetn(control_resetn),.din(core_busy_q),.dout(core_busy_control));
    always @(posedge control_clk)begin
        if(!control_resetn || state!=IDLE || req_valid || response_valid || cdc_memory_busy || core_busy_control)quiet_count<=0;
        else if(quiet_count!=15)quiet_count<=quiet_count+1'b1;
    end
    assign quiescent=quiet_count==15;
    assign araddr=address_q,arlen=0,arsize=3,arburst=1,arvalid=state==READ_ADDR;
    assign awaddr=address_q,awlen=0,awsize=3,awburst=1;
    assign awvalid=state==WRITE_SEND && aw_pending;
    assign wdata=data_q,wstrb=strobe_q,wlast=1,wvalid=state==WRITE_SEND && w_pending;
    assign rready=state==READ_DATA,bready=state==WRITE_RESP;
    always @(posedge control_clk) begin
        if(!control_resetn)begin
            state<=IDLE;response_valid<=0;response_data<=0;fault<=0;
            address_q<=0;data_q<=0;strobe_q<=0;aw_pending<=0;w_pending<=0;
        end else begin
            response_valid<=0;
            case(state)
                IDLE:if(req_valid && req_ready)begin
                    address_q<=32'h24000000|{8'd0,req_addr[23:3],3'b000};
                    data_q<=req_data;strobe_q<=req_strobe;
                    if(req_write)begin state<=WRITE_SEND;aw_pending<=1;w_pending<=1;end
                    else state<=READ_ADDR;
                end
                READ_ADDR:if(arready)state<=READ_DATA;
                READ_DATA:if(rvalid)begin
                    response_data<=rdata;
                    if(rresp!=0 || !rlast)fault<=1;
                    response_valid<=1;state<=IDLE;
                end
                WRITE_SEND:begin
                    if(awready)aw_pending<=0;
                    if(wready)w_pending<=0;
                    if((!aw_pending || awready) && (!w_pending || wready))state<=WRITE_RESP;
                end
                WRITE_RESP:if(bvalid)begin
                    if(bresp!=0)fault<=1;
                    response_data<=0;response_valid<=1;state<=IDLE;
                end
                default:begin state<=IDLE;fault<=1;end
            endcase
        end
    end
endmodule
