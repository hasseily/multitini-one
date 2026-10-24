// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps
// UCI accesses physical C64 RAM while the machine is stopped at a bus boundary.
// Unlike PRG install, this path neither resets the CPU nor changes BASIC state.
module multitini_c64_uci_ram(
    input wire clk,reset,hold,paused,reu_idle,
    input wire load_valid,load_first,load_last,load_abort,clear_error,
    input wire [7:0] load_data,
    input wire [31:0] load_offset,load_total,
    output wire load_ready,
    output reg load_error=0,
    output reg ram_write=0,
    output reg [15:0] ram_address=0,
    output reg [7:0] ram_data=0,
    input wire [31:0] read_request,
    output reg [3:0] read_done=0,
    output reg peek_request=0,
    output reg [15:0] peek_address=0,
    input wire peek_valid,
    input wire [7:0] peek_data,
    output wire buffer_owned,
    output reg buffer_write=0,
    output reg [10:0] buffer_address=0,
    output reg [7:0] buffer_data=0
);
    reg loading=0;
    reg [15:0] write_address=0;
    reg [31:0] expected_offset=0;
    reg [2:0] state=0;
    reg [9:0] index=0,last_index=0;
    reg [3:0] token=0;
    assign load_ready=hold && paused && reu_idle && state==0;
    assign buffer_owned=state!=0;
    always @(posedge clk) begin
        ram_write<=0;peek_request<=0;buffer_write<=0;
        if(reset)begin
            loading<=0;load_error<=0;expected_offset<=0;
            state<=0;read_done<=read_request[31:28];token<=0;index<=0;last_index<=0;
        end else begin
            if(clear_error)load_error<=0;
            if(load_abort || !hold)loading<=0;
            if(load_valid && load_ready)begin
                if(load_first)begin
                    loading<=load_offset==0 && load_total>=3 && load_total<=65538 && !load_last;
                    expected_offset<=1;write_address[7:0]<=load_data;
                    if(load_offset!=0 || load_total<3 || load_total>65538 || load_last)load_error<=1;
                end else if(!loading || load_offset!=expected_offset ||
                    load_last!=(load_offset+1==load_total))begin
                    loading<=0;load_error<=1;
                end else begin
                    expected_offset<=expected_offset+1;
                    if(load_offset==1)write_address[15:8]<=load_data;
                    else begin
                        ram_write<=1;ram_address<=write_address;ram_data<=load_data;
                        write_address<=write_address+1'b1;
                    end
                    if(load_last)loading<=0;
                end
            end
            case(state)
            0: if(read_request[27] && read_request[31:28]!=read_done &&
                  hold && paused && reu_idle && !loading && !load_valid)begin
                token<=read_request[31:28];peek_address<=read_request[15:0];
                last_index<=read_request[25:16];index<=0;state<=1;
            end
            1: begin peek_request<=1;state<=2;end
            2: if(peek_valid)begin
                buffer_write<=1;buffer_address<=11'h380+index;buffer_data<=peek_data;
                state<=3;
            end
            3: begin
                if(index==last_index)state<=4;
                else begin index<=index+1'b1;peek_address<=peek_address+1'b1;state<=1;end
            end
            4: begin read_done<=token;state<=0;end
            default:state<=0;
            endcase
        end
    end
endmodule
