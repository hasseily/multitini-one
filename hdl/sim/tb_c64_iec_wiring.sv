// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Focused production-adapter wiring test. Unrelated imported leaves are
// black boxes; only the three registered drivers are forced. The actual
// adapter expressions,1541 input ports,endpoint inputs and guard are retained.
module tb_c64_iec_wiring;
    logic clk=0;
    always #5 clk=~clk;
    logic [2:0] data_release=7,clock_release=7;
    logic atn_release=1;
    multitini_c64_adapter dut(.core_clk(clk),.cpu_clk(clk),.reset(1'b1));
    always @(data_release or clock_release or atn_release) begin
        force dut.c64_iec_data=data_release[0];
        force dut.drive_iec_data=data_release[1];
        force dut.iec9_data=data_release[2];
        force dut.c64_iec_clk=clock_release[0];
        force dut.drive_iec_clk=clock_release[1];
        force dut.iec9_clk=clock_release[2];
        force dut.c64_iec_atn=atn_release;
        force dut.c64_pc2_n=1'b1;
        force dut.drive_par_stb_out=1'b1;
    end
    initial begin
        for(integer d=0;d<8;d++)for(integer c=0;c<8;c++)begin
            data_release=d;clock_release=c;#2;
            if(dut.drive_i.c64_iec_data!==(&data_release) ||
               dut.drive_i.c64_iec_clk!==(&clock_release))
                $fatal(1,"1541 does not see complete physical wired-AND bus d=%d c=%d",d,c);
            if(dut.iec9_i.bus_data!==(&data_release) ||dut.iec9_i.bus_clk!==(&clock_release))
                $fatal(1,"IEC9 does not see complete physical wired-AND bus");
            if(dut.peripheral_iec_data!==(&data_release[2:1]) ||
               dut.peripheral_iec_clk!==(&clock_release[2:1]))
                $fatal(1,"C64 external input omits a peripheral");
            if(dut.cpu_turbo_guard_i.iec_lines_released && (d!=7||c!=7))
                $fatal(1,"turbo guard ignored a pulled-low driver");
            if(d==7&&c==7&&dut.cpu_turbo_guard_i.iec_lines_released!==1)
                $fatal(1,"turbo guard did not see idle physical bus");
        end
        data_release=7;clock_release=7;atn_release=0;#2;
        if(dut.drive_i.c64_iec_atn!==0||dut.iec9_i.atn!==0||dut.cpu_turbo_guard_i.iec_lines_released)
            $fatal(1,"ATN not shared with drive9/drive8/turbo guard");
        $display("PASS IEC9 production wiring64 combinations all-driver readback+guard");$finish;
    end
endmodule
