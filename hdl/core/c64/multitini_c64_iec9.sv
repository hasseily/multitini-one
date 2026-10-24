// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Standard Commodore serial bus device 9. All pin values are electrical
// levels: one releases the open collector, zero pulls it low. Timing follows
// Commodore's C64 Programmer's Reference Guide, Figure 6-4 (pp.364-365).
// DOS and storage live on the ARM; this block never interprets file contents.
module multitini_c64_iec9 #(
    parameter integer CLOCK_HZ = 31524725
) (
    input logic clk, hard_reset, run, quiesce,
    input logic atn, bus_clk, bus_data,
    output logic data_o, clk_o,
    output logic [31:0] event_data,
    output logic event_valid,
    input logic event_ready,
    input logic [31:0] reply_data,
    input logic reply_valid,
    output logic reply_ready,
    output logic busy, quiescent,
    output logic [15:0] token
);
    localparam integer T60 = (64'(CLOCK_HZ)*60+999999)/1000000;
    localparam integer T80 = (64'(CLOCK_HZ)*80+999999)/1000000;
    localparam integer T100 = (64'(CLOCK_HZ)*100+999999)/1000000;
    localparam integer T200 = (64'(CLOCK_HZ)*200+999999)/1000000;
    typedef enum logic [4:0] {
        IDLE, RX_READY, RX_START, RX_EOI, RX_AFTER_EOI, RX_LOW,
        RX_HIGH, RX_COMMIT, RX_ACK, TURN, TX_REQUEST, TX_WAIT,
        TX_READY, TX_EOI_LOW, TX_EOI_HIGH, TX_SETUP, TX_HIGH,
        TX_ACK, TX_GAP, TX_EMPTY, TX_DONE, RX_ATN_DELAY, RX_ATN_LOW
    } state_t;
    state_t state;
    logic [15:0] timer;
    logic [7:0] shift;
    logic [7:0] command_primary;
    logic [3:0] channel;
    logic [2:0] bit_index;
    logic listening, talking, rx_eoi, tx_eoi;
    logic previous_atn, running, first_read, was_quiesced;
    logic pending;
    logic [31:0] pending_data;
    logic [3:0] received_event;
    logic received_publish;
    wire atn_start = previous_atn && !atn;
    assign event_data = pending_data;
    assign event_valid = pending && run;
    // Consume obsolete responses even during ATN/idle. Only TX_WAIT and the
    // exact current token can install a byte. There is no deferred old reply.
    assign reply_ready = run && !(state==TX_WAIT &&
        reply_data[31:16]==token && (timer<T80 || pending) && !quiesce && !atn_start);
    assign busy = run && (state != IDLE || listening || talking);
    assign quiescent = run && quiesce && was_quiesced && !pending;

    always_comb begin
        received_event=3; received_publish=0;
        if(!atn) begin
            if(shift==8'h3f && listening) begin received_event=5;received_publish=1;end
            else if(shift==8'h5f && talking) begin received_event=7;received_publish=1;end
            else if(listening && shift[7:4]==4'hf) begin received_event=1;received_publish=1;end
            else if(listening && shift[7:4]==4'he) begin received_event=2;received_publish=1;end
            else if(listening && shift[7:4]==4'h6) begin received_event=8;received_publish=1;end
        end else if(listening) begin received_event=rx_eoi?4:3;received_publish=1;end
    end

    always_ff @(posedge clk) begin
        previous_atn <= atn;
        if(hard_reset) begin
            state<=IDLE;data_o<=1;clk_o<=1;timer<=0;shift<=0;channel<=0;
            bit_index<=0;listening<=0;talking<=0;rx_eoi<=0;tx_eoi<=0;
            previous_atn<=1;running<=0;pending<=0;pending_data<=0;
            token<=0;first_read<=1;was_quiesced<=0;command_primary<=0;
        end else if(!run) begin
            state<=IDLE;data_o<=1;clk_o<=1;pending<=0;
            listening<=0;talking<=0;running<=0;timer<=0;was_quiesced<=0;
            if(running) token<=token+1'b1;
        end else begin
            if(pending && event_ready) pending<=0;
            // The local event slot is reserved throughout transmission. Record
            // receiver acceptance before handling a coincident ATN/quiesce;
            // otherwise GET# followed by UNTALK could replay the last byte.
            if(state==TX_ACK && !bus_data && !pending) begin
                pending<=1;pending_data<={token,4'd10,channel,shift};
            end
            if(!running) begin
                running<=1;token<=token+1'b1;
                pending<=1;pending_data<={token+16'd1,16'd0};
                // Enabling during an existing ATN must acknowledge it too.
                if(atn) state<=IDLE; else state<=RX_ATN_DELAY;
                data_o<=atn;clk_o<=1;command_primary<=0;timer<=0;
            end else if(quiesce) begin
                // Accepted events are retained; a partial unacknowledged byte
                // is abandoned. Host drains these events before disabling.
                state<=IDLE;data_o<=1;clk_o<=1;listening<=0;talking<=0;
                if(!was_quiesced) token<=token+1'b1;
                was_quiesced<=1;
            end else if(atn_start || (was_quiesced && !atn)) begin
                // ATN can cancel every phase, including ARM storage waits.
                token<=token+1'b1;state<=RX_ATN_DELAY;data_o<=0;clk_o<=1;
                timer<=0;rx_eoi<=0;was_quiesced<=0;command_primary<=0;
            end else if(atn && !previous_atn && talking) begin
                state<=TURN;data_o<=0;clk_o<=1;timer<=0;first_read<=1;
            end else if(atn && !previous_atn && !listening) begin
                state<=IDLE;data_o<=1;clk_o<=1;
            end else begin
                was_quiesced<=0;
                case(state)
                IDLE: begin data_o<=1;clk_o<=1;end
                // An ATN edge may arrive while the previous talker still has
                // CLK high. Match VICE's bus-device P_PRE0/P_PRE1 settling:
                // wait 100us, then observe low before accepting ready-high.
                RX_ATN_DELAY: if(timer==T100-1) state<=RX_ATN_LOW;
                    else timer<=timer+1'b1;
                RX_ATN_LOW: if(!bus_clk) state<=RX_READY;
                RX_READY, RX_ACK: begin
                    // The listener may hold this line indefinitely while the
                    // ARM is busy. Do not begin a byte without queue space.
                    if(bus_clk && !pending && event_ready) begin
                        data_o<=1;state<=RX_START;timer<=0;rx_eoi<=0;
                    end
                end
                RX_START: begin
                    if(!bus_clk) begin state<=RX_LOW;bit_index<=0;end
                    else if(atn && timer==T200-1) begin
                        data_o<=0;state<=RX_EOI;timer<=0;rx_eoi<=1;
                    end else if(timer!=16'hffff) timer<=timer+1'b1;
                end
                RX_EOI: begin
                    if(timer==T80-1) begin data_o<=1;state<=RX_AFTER_EOI;end
                    else timer<=timer+1'b1;
                end
                RX_AFTER_EOI: if(!bus_clk) begin state<=RX_LOW;bit_index<=0;end
                RX_LOW: if(bus_clk) begin shift[bit_index]<=bus_data;state<=RX_HIGH;end
                RX_HIGH: if(!bus_clk) begin
                    if(bit_index==7) state<=RX_COMMIT;
                    else begin bit_index<=bit_index+1'b1;state<=RX_LOW;end
                end
                RX_COMMIT: if(!pending) begin
                    if(!atn && command_primary==0 && shift!=8'h29 &&
                       shift!=8'h49 && shift!=8'h3f && shift!=8'h5f) begin
                        // All devices acknowledge ATN, but only the addressed
                        // device acknowledges its primary frame. Otherwise a
                        // missing device would falsely appear present.
                        command_primary<=shift;listening<=0;talking<=0;
                        data_o<=1;clk_o<=1;state<=IDLE;
                    end else begin
                    if(!atn && command_primary==0) command_primary<=shift;
                    if(received_publish) begin
                        pending<=1;
                        pending_data<={token,received_event,
                            (!atn && (shift[7:4]==6 || shift[7:4]==14 || shift[7:4]==15))?shift[3:0]:channel,shift};
                    end
                    if(!atn) begin
                        if(shift==8'h29) begin listening<=1;talking<=0;end
                        if(shift[7:5]==3'b010) talking<=shift==8'h49;
                        if(shift==8'h3f) listening<=0;
                        if(shift==8'h5f) talking<=0;
                        if((listening || talking) &&
                           (shift[7:4]==6 || shift[7:4]==14 || shift[7:4]==15))
                            channel<=shift[3:0];
                    end
                    data_o<=0;state<=RX_ACK;
                    end
                end
                TURN: if(bus_clk) begin
                    // C64 first releases CLK and holds DATA. Acknowledge the
                    // reversed roles by pulling CLK, then release our DATA.
                    clk_o<=0;data_o<=1;timer<=0;state<=TX_REQUEST;
                end
                TX_REQUEST: if(!pending) begin
                    token<=token+1'b1;pending<=1;
                    pending_data<={token+16'd1,first_read?4'd6:4'd9,channel,8'd0};
                    first_read<=0;state<=TX_WAIT;
                end
                TX_WAIT: begin
                    if(timer<T80) timer<=timer+1'b1;
                    if(reply_valid && reply_ready && reply_data[31:16]==token && timer>=T80) begin
                        shift<=reply_data[7:0];tx_eoi<=reply_data[8];
                        if(reply_data[9]) begin clk_o<=1;data_o<=1;state<=TX_EMPTY;end
                        else begin clk_o<=1;state<=TX_READY;end
                    end
                end
                TX_READY: if(bus_data) begin
                    if(tx_eoi) state<=TX_EOI_LOW;
                    else begin clk_o<=0;timer<=0;bit_index<=0;data_o<=shift[0];state<=TX_SETUP;end
                end
                TX_EOI_LOW: if(!bus_data) state<=TX_EOI_HIGH;
                TX_EOI_HIGH: if(bus_data) begin
                    clk_o<=0;timer<=0;bit_index<=0;data_o<=shift[0];state<=TX_SETUP;
                end
                // Allow100us for the first low phase: KERNAL ready/EOI setup
                // can overlap a VIC badline. Later low/high phases use VICE's
                //60us timing. IEC permits this extra initial setup margin.
                TX_SETUP: if(timer==(bit_index==0?T100:T60)-1) begin clk_o<=1;timer<=0;state<=TX_HIGH;end
                    else timer<=timer+1'b1;
                TX_HIGH: if(timer==T60-1) begin
                    clk_o<=0;timer<=0;
                    if(bit_index==7) begin data_o<=1;state<=TX_ACK;end
                    else begin bit_index<=bit_index+1'b1;data_o<=shift[bit_index+1'b1];state<=TX_SETUP;end
                end else timer<=timer+1'b1;
                TX_ACK: if(!bus_data && !pending) begin timer<=0;state<=TX_GAP;end
                TX_GAP: if(timer==T100-1) begin
                    if(tx_eoi) begin clk_o<=1;data_o<=1;state<=TX_DONE;end
                    else begin timer<=T80;state<=TX_REQUEST;end
                end else timer<=timer+1'b1;
                // Empty stream deliberately times out the C64 ACPTR, as a
                // 1541 does for FILE NOT FOUND. No invented byte or EOI.
                TX_EMPTY, TX_DONE: begin clk_o<=1;data_o<=1;end
                default: state<=IDLE;
                endcase
            end
        end
    end
endmodule
