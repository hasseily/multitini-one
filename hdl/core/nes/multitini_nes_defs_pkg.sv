// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
package multitini_nes_defs_pkg;
    localparam logic [31:0] MT_NES_CORE_ID=32'h4e455320;
    localparam integer MT_NES_CORE_HZ=42954544;
    localparam logic [7:0] MT_LOAD_NES_CART=8'h50, MT_LOAD_NES_SAVE=8'h51;
    localparam integer MT_SIZE_NES_SAVE=8192;
    localparam logic [23:0] MT_NES_CHR_BASE=24'h200000;
    localparam logic [23:0] MT_NES_SAVE_BASE=24'h3c0000;
    function automatic logic nes_header_ok(input logic [127:0] h,input logic [31:0] total);
        logic [7:0] p,c,m;
        logic bounds;
        begin
            p=h[39:32]; c=h[47:40]; m={h[63:60],h[55:52]}; bounds=0;
            case(m)
                0: bounds=p<=2&&c<=1;
                1: bounds=p<=16&&c<=16;
                2: bounds=p<=16&&c==0;
                3: bounds=p<=2&&c>=1&&c<=4;
                4: bounds=p<=32&&c<=32;
                7: bounds=p>=2&&p<=16&&c==0;
                9: bounds=p==8&&c<=16;
                10:bounds=p<=16&&c<=16;
                11:bounds=p<=8&&c<=16;
                66:bounds=p<=8&&c<=4;
                71:bounds=p<=16&&c==0;
                default:bounds=0;
            endcase
            nes_header_ok=h[31:0]==32'h1a53454e && p!=0 && (p&(p-1'b1))==0 &&
                (c==0 || (c&(c-1'b1))==0) && h[51:50]==0 && h[59:56]==0 &&
                h[71:64]<=1 && h[127:72]==0 && bounds &&
                total==32'd16+{10'd0,p,14'd0}+{11'd0,c,13'd0};
        end
    endfunction
endpackage
