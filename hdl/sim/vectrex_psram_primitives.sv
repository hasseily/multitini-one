// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Pin-timing test models only. Synthesis uses the actual Xilinx primitives.
`ifndef MT_PSRAM_RETURN_DELAY
`define MT_PSRAM_RETURN_DELAY 0.0
`endif
`ifndef MT_PSRAM_INPUT_DELAY
`define MT_PSRAM_INPUT_DELAY 0.0
`endif
`ifndef MT_PSRAM_DATA_OUTPUT_DELAY
`define MT_PSRAM_DATA_OUTPUT_DELAY 0.0
`endif
`ifndef MT_PSRAM_CLOCK_OUTPUT_DELAY
`define MT_PSRAM_CLOCK_OUTPUT_DELAY 0.0
`endif
module IOBUF(input wire I,T, output wire O, inout wire IO);
    assign #(`MT_PSRAM_DATA_OUTPUT_DELAY) IO=T ? 1'bz : I;
    // Separate board return flight and FPGA IBUF propagation from tACLK.
    assign #(`MT_PSRAM_RETURN_DELAY + `MT_PSRAM_INPUT_DELAY) O=IO;
endmodule
module IDELAYCTRL(input wire REFCLK,RST, output reg RDY=0);
    always @(posedge REFCLK or posedge RST)
        if(RST) RDY<=0; else RDY<=1;
endmodule
module IDELAYE2 #(
    parameter CINVCTRL_SEL="FALSE", DELAY_SRC="IDATAIN",
    HIGH_PERFORMANCE_MODE="TRUE", IDELAY_TYPE="VAR_LOAD", IDELAY_VALUE=0,
    PIPE_SEL="FALSE", REFCLK_FREQUENCY=200.0, SIGNAL_PATTERN="DATA"
)(output wire DATAOUT, output wire [4:0] CNTVALUEOUT,
  input wire C,CE,CINVCTRL, input wire [4:0] CNTVALUEIN,
  input wire DATAIN,IDATAIN,INC,LD,LDPIPEEN,REGRST);
    assign #0.600 DATAOUT=IDATAIN;
    assign CNTVALUEOUT=IDELAY_VALUE;
endmodule
module IDDR #(parameter DDR_CLK_EDGE="SAME_EDGE", INIT_Q1=0, INIT_Q2=0,
    SRTYPE="ASYNC") (output reg Q1=INIT_Q1,Q2=INIT_Q2,
    input wire C,CE,D,R,S);
    reg negative=INIT_Q2;
    always @(negedge C or posedge R or posedge S)
        if(R) negative<=0; else if(S) negative<=1; else if(CE) negative<=D;
    always @(posedge C or posedge R or posedge S)
        if(R) begin Q1<=0;Q2<=0;end
        else if(S) begin Q1<=1;Q2<=1;end
        else if(CE) begin Q1<=D;Q2<=negative;end
endmodule
module ODDR #(parameter DDR_CLK_EDGE="SAME_EDGE", INIT=0,SRTYPE="ASYNC")
    (output wire Q,input wire C,CE,D1,D2,R,S);
    reg internal_q=INIT;
    // Includes ODDR clock-to-Q and FPGA clock-output-buffer propagation.
    assign #(`MT_PSRAM_CLOCK_OUTPUT_DELAY) Q=internal_q;
    reg second=0;
    always @(posedge C or posedge R or posedge S)
        if(R) begin internal_q<=0;second<=0;end
        else if(S) begin internal_q<=1;second<=1;end
        else if(CE) begin internal_q<=D1;second<=D2;end
    always @(negedge C) if(!R && !S && CE) internal_q<=second;
endmodule
