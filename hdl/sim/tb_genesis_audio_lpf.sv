// SPDX-License-Identifier: GPL-3.0-only
// Differential pinned-upstream and independent 64-bit arithmetic/state oracle.
`timescale 1ns/1ps
module tb_genesis_audio_lpf;
    reg clk=0;
    always #9.397 clk=~clk;
    reg reset=1;
    reg [1:0] mode=0;
    reg signed [15:0] sample=0;
    wire signed [15:0] old_out,new_out;
    genesis_lpf original(.clk(clk),.reset(reset),.lpf_mode(mode),.in(sample),.out(old_out));
    genesis_lpf_late_mux candidate(.clk(clk),.reset(reset),.lpf_mode(mode),.in(sample),.out(new_out));
    integer checks=0,updates=0,resets=0;
    reg [31:0] random_state=32'hC08A417D;
    function [31:0] random_word;
        reg [31:0] v;
        begin
            v=random_state;
            v=v^(v<<13);v=v^(v>>17);v=v^(v<<5);
            random_state=v;
            random_word=v;
        end
    endfunction
    function signed [15:0] value(input integer index);
        begin
            case(index%12)
                0:value=16'sh8000;1:value=16'sh8001;
                2:value=-16'sd1;3:value=16'sd0;4:value=16'sd1;
                5:value=16'sh7fff;6:value=16'sh7ffe;
                default:value=random_word();
            endcase
        end
    endfunction
    function signed [33:0] oracle_acc(input [1:0] m,input signed [15:0] a,b,c);
        reg signed [63:0] aa,bb,cc,result;
        begin
            aa=a;bb=b;cc=c;
            case(m)
                0:result=64'sd10528*aa-64'sd5264*bb+64'sd27504*cc;
                1:result=64'sd12888*aa-64'sd6440*bb+64'sd26328*cc;
                2:result=64'sd6840*aa+64'sd6840*bb+64'sd19088*cc;
                3:result=64'sd32768*cc;
            endcase
            oracle_acc=result[33:0];
        end
    endfunction
    task compare;
        reg signed [33:0] expected;
        begin
            expected=oracle_acc(mode,candidate.x0,candidate.x1,candidate.y0);
            if ({candidate.count,candidate.x0,candidate.x1,candidate.y0} !==
                {original.lpf6db.count,original.lpf6db.x0,original.lpf6db.x1,original.lpf6db.y0})
                $fatal(1,"state mismatch check %0d",checks);
            if (new_out !== old_out || candidate.out32 !== original.lpf6db.out32 || candidate.out32 !== expected)
                $fatal(1,"arithmetic/output mismatch check %0d mode %0d out=%h/%h acc=%h/%h/%h coeffs=%h/%h/%h",checks,mode,new_out,old_out,candidate.out32,original.lpf6db.out32,expected,original.A2,original.B1,original.B2);
            checks=checks+1;
        end
    endtask
    task edge_check;
        reg [9:0] prior_count;
        reg signed [15:0] prior_x0,prior_x1,prior_y0;
        reg signed [33:0] expected;
        begin
            #1;
            compare();
            prior_count=candidate.count;
            prior_x0=candidate.x0;prior_x1=candidate.x1;prior_y0=candidate.y0;
            expected=oracle_acc(mode,prior_x0,prior_x1,prior_y0);
            @(posedge clk);#1;
            compare();
            if(reset) begin
                if({candidate.count,candidate.x0,candidate.x1,candidate.y0} !== 58'd0)
                    $fatal(1,"reset mismatch");
                resets=resets+1;
            end else if(prior_count==503) begin
                if(candidate.count!==0 || candidate.x0!==sample || candidate.x1!==prior_x0 ||
                   candidate.y0!=={expected[33],expected[29:15]}) $fatal(1,"sample update mismatch");
                updates=updates+1;
            end else begin
                if(candidate.count!==(prior_count+10'd1) ||
                   {candidate.x0,candidate.x1,candidate.y0}!=={prior_x0,prior_x1,prior_y0})
                    $fatal(1,"hold mismatch");
            end
        end
    endtask
    integer i;
    initial begin
        #0.1;mode=1;
        #0.1;mode=0;
        repeat(2) @(posedge clk);
        #1;compare();
        // Full uninterrupted counter epochs; change mode and signed samples
        // before every rising edge, including all reset/sample boundaries.
        for(i=0;i<1200000;i=i+1) begin
            @(negedge clk);
            mode=random_word();sample=value(i);
            reset=(i<8 || i%10007==10006 || i%17011==503 || i%23003==502);
            edge_check();
            // Mid-cycle bypass changes must remain immediately observable.
            mode=(mode+1)&3;sample=value(i+7);
            #1;compare();
        end
        // Exercise arbitrary signed feedback states, including overflow edges,
        // immediately before a sample, before a hold, and before a reset.
        for(i=0;i<100000;i=i+1) begin
            @(negedge clk);
            reset=(i%31==0);mode=i%4;sample=value(i+9);
            candidate.x0=value(i);original.lpf6db.x0=candidate.x0;
            candidate.x1=value(i+3);original.lpf6db.x1=candidate.x1;
            candidate.y0=value(i+6);original.lpf6db.y0=candidate.y0;
            candidate.count=(i%7==0) ? 502 : 503;
            original.lpf6db.count=candidate.count;
            edge_check();
        end
        $display("PASS audio late-mux differential and independent oracle: %0d comparisons, %0d sample updates, %0d reset edges; 1.2M sequential cycles plus 100k arbitrary signed states.",checks,updates,resets);
        $finish;
    end
endmodule
