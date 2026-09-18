// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Four registered stages. Artwork alpha/opacity control the background;
// unattenuated artwork RGB tints the beam, which approaches white at high
// intensity. This intentionally avoids MiSTer's folded4-bit intermediate.
module multitini_vectrex_compositor (
    input wire clk, resetn, visible,
    input wire [7:0] intensity,
    input wire [15:0] rgba,
    input wire artwork_valid,
    input wire [4:0] video_config,
    output reg [15:0] rgb565
);
    function automatic [7:0] gain(input [7:0] value,input [1:0] level);
        reg [9:0] result;
        begin
            case(level)
                0:result={2'd0,value};
                1:result={1'd0,value,1'b0};
                2:result={1'd0,value,1'b0}+{2'd0,value};
                default:result={value,2'd0};
            endcase
            gain=result>255 ? 8'd255 : result[7:0];
        end
    endfunction
    function automatic [7:0] combine(input [11:0] background,input [15:0] beam,
        input [1:0] opacity,input bright,input enabled,input [7:0] gray);
        reg [14:0] scaled;
        reg [9:0] total;
        begin
            case(opacity)
                0:scaled={3'd0,background};
                1:scaled={2'd0,background,1'b0};
                2:scaled={2'd0,background,1'b0}+{3'd0,background};
                default:scaled={1'd0,background,2'd0};
            endcase
            total={1'b0,scaled[14:6]}+{2'd0,(bright ? 8'd255 : beam[15:8])};
            combine=!enabled ? gray : total>255 ? 8'd255 : total[7:0];
        end
    endfunction
    wire [7:0] boosted=gain(intensity,video_config[2:1]);
    wire art=artwork_valid && video_config[0];
    wire black=rgba[11:0]==0;
    reg [7:0] i0,w0,r0,g0,b0,gray1,gray2;
    reg [3:0] alpha0;
    reg [1:0] opacity0,opacity1,opacity2;
    reg active0,active1,active2,visible0,visible1,visible2,bright1,bright2;
    reg [11:0] background_r1,background_g1,background_b1;
    reg [11:0] background_r2,background_g2,background_b2;
    reg [15:0] color_r1,color_g1,color_b1,white_r1,white_g1,white_b1;
    reg [15:0] beam_r2,beam_g2,beam_b2;
    wire [7:0] result_r=combine(background_r2,beam_r2,opacity2,bright2,active2,gray2);
    wire [7:0] result_g=combine(background_g2,beam_g2,opacity2,bright2,active2,gray2);
    wire [7:0] result_b=combine(background_b2,beam_b2,opacity2,bright2,active2,gray2);
    always @(posedge clk) begin
        if(!resetn) begin
            i0<=0;w0<=0;r0<=0;g0<=0;b0<=0;alpha0<=0;opacity0<=0;
            active0<=0;visible0<=0;gray1<=0;opacity1<=0;active1<=0;visible1<=0;bright1<=0;
            gray2<=0;opacity2<=0;active2<=0;visible2<=0;bright2<=0;
            background_r1<=0;background_g1<=0;background_b1<=0;
            background_r2<=0;background_g2<=0;background_b2<=0;
            color_r1<=0;color_g1<=0;color_b1<=0;white_r1<=0;white_g1<=0;white_b1<=0;
            beam_r2<=0;beam_g2<=0;beam_b2<=0;rgb565<=0;
        end else begin
            i0<=boosted;w0<=boosted[7] ? {boosted[6:0],1'b1} : 8'd0;
            r0<=black ? 8'hff : {rgba[3:0],rgba[3:0]};
            g0<=black ? 8'hff : {rgba[7:4],rgba[7:4]};
            b0<=black ? 8'hff : {rgba[11:8],rgba[11:8]};
            alpha0<=black ? 4'd0 : rgba[15:12];
            opacity0<=video_config[4:3];active0<=art;visible0<=visible;
            background_r1<=r0*alpha0;background_g1<=g0*alpha0;background_b1<=b0*alpha0;
            color_r1<=i0*r0;white_r1<=w0*(8'd255-r0);
            color_g1<=i0*g0;white_g1<=w0*(8'd255-g0);
            color_b1<=i0*b0;white_b1<=w0*(8'd255-b0);
            gray1<=i0;bright1<=i0==255;opacity1<=opacity0;active1<=active0;visible1<=visible0;
            // Keep the product and sum in separate cycles at 148.5 MHz.
            beam_r2<=color_r1+white_r1;beam_g2<=color_g1+white_g1;beam_b2<=color_b1+white_b1;
            background_r2<=background_r1;background_g2<=background_g1;background_b2<=background_b1;
            gray2<=gray1;bright2<=bright1;opacity2<=opacity1;active2<=active1;visible2<=visible1;
            rgb565<=visible2 ? {result_r[7:3],result_g[7:2],result_b[7:3]} : 16'd0;
        end
    end
endmodule
