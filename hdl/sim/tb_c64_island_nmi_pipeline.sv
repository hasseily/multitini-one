// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module tb_c64_island_nmi_pipeline;
    parameter EXTRA_LAG=0;
    reg clk=0,reset=1,turbo=0,t65_enable=0;
    reg native_pin=1,live_nmi_n=1,late_nmi_n=1,exact_nmi_n=1;
    wire t65_nmi_n,exact_switch_safe,pending_rearm,carry_active,used_hold;
    multitini_c64_nmi_delivery dut(.clk(clk),.reset(reset),.turbo(turbo),
        .t65_enable(t65_enable),.live_nmi_n(EXTRA_LAG?late_nmi_n:live_nmi_n),
        .exact_nmi_n(exact_nmi_n),.t65_nmi_n(t65_nmi_n),
        .exact_switch_safe(exact_switch_safe),.pending_rearm(pending_rearm),
        .carry_active(carry_active),.used_hold(used_hold));
    always #5 clk=~clk;
    // Match the real island's fast-domain input register. Native state changes
    // below occur after the clock, as a native-domain nonblocking assignment.
    // Thus the detector sees the two-clock lag counted by DESIGN section5.9.
    always @(posedge clk) begin live_nmi_n<=native_pin;late_nmi_n<=live_nmi_n;end
    integer fd,n,packed_value,rc,checks=0,cases=0,ticks=0;
    reg [1023:0] path;
    reg last_shown=1;
    integer edges=0;
    initial begin
        if(!$value$plusargs("VECTORS=%s",path)) $fatal(1,"missing vectors");
        fd=$fopen(path,"r");if(!fd)$fatal(1,"cannot open vectors");
        while(!$feof(fd)) begin
            rc=$fscanf(fd,"%d %h\n",n,packed_value);
            if(rc!=2)$fatal(1,"malformed vector row");
            if(n==0) begin
                @(negedge clk);reset=1;turbo=0;t65_enable=0;
                native_pin=packed_value[0];exact_nmi_n=packed_value[0];
                repeat(4) @(negedge clk);
                reset=0;t65_enable=1;
                @(posedge clk);#0.01;t65_enable=0;
                last_shown=packed_value[0];cases++;ticks=0;
            end else begin
                repeat(n) begin
                    @(negedge clk);turbo=packed_value[2];t65_enable=packed_value[3];
                    exact_nmi_n=packed_value[1];
                    @(posedge clk);
                    if(t65_enable) begin
                        checks++;
                        if(t65_nmi_n!==packed_value[4])
                            $fatal(1,"NMI_PIPELINE mismatch case=%0d tick=%0d shown=%b expected=%b live=%b B0=%b turbo=%b carry=%b force=%b",cases,ticks,t65_nmi_n,packed_value[4],live_nmi_n,exact_nmi_n,turbo,carry_active,pending_rearm);
                        if(last_shown&&!t65_nmi_n)edges++;
                        last_shown=t65_nmi_n;
                    end
                    #0.01;native_pin=packed_value[0];ticks++;
                end
            end
        end
        $fclose(fd);
        $display("NMI_PIPELINE PASS cases=%0d checks=%0d edges=%0d",cases,checks,edges);$finish;
    end
endmodule
