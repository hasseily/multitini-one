// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_genesis_vdp_pair;
    reg MCLK=0;always #5 MCLK=~MCLK;
    reg [5:0] inputs=0;
    wire out12,out13,out15;
    multitini_genesis_vdp_bus_pair dut(.MCLK(MCLK),
        .reset_comb(inputs[5]),.dff21(inputs[4]),
        .cpu_as(inputs[3]),.w2(inputs[2]),.dff14(inputs[1]),.w44(inputs[0]),
        .out12(out12),.out13(out13),.out15(out15));
    integer vectors,fd,scan,word_value;
    initial begin
        fd=$fopen("vdp-pair-vectors.txt","r");
        if(!fd)$fatal(1,"Missing exhaustive pair vectors");
        vectors=0;
        while(!$feof(fd))begin
            scan=$fscanf(fd,"%h\n",word_value);
            if(scan==1)begin
                @(negedge MCLK);inputs=word_value[5:0];#1;
                if({out12,out15,out13}!==word_value[8:6])$fatal(1,"VDP transition/pulse mismatch at vector %0d",vectors);
                @(posedge MCLK);#1;
                if({dut.l1_13,dut.l2_13,dut.l1_15,dut.l2_15,dut.l1_12,dut.l2_12}!==word_value[14:9])
                    $fatal(1,"VDP latch transition mismatch at vector %0d",vectors);
                vectors=vectors+1;
            end
        end
        $fclose(fd);
        if(vectors<704)$fatal(1,"Missing reachable transitions");
        $display("PASS Genesis acyclic VDP pair: %0d exhaustive reachable-state vectors",vectors);
        $finish;
    end
endmodule
