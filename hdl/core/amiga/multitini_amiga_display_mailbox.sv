// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Optional guest preferences in unused Gayle DE0Fxx space. Native bus
// acknowledgement still belongs to the original chipset. ACK is written last.
module multitini_amiga_display_mailbox(
    input wire service_clk,native_clk,reset,guest_reset,
    input wire [15:0] request,
    input wire [23:1] address,
    input wire as_n,rw,uds_n,lds_n,dtack_n,
    input wire [15:0] write_data,
    output wire read_selected,
    output wire [15:0] read_data,
    input wire [4:0] arm_address,
    output reg [15:0] arm_data
);
    reg [15:0] words[3:17];
    wire selected=({address,1'b0}>=24'hde0f00 && {address,1'b0}<24'hde0f24);
    wire [4:0] word_index=address[5:1];
    assign read_selected=selected && !as_n && rw && !(uds_n && lds_n);
    function automatic [15:0] value(input [4:0] index);
        begin
            case(index)
                0:value=16'h4d54;
                1:value=16'd1;
                2:value=request;
                default:value=index<=17 ? words[index] : 16'd0;
            endcase
        end
    endfunction
    // Name the live operands directly. The native CPU can keep the same
    // address selected while ARM publishes a new request or a byte write
    // updates a word; address changes are not a freshness barrier.
    assign read_data=word_index==0 ? 16'h4d54:word_index==1 ? 16'd1:
        word_index==2 ? request:word_index<=17 ? words[word_index]:16'd0;
    integer i;
    always @(posedge native_clk or posedge reset)begin
        if(reset)begin
            for(i=3;i<=17;i=i+1)words[i]<=0;
        end else if(guest_reset)begin
            for(i=3;i<=17;i=i+1)words[i]<=0;
        end else if(selected && !as_n && !rw && !dtack_n && word_index>=3)begin
            if(!uds_n)words[word_index][15:8]<=write_data[15:8];
            if(!lds_n)words[word_index][7:0]<=write_data[7:0];
        end
    end
    // Related sibling clocks: native registers settle for the inactive half
    // cycle. Host address is staged before this edge; completion is next posedge.
    always @(negedge service_clk or posedge reset)
        if(reset)arm_data<=0;
        else arm_data<=value(arm_address);
endmodule
