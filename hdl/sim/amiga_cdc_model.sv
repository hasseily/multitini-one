// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Test-only four-phase bundled-data handshake. Synchronizer lengths follow
// the production XPM parameters, including the send/acknowledge return to0.
// This models digital latency; physical metastability is a separate gate.
module xpm_cdc_handshake #(
    parameter integer DEST_EXT_HSK=1,DEST_SYNC_FF=4,INIT_SYNC_FF=0,
    parameter integer SIM_ASSERT_CHK=1,SRC_SYNC_FF=4,WIDTH=32
)(
    input wire dest_ack,dest_clk,
    output reg [WIDTH-1:0] dest_out=0,
    output wire dest_req,
    input wire src_clk,input wire [WIDTH-1:0] src_in,
    output wire src_rcv,input wire src_send
);
    reg [DEST_SYNC_FF-1:0] request_sync=0;
    reg [SRC_SYNC_FF-1:0] acknowledge_sync=0;
    assign dest_req=request_sync[DEST_SYNC_FF-1];
    assign src_rcv=acknowledge_sync[SRC_SYNC_FF-1];
    always @(posedge dest_clk)begin
        request_sync<={request_sync[DEST_SYNC_FF-2:0],src_send};
        if(request_sync[DEST_SYNC_FF-2] && !request_sync[DEST_SYNC_FF-1])dest_out<=src_in;
    end
    always @(posedge src_clk)
        acknowledge_sync<={acknowledge_sync[SRC_SYNC_FF-2:0],DEST_EXT_HSK ? dest_ack : dest_req};
    initial if(DEST_SYNC_FF<2 || SRC_SYNC_FF<2)$fatal(1,"invalid XPM fixture synchronizer length");
    wire unused_parameters=(INIT_SYNC_FF!=0)^(SIM_ASSERT_CHK==0);
endmodule
