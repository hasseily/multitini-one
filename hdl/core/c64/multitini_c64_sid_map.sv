// SPDX-License-Identifier: GPL-3.0-only
// Ordered native-domain shadow upload; a complete table swaps atomically.
module multitini_c64_sid_map #(parameter integer SID_COUNT=8)(
    input wire clk,reset,word_valid,input wire [5:0] word_index,
    input wire [31:0] word_data,input wire commit,input wire [7:0] commit_seq,
    input wire ce_1m,paused,
    output reg active_valid=0,output reg [7:0] active_seq=0,
    output wire [255:0] slot_masks,output wire [31:0] write_inhibit,
    output wire [95:0] owners,output wire [127:0] io_slot_masks,
    output wire [255:0] chip_params,output wire [31:0] output_config,
    output reg rejected=0
);
    wire [31:0] shadow[0:56],active[0:56];
    reg [56:0] written=0,invalid=0;
    reg pending=0;reg [7:0] pending_sequence=0;
    reg [2:0] owner_q[0:31];
    integer j;
    function automatic [2:0] lowest(input [7:0] bits);
        integer b;begin lowest=0;for(b=7;b>=0;b=b-1) if(bits[b]) lowest=b;end
    endfunction
    wire valid_table=(&written) && !(|invalid);
    wire valid_word=word_index<32 ?
        ((word_data&32'hfffffe00)==0 && word_data[7:0]!=0 && (word_data[7:0]>>SID_COUNT)==0 &&
         (!word_data[8] || (word_data[7:0] & (word_data[7:0]-1))==0)) :
        word_index<48 ? ((word_data&32'hffffff00)==0 && (word_data[7:0]>>SID_COUNT)==0) :
        word_index<56 ? ((word_data&32'hfffc0000)==0 && (word_index<48+SID_COUNT || word_data==0)) :
        word_index==56 && (word_data&32'hfff00000)==0 && word_data[19:18]!=3;
    // Validate the complete incoming word once, then retain only the bits
    // defined by the ABI. Reserved bits must not become thousands of FFs.
    for(genvar n=0;n<57;n=n+1)begin : entry
        localparam WIDTH=n<32?9:n<48?8:n<56?18:20;
        reg [WIDTH-1:0] stage;
        reg [WIDTH-1:0] live=0;
        assign shadow[n]={{(32-WIDTH){1'b0}},stage};
        assign active[n]={{(32-WIDTH){1'b0}},live};
        always @(posedge clk)begin
            if(word_valid && !pending && word_index==n)stage<=word_data[WIDTH-1:0];
            if(reset)live<=0;
            else if(pending && (ce_1m||paused))live<=stage;
        end
    end
    for(genvar s=0;s<32;s=s+1) begin
        assign slot_masks[s*8+:8]=active[s][7:0];
        assign write_inhibit[s]=active[s][8];assign owners[s*3+:3]=owner_q[s];
    end
    for(genvar s=0;s<16;s=s+1) assign io_slot_masks[s*8+:8]=active[32+s][7:0];
    for(genvar s=0;s<8;s=s+1) assign chip_params[s*32+:32]=active[48+s];
    assign output_config=active[56];
    always @(posedge clk) begin
        rejected<=0;
        if(reset) begin
            written<=0;invalid<=0;pending<=0;active_valid<=0;active_seq<=0;
            for(j=0;j<32;j=j+1) owner_q[j]<=0;
        end else begin
            if(word_valid) begin
                if(!pending && word_index<57) begin written[word_index]<=1;invalid[word_index]<=!valid_word;end
                else rejected<=1;
            end
            if(commit) begin
                if(!pending && valid_table && !word_valid) begin pending<=1;pending_sequence<=commit_seq;end
                else rejected<=1;
            end
            if(pending && (ce_1m||paused)) begin
                for(j=0;j<32;j=j+1) owner_q[j]<=lowest(shadow[j][7:0]);
                active_valid<=1;active_seq<=pending_sequence;pending<=0;written<=0;invalid<=0;
            end
        end
    end
endmodule
