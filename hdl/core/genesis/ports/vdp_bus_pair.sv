// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2023 nukeykt
// Derived from Nuked-MD ym7101.v and its ym7101_dff cell at
// MegaDrive_MiSTer 083e1b1f4db8ffb32d6e8f83cf822fedc6184a61.
// Multitini change, 2026-09-30: acyclic arrangement of the bus latch cluster.
// This is free software under GNU GPL version 2 or any later version, without
// any warranty; see third_party/genesis/upstream/rtl/nuked-md/LICENSE.
// The dff13/dff15 positive feedback retains the preceding sampled output in
// its bistable case. The dff12/dff13 self-reset path has input combinations
// with no simultaneous Boolean fixed point. Those become one sampled,
// clear-dominant pulse: dff12 asserts while dff13 clears. All stable upstream
// transitions are unchanged; scripts/test_genesis_vdp_pair.py enumerates both
// the stable transitions and the deliberately resolved pulse cases.
module multitini_genesis_vdp_bus_pair (
    input wire MCLK,
    input wire reset_comb,dff21,cpu_as,w2,dff14,w44,
    output wire out12,out13,out15
);
    reg l1_13=0,l2_13=0,l1_15=0,l2_15=0;
    reg l1_12=0,l2_12=0;
    wire reset13=reset_comb || out12;
    wire enable13=w2 && cpu_as;
    wire candidate15=!(reset_comb || dff21) && (dff14 ? l1_15 : l2_15);
    wire base13=!reset_comb && ((enable13 && !candidate15) ? l1_13 : l2_13);
    wire clear_request=cpu_as ? l2_12 : l1_12;
    assign out13=base13 && !clear_request;
    assign out12=base13 && clear_request;
    assign out15=candidate15 && !out13;
    wire clock13=enable13 && (!out15 || dff21);
    wire reset15=reset_comb || dff21 || out13;
    always @(posedge MCLK) begin
        if(reset13) l1_13<=0;
        else if(!clock13) l1_13<=w44;
        l2_13<=out13;
        if(reset15) l1_15<=0;
        else if(!dff14) l1_15<=w44;
        l2_15<=out15;
        if(!out13) l1_12<=0;
        else if(cpu_as) l1_12<=1;
        l2_12<=out12;
    end
endmodule
