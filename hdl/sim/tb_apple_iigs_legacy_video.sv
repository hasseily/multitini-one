// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Functional gate for the synthesized VGC. No copyrighted ROM or media data.
// Constant interiors cover mode selection. Address-dependent rows check the
// legacy graphics boundaries and distinguish the first and last memory cells.
module tb_apple_iigs_legacy_video;
    reg clk=0;
    always #5 clk=~clk;
    reg ce=0, load_we=0;
    reg [11:0] load_addr=0;
    reg [7:0] load_data=0;
    reg [9:0] h=0;
    reg [8:0] v=255;
    reg hires=1, an3=1, eighty=0, text_mode=0, mixed=0, shr=0, mono=0;
    reg shr640=0;
    reg [7:0] legacy_byte=8'hff;
    reg [7:0] legacy_aux_byte=8'hff;
    integer edge_case=0;
    integer edge_pixels=0;
    reg [11:0] shr_color=12'hfff;
    reg [7:0] video_data=0;
    wire [22:0] address;
    wire [7:0] red,green,blue;
    integer cases=0, total=0;

    vgc dut(.load_rom_clk(clk),.load_rom_we(load_we),.load_rom_addr(load_addr),
        .load_rom_data(load_data),.CLK_28M(clk),.CLK_14M(clk),.clk_vid(clk),
        .ce_pix(ce),.H(h),.V(v),.R(red),.G(green),.B(blue),.video_addr(address),
        .video_data(video_data),.TEXTCOLOR(8'hf0),.BORDERCOLOR(4'd2),
        .HIRES_MODE(hires),.AN3(an3),.STORE80(1'b0),.ALTCHARSET(1'b0),
        .EIGHTYCOL(eighty),.PAGE2(1'b0),.TEXTG(text_mode),.MIXG(mixed),
        .SHRG(shr),.DHRG_MONO(mono),.scanline_irq(),.vbl_irq(),.shr_bus_byte());

    function automatic [7:0] edge_byte(input integer column, input bit aux_bank);
        case (edge_case)
            1: case (column)
                0: edge_byte=8'h99;
                1: edge_byte=8'hcc;
                39: edge_byte=8'h66;
                default: edge_byte=0;
            endcase
            2: case (column)
                0: edge_byte=8'hcc;
                1: edge_byte=aux_bank ? 8'h33 : 8'hdd;
                39: edge_byte=aux_bank ? 8'h77 : 8'h11;
                default: edge_byte=0;
            endcase
            3,4: case (column)
                0: edge_byte=aux_bank ? 8'h55 : 8'h26;
                1: edge_byte=aux_bank ? 8'h15 : 8'h63;
                39: edge_byte=aux_bank ? 8'h4b : 8'h6d;
                default: edge_byte=0;
            endcase
            default: edge_byte=0;
        endcase
    endfunction

    // One CE-qualified synchronous video RAM read, like production slow RAM.
    always @(posedge clk) if(ce) begin
        if (!shr) video_data <= edge_case != 0 ? edge_byte(int'(address[6:0]),address[16]) :
                                               (address[16] ? legacy_aux_byte : legacy_byte);
        else if (address>=23'h19d00 && address<23'h19e00)
            video_data <= shr640 ? 8'h80 : 8'h00;
        else if (address>=23'h19e00 && address<23'h1a000)
            // Physical palette words are $0RGB: little-endian GB, then 0R.
            video_data <= address[0] ? {4'd0,shr_color[11:8]} : shr_color[7:0];
        else video_data <= 8'hff;
    end

    task check_edge_pixel(input integer h_position);
        localparam [63:0] DHR_LEFT=64'hFEEEEEEEEDDDDEEE;
        localparam [63:0] DHR_RIGHT=64'hFFF1199997777722;
        integer start_h, x, shift;
        reg checked;
        reg [3:0] index;
        reg [7:0] source_byte;
        reg [23:0] expected;
        begin
            start_h=edge_case<=2 ? 84 : edge_case==3 ? 83 : 89;
            x=h_position-start_h;
            checked=x>=-1 && x<=560;
            expected=0;
            if(x==-1 || x==560) expected=24'h000099;
            else if(x>=0 && x<560) begin
                case(edge_case)
                    1: case(x/14)
                        0: expected=24'hff6600;
                        1: expected=24'h00dd00;
                        39: expected=24'h2222ff;
                        default: expected=0;
                    endcase
                    2: case(x/7)
                        0: expected=24'hff6600;
                        1: expected=24'h00dd00;
                        2: expected=24'h2222ff;
                        3: expected=24'hffff00;
                        78: expected=24'h00ff99;
                        79: expected=24'hdd0033;
                        default: expected=0;
                    endcase
                    3: begin
                        source_byte=edge_byte(x/14,(x%14)<7);
                        expected=source_byte[x%7] ? 24'hffffff : 24'h000000;
                    end
                    4: begin
                        // Reference DHR windows use five bits before and after
                        // each group origin, with zeros beyond the 560-bit row.
                        checked=x<16 || x>=544;
                        shift=(15-(x%16))*4;
                        index=x<16 ? DHR_LEFT[shift+:4] : DHR_RIGHT[shift+:4];
                        case(index)
                            1: expected=24'hffff00;
                            2: expected=24'hff6600;
                            7: expected=24'h66aaff;
                            9: expected=24'h007722;
                            13: expected=24'h2222ff;
                            14: expected=24'h555555;
                            15: expected=24'h000000;
                            default: $fatal(1,"Unexpected DHR reference palette index %0d",index);
                        endcase
                    end
                    default: expected=0;
                endcase
            end
            if(checked) begin
                if({red,green,blue} !== expected)
                    $fatal(1,"LEGACY_EDGE_MISMATCH case=%0d H=%0d got=%h expected=%h",
                           edge_case,h_position,{red,green,blue},expected);
                edge_pixels=edge_pixels+1;
            end
        end
    endtask

    task scanline(input integer row, input bit check_pixels,
                  input [23:0] expected, input string label_text);
        integer column, checked;
        begin
            checked=0;
            for(column=0;column<912;column=column+1) begin
                @(negedge clk); h=10'(column); v=9'(row); ce=1;
                @(posedge clk); #2;
                if(check_pixels && edge_case!=0) check_edge_pixel(column);
                else if(check_pixels && column>=110 && column<=620) begin
                    if({red,green,blue} !== expected)
                        $fatal(1,"LEGACY_VIDEO_MISMATCH case=%s H=%0d V=%0d got=%h expected=%h",
                               label_text,column,row,{red,green,blue},expected);
                    checked=checked+1;
                end
                @(negedge clk); ce=0;
                repeat(2) @(negedge clk);
            end
            if(check_pixels && edge_case==0) begin
                if(checked!=511) $fatal(1,"LEGACY_VIDEO_SAMPLE_COUNT");
                cases=cases+1; total=total+checked;
                $display("PASS LEGACY_VIDEO_CASE %s samples=%0d",label_text,checked);
            end
        end
    endtask

    task check_line(input string label_text, input integer row, input [23:0] expected);
        begin
            // Prime previous-line state and SHR SCB/palette fetches naturally.
            scanline(row-1,0,expected,label_text);
            scanline(row,1,expected,label_text);
        end
    endtask

    integer index;
    initial begin
        // Allow Xilinx global set/reset to release, then upload synthetic glyphs.
        #1000;
        for(index=0;index<4096;index=index+1) begin
            @(negedge clk); load_we=1; load_addr=12'(index);
            // Character 1 is blank; all other characters have seven set pixels.
            load_data=((index>>3)&63)==1 ? 8'h00 : 8'h7f;
        end
        @(negedge clk); load_we=0;
        check_line("hgr-white",256,24'hffffff);
        legacy_byte=0; check_line("hgr-black",256,24'h000000);
        legacy_byte=8'hff; eighty=1;
        check_line("hgr-with-80col-an3",256,24'hffffff);
        eighty=0; hires=0; legacy_byte=8'h9c;
        check_line("lores-lower-nibble",256,24'h00dd00);
        check_line("lores-upper-nibble",260,24'hff6600);
        eighty=1; an3=0; legacy_byte=8'h99; legacy_aux_byte=8'hcc;
        check_line("double-lores",256,24'hff6600);
        hires=1; mono=1; legacy_byte=8'hff; legacy_aux_byte=legacy_byte;
        check_line("dhgr-mono-white",256,24'hffffff);
        legacy_byte=0; legacy_aux_byte=legacy_byte;
        check_line("dhgr-mono-black",256,24'h000000);
        mono=0; legacy_byte=8'hff; legacy_aux_byte=legacy_byte;
        check_line("dhgr-color-white",256,24'hffffff);
        legacy_byte=0; legacy_aux_byte=legacy_byte;
        check_line("dhgr-color-black",256,24'h000000);
        text_mode=1; eighty=0; an3=1; legacy_byte=8'hff;
        check_line("text40-white",256,24'hffffff);
        legacy_byte=8'hc1; check_line("text40-black",256,24'h000000);
        eighty=1; legacy_byte=8'hff; legacy_aux_byte=legacy_byte;
        check_line("text80-white",256,24'hffffff);
        legacy_byte=8'hc1; legacy_aux_byte=legacy_byte;
        check_line("text80-black",256,24'h000000);
        text_mode=0; eighty=0; mixed=1; legacy_byte=8'hff;
        check_line("mixed-hgr-top",256,24'hffffff);
        legacy_byte=8'hc1; check_line("mixed-text-bottom",416,24'h000000);
        mixed=0; shr=1; shr_color=12'hf60; shr640=0;
        check_line("shr320-orange",256,24'hff6600);
        shr_color=12'h22f; shr640=1;
        check_line("shr640-blue",256,24'h2222ff);
        shr=0; hires=0; eighty=0; an3=1; edge_case=1;
        check_line("lores-edges",256,0);
        eighty=1; an3=0; edge_case=2;
        check_line("double-lores-edges",256,0);
        hires=1; mono=1; edge_case=3;
        check_line("dhgr-mono-edges",256,0);
        mono=0; edge_case=4;
        check_line("dhgr-color-edges",256,0);
        eighty=0; an3=1; edge_case=5;
        check_line("hgr-edges",256,0);
        if(edge_pixels!=2282) $fatal(1,"LEGACY_VIDEO_EDGE_COUNT %0d",edge_pixels);
        $display("PASS LEGACY_VIDEO_EDGES samples=%0d",edge_pixels);
        if(cases!=18 || total!=9198) $fatal(1,"LEGACY_VIDEO_TOTAL_COUNT");
        $display("PASS LEGACY_VIDEO_COMPLETE cases=%0d samples=%0d",cases,total);
        $finish;
    end
    initial begin #10000000; $fatal(1,"LEGACY_VIDEO_TIMEOUT"); end
endmodule
