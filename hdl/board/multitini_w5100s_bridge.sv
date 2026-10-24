// SPDX-License-Identifier: GPL-3.0-only
// W5100S host-only parallel bridge. Bus cadence follows Appletini ONE
// hdl/apple/uthernet2_card.sv at 73de6fe03c31ad6f8ebfce3328704761fcd8c5a2
// (GPL-3.0); no Apple bus logic.
// W5100S SYS_CLK must remain in its normal 100 MHz mode. Slow/power-down
// clock modes require a different bus cadence and are not enabled by firmware.
`timescale 1ns/1ps
module multitini_w5100s_bridge #(
    parameter integer CLOCK_HZ=133333333,
    parameter integer RESET_HOLD_US=10000,
    parameter integer READY_WAIT_US=61000,
    parameter integer SETUP_CYCLES=3,
    parameter integer STROBE_CYCLES=16,
    parameter integer RECOVERY_CYCLES=5
) (
    input wire clk, resetn, run,
    input wire access_valid,
    input wire [31:0] access_word,
    output wire [31:0] status,
    output reg access_error,
    (* IOB="TRUE" *) output reg [1:0] eth_a,
    (* IOB="TRUE" *) output reg eth_rd_n, eth_wr_n, eth_cs_n, eth_rst_n,
    (* IOB="TRUE" *) output reg [7:0] eth_d_o,
    output wire [7:0] eth_d_oe,
    input wire [7:0] eth_d_i,
    input wire eth_int_n
);
    localparam integer HOLD_COUNT=(64'(CLOCK_HZ)*RESET_HOLD_US+999999)/1000000;
    localparam integer READY_COUNT=(64'(CLOCK_HZ)*READY_WAIT_US+999999)/1000000;
    localparam integer RESET_MAX=HOLD_COUNT>READY_COUNT ? HOLD_COUNT : READY_COUNT;
    localparam integer RESET_W=$clog2(RESET_MAX+2);
    localparam [2:0] RESET_HOLD=0, RESET_WAIT=1, IDLE=2, SETUP=3, STROBE=4, RECOVER=5, DISPATCH=6;
    localparam [2:0] MODE=0, ADDRESS_HI=1, ADDRESS_LO=2, DATA=3, DIRECT=4;
    reg [2:0] state, phase;
    reg [RESET_W-1:0] reset_count;
    reg [7:0] cycle_count, completion_seq, read_data;
    (* IOB="TRUE" *) reg [7:0] captured_data;
    // Register the IOBUF.T polarity. The inverted public OE interface folds
    // with the shell's inversion, leaving a direct flop-to-T connection.
    (* IOB="TRUE" *) reg [7:0] eth_d_t;
    assign eth_d_oe=~eth_d_t;
    reg [14:0] address_q, next_address;
    reg [7:0] write_data;
    reg write_q, pointer_valid, mode_valid, bootstrap, done, error, reset_done;
    wire interrupt_high;
    cdc_bit_sync interrupt_sync(.clk(clk),.resetn(resetn),.din(eth_int_n),.dout(interrupt_high));
    wire ready=run && reset_done && state==IDLE;
    assign status={1'b1,7'd0,completion_seq,read_data,2'd0,!interrupt_high,reset_done,error,done,
                   (run && !ready),ready};

    task automatic start_bus(input [2:0] which);
        begin
            phase<=which; state<=SETUP;cycle_count<=0;
            eth_cs_n<=1;eth_rd_n<=1;eth_wr_n<=1;
            eth_a<=which==MODE || which==DIRECT ? 2'd0 : which==ADDRESS_HI ? 2'd1 : which==ADDRESS_LO ? 2'd2 : 2'd3;
            eth_d_o<=which==MODE ? 8'h03 : which==ADDRESS_HI ? {1'b0,address_q[14:8]} :
                     which==ADDRESS_LO ? address_q[7:0] : which==DIRECT ? write_data|8'h03 : write_data;
            eth_d_t<={(8){!(which==MODE || which==ADDRESS_HI || which==ADDRESS_LO || write_q)}};
        end
    endtask

    always @(posedge clk) begin
        access_error<=0;
        if(!resetn || !run) begin
            state<=RESET_HOLD;phase<=MODE;reset_count<=0;cycle_count<=0;
            completion_seq<=0;read_data<=0;captured_data<=0;address_q<=0;next_address<=0;
            write_data<=0;write_q<=0;pointer_valid<=0;mode_valid<=0;
            bootstrap<=1;done<=0;error<=0;reset_done<=0;
            eth_a<=0;eth_d_o<=0;eth_d_t<=8'hff;eth_rd_n<=1;eth_wr_n<=1;eth_cs_n<=1;eth_rst_n<=0;
        end else begin
            if(access_valid && (!ready || access_word[31:25]!=0 || access_word[15])) begin
                access_error<=1;error<=1;
            end
            case(state)
            RESET_HOLD: if(reset_count+1>=HOLD_COUNT) begin
                eth_rst_n<=1;reset_count<=0;state<=RESET_WAIT;
            end else reset_count<=reset_count+1'b1;
            RESET_WAIT: if(reset_count+1>=READY_COUNT) start_bus(MODE);
                        else reset_count<=reset_count+1'b1;
            IDLE: if(access_valid && access_word[31:25]==0 && !access_word[15]) begin
                address_q<=access_word[14:0];write_q<=access_word[24];write_data<=access_word[23:16];
                done<=0;error<=0;bootstrap<=0;
                // Latch request first; dispatch on the following cycle so no
                // AXI decode/data path reaches the external output flops.
                state<=DISPATCH;
            end
            DISPATCH: if(address_q==0) start_bus(DIRECT);
                      else if(!mode_valid) start_bus(MODE);
                      else if(pointer_valid && next_address==address_q) start_bus(DATA);
                      else start_bus(ADDRESS_HI);
            SETUP: if(cycle_count+1>=SETUP_CYCLES) begin
                eth_cs_n<=0;eth_wr_n<=!(phase==MODE || phase==ADDRESS_HI || phase==ADDRESS_LO || write_q);
                eth_rd_n<=phase==MODE || phase==ADDRESS_HI || phase==ADDRESS_LO || write_q;
                state<=STROBE;cycle_count<=0;
            end else cycle_count<=cycle_count+1'b1;
            STROBE: if(cycle_count+1>=STROBE_CYCLES) begin
                captured_data<=eth_d_i;
                eth_cs_n<=1;eth_wr_n<=1;eth_rd_n<=1;
                // Keep write data enabled for one further clock after WR#.
                state<=RECOVER;cycle_count<=0;
                if(phase==DATA || phase==DIRECT) begin
                    next_address<=address_q+15'd1;
                    pointer_valid<=phase==DATA;
                    if(phase==DIRECT && write_q) mode_valid<=!write_data[7];
                end
            end else cycle_count<=cycle_count+1'b1;
            RECOVER: begin
                eth_d_t<=8'hff;
                if(cycle_count+1>=RECOVERY_CYCLES) begin
                    case(phase)
                    MODE: begin
                        mode_valid<=1;pointer_valid<=0;
                        if(bootstrap) begin reset_done<=1;bootstrap<=0;state<=IDLE;end
                        else start_bus(ADDRESS_HI);
                    end
                    ADDRESS_HI: start_bus(ADDRESS_LO);
                    ADDRESS_LO: start_bus(DATA);
                    default: begin
                        if(!write_q) read_data<=captured_data;
                        completion_seq<=completion_seq+1'b1;done<=1;state<=IDLE;
                    end
                    endcase
                end else cycle_count<=cycle_count+1'b1;
            end
            default: state<=RESET_HOLD;
            endcase
        end
    end
endmodule
