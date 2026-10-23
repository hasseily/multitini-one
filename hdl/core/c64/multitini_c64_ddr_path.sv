// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Dedicated C64 REU aperture on HP2, derived from the IIgs DDR transport.
//
// HP2's ACLK is the machine clock in the C64 image, so this AXI3 master runs
// directly on core_clk and the data path has no clock crossing. One
// transaction is outstanding at a time:
// - memory_burst=0 read: one 64-bit beat (ARLEN=0) for core_memory_addr[23:3].
// - memory_burst=1 read: a 4-beat INCR burst (ARLEN=3) of the 32-byte line.
// - write: one strobed 64-bit beat. AW and W handshake independently; the
//   single completion pulse waits for the B response.
// A read returns one core_memory_rvalid pulse per beat in ascending address
// order. A write returns one pulse after B. RRESP/BRESP errors and a wrong
// RLAST set a sticky fault, which the core treats as fatal. This path still
// returns the expected number of pulses after a fault; whether the requester
// then drains its own queue is the requester's contract.
//
// Machine-clock reset (core_resetn, the PL c64_aresetn) does not reset the
// PS7 HP2 AFI, so an issued AXI transaction is never abandoned. If reset
// arrives after a request was accepted, AWVALID/WVALID/ARVALID stay high with
// their held payload until each handshake completes, and the matching B, or
// the R beats up to RLAST or the expected count, are absorbed without a
// requester pulse or fault. core_memory_ready stays low and the drain level
// stays busy until that orphan has finished, so a later write can never pair
// with an orphaned address or data beat. The normal (no reset) protocol is
// unchanged. Not recoverable here: machine MMCM lock loss, which can glitch or
// stop this ACLK under an in-flight handshake, and an HP2 port that never
// answers (the drain then stays busy and firmware cancels the core switch
// instead of starting PCAP).
//
// Only the drain summary crosses clocks. It is a registered machine-domain
// level, synchronized into control_clk by cdc_bit_sync and then filtered by
// the control-domain quiet counter that the firmware polls before PCAP. The
// summary also stays busy until the machine-domain core reset request has
// been held for STOP_HOLD machine cycles, and the quiet counter restarts while
// the control-domain request is low. Quiescent therefore means the request
// reached the machine, the core stopped issuing, and every posted write and
// AXI transaction then drained.
module multitini_c64_ddr_path (
    input wire core_clk,core_resetn,control_clk,control_resetn,
    // Core reset request: the control-domain register bit and its machine-
    // domain synchronized copy (the one that holds the adapter in reset).
    input wire control_reset_request,core_reset_request,
    input wire [23:0] core_memory_addr,
    input wire core_memory_write,
    input wire [63:0] core_memory_wdata,
    input wire [7:0] core_memory_wstrb,
    input wire core_memory_valid,
    input wire core_memory_burst,
    // Requester's registered drain level: posted writes or a request pending.
    input wire core_memory_busy,
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
    localparam [31:0] APERTURE=32'h25000000;
    localparam [1:0] IDLE=2'd0,READ=2'd1,WRITE=2'd2,READ_PAD=2'd3;
    // Allow a complete 32-clock native frame for guest reset to reach the
    // machine, then another frame for the last accepted write and registered
    // cache drain level. The cache keeps posted writes alive during soft reset.
    localparam [5:0] STOP_HOLD=6'd63;
    reg [1:0] state;
    reg [31:0] address_q;
    reg [63:0] data_q;
    reg [7:0] strobe_q;
    reg burst_q,ar_pending,aw_pending,w_pending;
    reg [1:0] beat;
    reg [63:0] response_data;
    reg response_valid,fault;
    // The in-flight transaction was cut off by machine reset: finish it on
    // AXI, but return no pulse and record no fault for it.
    reg orphan;
    wire last_beat=beat==(burst_q ? 2'd3 : 2'd0);
    wire drop=orphan || !core_resetn;

    assign core_memory_ready=state==IDLE;
    assign core_memory_rdata=response_data;
    assign core_memory_rvalid=response_valid;
    assign core_memory_fault=fault;

    assign araddr=address_q,arlen=burst_q ? 4'd3 : 4'd0,arsize=3'd3,arburst=2'd1;
    assign arvalid=state==READ && ar_pending;
    assign awaddr=address_q,awlen=4'd0,awsize=3'd3,awburst=2'd1;
    assign awvalid=state==WRITE && aw_pending;
    assign wdata=data_q,wstrb=strobe_q,wlast=1'b1;
    assign wvalid=state==WRITE && w_pending;
    // Responses are consumed only for the transaction that requested them,
    // or absorbed for an orphan. Ready stays high so a stray beat or B can
    // never stall the PS port.
    assign rready=1'b1,bready=1'b1;

    always @(posedge core_clk)begin
        response_valid<=0;
        if(!core_resetn)begin response_data<=0;fault<=0;end
        case(state)
            IDLE:begin
                orphan<=0;
                // The held payload needs no reset: no valid is raised in IDLE.
                if(!core_resetn)begin
                    ar_pending<=0;aw_pending<=0;w_pending<=0;beat<=0;
                end else if(core_memory_valid)begin
                    data_q<=core_memory_wdata;strobe_q<=core_memory_wstrb;beat<=0;
                    if(core_memory_write)begin
                        address_q<=APERTURE|{8'd0,core_memory_addr[23:3],3'b000};
                        burst_q<=0;aw_pending<=1;w_pending<=1;state<=WRITE;
                    end else begin
                        address_q<=APERTURE|(core_memory_burst ?
                            {8'd0,core_memory_addr[23:5],5'b00000} :
                            {8'd0,core_memory_addr[23:3],3'b000});
                        burst_q<=core_memory_burst;ar_pending<=1;state<=READ;
                    end
                end
            end
            READ:begin
                if(!core_resetn)orphan<=1;
                if(arready)ar_pending<=0;
                // No beat can belong to this burst before AR completes.
                if(rvalid && !ar_pending)begin
                    beat<=beat+1'b1;
                    if(!drop)begin
                        response_data<=rdata;response_valid<=1;
                        if(rresp!=2'b00)fault<=1;
                    end
                    if(last_beat)begin
                        if(!rlast && !drop)fault<=1;
                        state<=IDLE;
                    end else if(rlast)begin
                        // An orphan burst ends at RLAST; it needs no padding.
                        if(!drop)fault<=1;
                        state<=drop ? IDLE : READ_PAD;
                    end
                end
            end
            // A burst ended early: return the missing beats as zero so the
            // requester sees its full response count after the fault.
            READ_PAD:begin
                if(drop)state<=IDLE;
                else begin
                    response_data<=0;response_valid<=1;beat<=beat+1'b1;
                    if(last_beat)state<=IDLE;
                end
            end
            WRITE:begin
                if(!core_resetn)orphan<=1;
                if(awready)aw_pending<=0;
                if(wready)w_pending<=0;
                // B is accepted only after both address and data issued.
                if(!aw_pending && !w_pending && bvalid)begin
                    if(!drop)begin
                        if(bresp!=2'b00)fault<=1;
                        response_data<=0;response_valid<=1;
                    end
                    state<=IDLE;
                end
            end
            default:begin state<=IDLE;if(core_resetn)fault<=1;end
        endcase
    end

    // Machine-domain stop proof: the synchronized request held STOP_HOLD.
    reg [5:0] stop_count;
    always @(posedge core_clk)begin
        if(!core_resetn || !core_reset_request)stop_count<=0;
        else if(stop_count!=STOP_HOLD)stop_count<=stop_count+1'b1;
    end

    // Drain summary. Every term is a machine-domain register or a registered
    // requester output, but the OR is combinational: register it before the
    // synchronizer and keep the launch register through optimization so no
    // decode glitch can reach control_clk. Reset reports busy, and so does a
    // machine that is not yet proven stopped.
    (* DONT_TOUCH="TRUE" *) reg core_busy_q;
    wire core_busy_control;
    reg [3:0] quiet_count;
    always @(posedge core_clk)begin
        if(!core_resetn)core_busy_q<=1;
        else core_busy_q<=core_memory_busy || core_memory_valid || state!=IDLE || response_valid ||
            stop_count!=STOP_HOLD;
    end
    cdc_bit_sync busy_sync_i(.clk(control_clk),.resetn(control_resetn),.din(core_busy_q),.dout(core_busy_control));
    // The quiet window opens only while the control-domain request is set,
    // so a quiet level launched before the request reached the machine (and
    // still in the synchronizers) cannot complete it.
    always @(posedge control_clk)begin
        if(!control_resetn || !control_reset_request || core_busy_control)quiet_count<=0;
        else if(quiet_count!=15)quiet_count<=quiet_count+1'b1;
    end
    assign quiescent=quiet_count==15;
endmodule
