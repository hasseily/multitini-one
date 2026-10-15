// SPDX-License-Identifier: GPL-3.0-only
// Functional model of the reviewed 7-series default SYNC BUFGCE.
// Routing, pulse-width and CE setup/hold qualification require Vivado.
// Installed Unisim BUFGCE.v latches CE only while I is low.
`timescale 1ns/1ps
module BUFGCE(input wire I,CE,output wire O);
    reg enable_latched=0;
    always_latch if(!I)enable_latched=CE;
    wire gated=I && enable_latched;
    wire delayed;
    assign #0.001 delayed=gated;
    assign O=$test$plusargs("DELAYED_BUFG") ? delayed : gated;
endmodule
