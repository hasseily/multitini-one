// SPDX-License-Identifier: MIT
/*MIT License

Copyright (c) 2019 Gregory Hogan (Soltan_G42)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.*/


// Derived from genesis_lpf.v and audio_iir_filter.v at MiSTer Mega Drive commit
// 083e1b1f4db8ffb32d6e8f83cf822fedc6184a61; reproduced by port_genesis_core.py.
// Shared state, the 504-clock sample interval, reset, truncation and bypass
// match the originals. Keep constant arithmetic before the mode selection
// so a parent-clock mode change does not launch through the DSP cascade.
module genesis_lpf_late_mux (
    input wire clk,
    input wire reset,
    input wire [1:0] lpf_mode,
    input wire signed [15:0] in,
    output wire signed [15:0] out
);
    reg signed [15:0] x0,x1,y0;
    reg [9:0] count;
    (* keep="true", use_dsp="yes" *) wire signed [33:0] acc_model1 =
        (18'sd10528*x0 - 18'sd5264*x1) + 18'sd27504*y0;
    (* keep="true", use_dsp="yes" *) wire signed [33:0] acc_model2 =
        (18'sd12888*x0 - 18'sd6440*x1) + 18'sd26328*y0;
    (* keep="true", use_dsp="yes" *) wire signed [33:0] acc_minimal =
        (18'sd6840*x0 + 18'sd6840*x1) + 18'sd19088*y0;
    (* keep="true" *) wire signed [33:0] acc_bypass = 18'sd32768*y0;
    reg signed [33:0] out32;
    always @* begin
        case (lpf_mode)
            2'b00: out32=acc_model1;
            2'b01: out32=acc_model2;
            2'b10: out32=acc_minimal;
            default: out32=acc_bypass;
        endcase
    end
    assign out=(lpf_mode==2'b11) ? in : y0;
    always @(posedge clk) begin
        if (reset) begin
            count<=0;
            x0<=0;
            x1<=0;
            y0<=0;
        end else begin
            count<=count+1'd1;
            if (count==10'd503) begin
                count<=0;
                y0<={out32[33],out32[29:15]};
                x1<=x0;
                x0<=in;
            end
        end
    end
endmodule
