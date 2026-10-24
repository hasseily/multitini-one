// SPDX-License-Identifier: GPL-3.0-only
// slot_next names the next retiring fast edge, so the registered grant is
// stable before that cycle's falling-edge BRAM access. Credits consume only
// grant_q, never an earlier/later lookup of the same grant.
module multitini_c64_island_governor (
    input wire clk, reset, turbo, policy_changed,
    input wire [3:0] rate_index,
    input wire c64u_top,
    input wire [5:0] slot_next,
    input wire frame_run_next,
    input wire forward_wait, completion_enable,
    output reg grant_q = 0,
    output reg credit_q = 0
);
    function automatic integer rate(input [3:0] index, input u64);
        case (index)
            0:rate=1; 1:rate=2; 2:rate=3; 3:rate=4;
            4:rate=6; 5:rate=8; 6:rate=10; 7:rate=12;
            8:rate=14; 9:rate=16; 10:rate=20; 11:rate=24;
            12:rate=32; 13:rate=40; 14:rate=u64?47:48;
            default:rate=u64?63:64;
        endcase
    endfunction
    function automatic [63:0] grant_row(input integer n);
        reg [63:0] raw, result;
        integer acc, s, first, rotate;
        begin
            raw=0; result=0; acc=0; first=-1;
            for(s=0;s<64;s=s+1) begin
                acc=acc+n;
                if(acc>=64) begin
                    acc=acc-64; raw[s]=1;
                    if(first<0) first=s;
                end
            end
            rotate=(5-first+64)%64;
            for(s=0;s<64;s=s+1) result[s]=raw[(s-rotate+64)%64];
            if(n==63) result=~64'h10;
            if(n==64) result=~64'h0;
            grant_row=result;
        end
    endfunction
    reg [63:0] table_row;
    always_comb begin
        // Constant calls produce an 18x64 ROM, not a runtime divider.
        case(rate_index)
            0:table_row=grant_row(1); 1:table_row=grant_row(2);
            2:table_row=grant_row(3); 3:table_row=grant_row(4);
            4:table_row=grant_row(6); 5:table_row=grant_row(8);
            6:table_row=grant_row(10); 7:table_row=grant_row(12);
            8:table_row=grant_row(14); 9:table_row=grant_row(16);
            10:table_row=grant_row(20); 11:table_row=grant_row(24);
            12:table_row=grant_row(32); 13:table_row=grant_row(40);
            14:table_row=c64u_top?grant_row(47):grant_row(48);
            default:table_row=c64u_top?grant_row(63):grant_row(64);
        endcase
    end
    always @(posedge clk) begin
        if(reset || !turbo || policy_changed) begin
            grant_q<=0; credit_q<=0;
        end else begin
            grant_q<=frame_run_next && table_row[slot_next];
            if(completion_enable) credit_q<=0;
            else if(forward_wait && grant_q) credit_q<=1;
        end
    end
endmodule
