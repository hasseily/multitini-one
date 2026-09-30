// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns / 1ps

// Decorative pixels outside the supplied game viewport. Three-pixel lookahead
// matches the three registered arithmetic/style stages without delaying game,
// UI, or DVI timing. Signed edge distances clip safely at every screen edge.
module multitini_border_overlay #(
    parameter bit DYNAMIC_DISPLAY=1'b0,
    parameter integer H_ACTIVE=1920, H_TOTAL=2200,
    parameter integer V_ACTIVE=1080, V_TOTAL=1125
) (
    input wire [11:0] display_h_active,display_h_total,display_v_active,display_v_total,
    input logic pixel_clk, resetn,
    input logic [11:0] scan_x, scan_y,
    input logic [12:0] frame_config,
    input logic [11:0] viewport_x, viewport_y, viewport_width, viewport_height,
    output logic pixel_opaque,
    output logic [15:0] pixel_rgb565
);
    wire [11:0] h_active=DYNAMIC_DISPLAY ? display_h_active : H_ACTIVE;
    wire [11:0] h_total=DYNAMIC_DISPLAY ? display_h_total : H_TOTAL;
    wire [11:0] v_active=DYNAMIC_DISPLAY ? display_v_active : V_ACTIVE;
    wire [11:0] v_total=DYNAMIC_DISPLAY ? display_v_total : V_TOTAL;
    logic signed [13:0] left_edge, right_edge, top_edge, bottom_edge;
    logic [11:0] fetch_x, fetch_y;
    logic signed [13:0] dl_q, dr_q, dt_q, db_q;
    logic signed [13:0] distance_qq;
    logic active_q, active_qq;
    logic corner_qq, dash_qq;
    logic [2:0] style_q, style_qq;
    logic [23:0] color_q, color_qq, shaded;
    logic draw_pixel;
    logic [4:0] padding;

    function automatic logic [23:0] palette(input logic [3:0] index);
        case (index)
        0: palette=24'h30343B; 1: palette=24'h424D88;
        2: palette=24'h4B7455; 3: palette=24'h497C80;
        4: palette=24'h8C5050; 5: palette=24'h815A86;
        6: palette=24'h8B7150; 7: palette=24'h9B9DA2;
        8: palette=24'h575C65; 9: palette=24'h758EC2;
        10: palette=24'h86AB83; 11: palette=24'h7CACB0;
        12: palette=24'hBA8181; 13: palette=24'hAE87B3;
        14: palette=24'hBBB184; default: palette=24'hC7C8CA;
        endcase
    endfunction
    function automatic logic signed [13:0] max2(
        input logic signed [13:0] a,b);
        max2=a>b ? a:b;
    endfunction
    always_comb begin
        padding=frame_config[12:8];
        if (padding==0) padding=1;
        else if (padding>20) padding=20;
        // Exactly 'padding' untouched pixels separate the image and line.
        left_edge=$signed({2'b0,viewport_x})-$signed({9'b0,padding})-14'sd1;
        right_edge=$signed({2'b0,viewport_x})+$signed({2'b0,viewport_width})+
                   $signed({9'b0,padding});
        top_edge=$signed({2'b0,viewport_y})-$signed({9'b0,padding})-14'sd1;
        bottom_edge=$signed({2'b0,viewport_y})+$signed({2'b0,viewport_height})+
                    $signed({9'b0,padding});
        fetch_x=scan_x+12'd3; fetch_y=scan_y;
        if (scan_x>=h_total-3) begin
            fetch_x=scan_x-(h_total-3);
            fetch_y=scan_y==v_total-1 ? 12'd0 : scan_y+12'd1;
        end
        draw_pixel=0; shaded=color_qq;
        case (style_qq)
        1: draw_pixel=distance_qq==0 || distance_qq==3;
        2: draw_pixel=distance_qq==0 && corner_qq;
        3: draw_pixel=distance_qq==0 && dash_qq;
        4: begin
            draw_pixel=distance_qq>=0 && distance_qq<=3;
            case (distance_qq)
            1: shaded={1'b0,color_qq[23:17],1'b0,color_qq[15:9],1'b0,color_qq[7:1]};
            2: shaded={2'b0,color_qq[23:18],2'b0,color_qq[15:10],2'b0,color_qq[7:2]};
            3: shaded={3'b0,color_qq[23:19],3'b0,color_qq[15:11],3'b0,color_qq[7:3]};
            default: ;
            endcase
        end
        default: draw_pixel=distance_qq==0;
        endcase
    end
    always_ff @(posedge pixel_clk) begin
        if (!resetn) begin
            dl_q<=0; dr_q<=0; dt_q<=0; db_q<=0; distance_qq<=0;
            active_q<=0; active_qq<=0; corner_qq<=0; dash_qq<=0;
            style_q<=0; style_qq<=0; color_q<=0; color_qq<=0;
            pixel_opaque<=0; pixel_rgb565<=0;
        end else begin
            dl_q<=left_edge-$signed({2'b0,fetch_x});
            dr_q<=$signed({2'b0,fetch_x})-right_edge;
            dt_q<=top_edge-$signed({2'b0,fetch_y});
            db_q<=$signed({2'b0,fetch_y})-bottom_edge;
            active_q<=frame_config[0] && viewport_width!=0 && viewport_height!=0 &&
                      fetch_x<h_active && fetch_y<v_active;
            style_q<=frame_config[3:1]; color_q<=palette(frame_config[7:4]);
            distance_qq<=max2(max2(dl_q,dr_q),max2(dt_q,db_q));
            active_qq<=active_q; style_qq<=style_q; color_qq<=color_q;
            corner_qq<=((dt_q==0 || db_q==0) && (dl_q>=-23 || dr_q>=-23)) ||
                       ((dl_q==0 || dr_q==0) && (dt_q>=-23 || db_q>=-23));
            // Sixteen-pixel dash period: eight lit and eight clear. Using the
            // low bits of negative distance keeps the phase edge-relative.
            dash_qq<=(dt_q==0 || db_q==0) ? !dl_q[3] : !dt_q[3];
            pixel_opaque<=active_qq && draw_pixel;
            pixel_rgb565<={shaded[23:19],shaded[15:10],shaded[7:3]};
        end
    end
endmodule
