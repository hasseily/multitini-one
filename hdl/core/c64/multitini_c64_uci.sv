// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Ultimate command register endpoint. Only accepted native CPU transactions
// reach bus_valid. The ARM owns command dispatch; this block owns byte queues
// and observable handshakes. No CPU polling or storage work crosses this clock.
module multitini_c64_uci (
    input logic clk, hard_reset, block_reset,
    input logic enabled, suspended,
    input logic bus_valid, bus_write,
    input logic ff00_write,
    input logic [2:0] bus_addr,
    input logic [7:0] bus_wdata,
    output logic [7:0] rdata,
    input logic [4:0] bus_id,
    input logic freeze_allowed, irq_allowed,
    input logic [31:0] op_data,
    input logic op_valid,
    output wire op_ready,
    output logic [31:0] status, status2,
    output logic [10:0] ram_addr,
    output logic ram_we,
    output logic [7:0] ram_wdata,
    input logic [7:0] ram_rdata,
    input logic [3:0] ram_read_done,
    output wire freeze, irq
);
    localparam [1:0] IDLE=0, BUSY=1, LAST=2, MORE=3;
    logic [1:0] state;
    logic error, abort_pending, data_accepted, command_busy;
    logic overflow, busy_write, idle_abort;
    logic freeze_latch, trigger_latch, irq_latch, irq_pending;
    logic [9:0] fill_count, command_length, response_length, response_count;
    logic [8:0] status_length, status_count;
    logic [7:0] response_head, status_head;
    logic response_valid, status_valid, validate_pending, validate_more;
    logic [1:0] fetch_wait, fetch_kind;
    logic [3:0] command_sequence, epoch;
    logic [1:0] op_ack;
    logic op_dropped, reset_previous;
    wire data_state=state==LAST || state==MORE;
    wire data_available=data_state && !abort_pending && response_valid && response_count<response_length;
    wire status_available=data_state && !abort_pending && status_valid && status_count<status_length;
    wire write_control=bus_valid && bus_write && bus_addr==4;
    wire write_command=bus_valid && bus_write && bus_addr==5;
    wire tagged_op=op_data[28:25]==command_sequence && op_data[31:29]==epoch[2:0];
    assign op_ready=1'b1;
    assign freeze=freeze_latch && freeze_allowed && !block_reset;
    assign irq=irq_pending && irq_allowed && !block_reset;

    always_comb begin
        rdata=8'hff;
        case(bus_addr)
            3: rdata={3'd0,bus_id};
            4: rdata={data_available,status_available,state,error,abort_pending,data_accepted,command_busy};
            5: rdata=irq ? 8'h49 : 8'hc9;
            6: rdata=data_available ? response_head : 8'd0;
            7: rdata=status_available ? status_head : 8'd0;
            default:;
        endcase
        status=0;
        status[1:0]=state;status[2]=abort_pending;status[3]=data_accepted;
        status[4]=error;status[5]=freeze_latch;status[6]=trigger_latch;status[7]=irq_latch;
        status[17:8]=command_busy ? command_length : fill_count;
        status[21:18]=command_sequence;status[25:22]=epoch;
        status[26]=suspended;status[27]=enabled;status[29]=idle_abort;status[31]=1;
        status2=0;status2[9:0]=response_count;status2[18:10]=status_count;
        status2[20:19]=op_ack;status2[21]=op_dropped;status2[22]=busy_write;
        status2[23]=overflow;status2[27:24]=ram_read_done;
        status2[30]=command_busy;status2[31]=1;
    end

    always_ff @(posedge clk) begin
        ram_we<=0;
        reset_previous<=block_reset;
        if(hard_reset) begin
            state<=IDLE;error<=0;abort_pending<=0;data_accepted<=0;command_busy<=0;
            overflow<=0;busy_write<=0;idle_abort<=0;freeze_latch<=0;trigger_latch<=0;
            irq_latch<=0;irq_pending<=0;fill_count<=0;command_length<=0;
            response_length<=0;response_count<=0;status_length<=0;status_count<=0;
            response_head<=0;status_head<=0;response_valid<=0;status_valid<=0;
            validate_pending<=0;validate_more<=0;fetch_wait<=0;fetch_kind<=0;
            command_sequence<=0;epoch<=0;op_ack<=0;op_dropped<=0;reset_previous<=1;
            ram_addr<=0;ram_wdata<=0;ram_we<=0;
        end else begin
            if(block_reset && !reset_previous) epoch<=epoch+1'b1;
            // Acknowledgment survives guest reset so the host can drain stale
            // operations without attributing their completion to a new guest.
            if(op_valid) begin op_ack<=op_ack+1'b1;op_dropped<=1;end
            if(block_reset) begin
                state<=IDLE;error<=0;abort_pending<=0;data_accepted<=0;command_busy<=0;
                overflow<=0;busy_write<=0;idle_abort<=0;freeze_latch<=0;trigger_latch<=0;
                irq_latch<=0;irq_pending<=0;fill_count<=0;command_length<=0;
                response_length<=0;response_count<=0;status_length<=0;status_count<=0;
                response_valid<=0;status_valid<=0;validate_pending<=0;fetch_wait<=0;
            end else begin
                // Ultimate's deferred freeze starts on an accepted CPU write
                // to $FF00. Reply publication below has priority over it.
                if(ff00_write && trigger_latch) begin
                    freeze_latch<=1;trigger_latch<=0;
                end
                // Address is registered, BRAM returns a byte on the next edge,
                // and the endpoint captures it on the following edge.
                if(fetch_wait!=0) begin
                    fetch_wait<=fetch_wait-1'b1;
                    if(fetch_wait==1) begin
                        if(fetch_kind==1) begin response_head<=ram_rdata;response_valid<=1;end
                        else begin status_head<=ram_rdata;status_valid<=1;end
                    end
                end else if(!write_command && (validate_pending || data_state) && !abort_pending) begin
                    if(!response_valid && response_count<response_length) begin
                        ram_addr<=11'h380+{1'b0,response_count};fetch_wait<=2;fetch_kind<=1;
                    end else if(!status_valid && status_count<status_length) begin
                        ram_addr<=11'h700+{2'b0,status_count};fetch_wait<=2;fetch_kind<=2;
                    end
                end
                if(validate_pending && fetch_wait==0 &&
                   (response_valid || response_length==0) &&
                   (status_valid || status_length==0)) begin
                    state<=validate_more?MORE:LAST;validate_pending<=0;
                    command_busy<=0;data_accepted<=0;freeze_latch<=0;trigger_latch<=0;
                    irq_pending<=irq_latch;
                end

                if(op_valid && tagged_op) begin
                    case(op_data[3:0])
                        1: if(command_busy) begin
                            command_busy<=0;fill_count<=0;op_dropped<=0;
                        end
                        2: begin data_accepted<=0;op_dropped<=0;end
                        3: if(abort_pending) begin
                            state<=IDLE;abort_pending<=0;data_accepted<=0;command_busy<=0;
                            response_length<=0;status_length<=0;response_valid<=0;status_valid<=0;
                            validate_pending<=0;fetch_wait<=0;freeze_latch<=0;trigger_latch<=0;
                            irq_latch<=0;irq_pending<=0;op_dropped<=0;
                        end
                        4,5: if(state==BUSY && !abort_pending && op_data[14:4]<=896 && op_data[24:16]<=256) begin
                            response_length<=op_data[13:4];status_length<=op_data[24:16];
                            response_count<=0;status_count<=0;response_valid<=0;status_valid<=0;
                            validate_pending<=1;validate_more<=op_data[3:0]==5;fetch_wait<=0;
                            if(command_busy) fill_count<=0;
                            op_dropped<=0;
                        end
                        6: begin idle_abort<=0;op_dropped<=0;end
                        15: begin
                            state<=IDLE;abort_pending<=0;data_accepted<=0;command_busy<=0;
                            fill_count<=0;response_length<=0;status_length<=0;
                            response_count<=0;status_count<=0;response_valid<=0;status_valid<=0;
                            validate_pending<=0;fetch_wait<=0;freeze_latch<=0;trigger_latch<=0;
                            irq_latch<=0;irq_pending<=0;op_dropped<=0;
                        end
                        default:;
                    endcase
                end
                if(bus_valid && !bus_write) begin
                    // Ultimate clears its IRQ enable on either queue read,
                    // including an empty read. DATA_ACC also acknowledges it.
                    if(bus_addr==6 || bus_addr==7) begin irq_pending<=0;irq_latch<=0;end
                    if(bus_addr==6 && data_available) begin response_count<=response_count+1'b1;response_valid<=0;end
                    if(bus_addr==7 && status_available) begin status_count<=status_count+1'b1;status_valid<=0;end
                end
                if(write_command) begin
                    if(command_busy) busy_write<=1;
                    else if(fill_count<896) begin
                        ram_addr<={1'b0,fill_count};ram_wdata<=bus_wdata;ram_we<=1;
                        fill_count<=fill_count+1'b1;
                    end else overflow<=1;
                end
                // CPU actions use the state before this write. In particular,
                // DATA_ACC+PUSH cannot turn an old data state into a new PUSH.
                if(write_control) begin
                    if(bus_wdata[3]) error<=0;
                    if(bus_wdata[2]) begin
                        fill_count<=0;overflow<=0;
                        if(state==IDLE) idle_abort<=1;
                        else abort_pending<=1;
                    end
                    if(bus_wdata[1] && data_state) begin
                        irq_pending<=0;irq_latch<=0;
                        if(state==LAST) begin
                            state<=IDLE;
                            if(abort_pending || bus_wdata[2]) begin abort_pending<=0;idle_abort<=1;end
                        end else begin state<=BUSY;data_accepted<=1;end
                    end
                    if(bus_wdata[0]) begin
                        if(state==IDLE) begin
                            state<=BUSY;command_busy<=1;command_sequence<=command_sequence+1'b1;
                            command_length<=fill_count;abort_pending<=0;data_accepted<=0;
                            freeze_latch<=bus_wdata[7] && freeze_allowed;
                            trigger_latch<=bus_wdata[6];irq_latch<=bus_wdata[5] && irq_allowed;
                            irq_pending<=0;busy_write<=0;
                        end else error<=1;
                    end
                end
            end
        end
    end
endmodule
