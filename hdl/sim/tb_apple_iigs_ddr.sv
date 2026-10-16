// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// IIgs DDR path, memory interface v2. The AXI master runs on the machine
// clock; only the drain summary crosses into control_clk. Quiescence needs
// the core reset request in both domains, and a machine reset in the middle
// of an AXI transaction must finish that transaction without a pulse.
module tb_apple_iigs_ddr;
    localparam real CORE_HALF=8.73,CONTROL_HALF=3.75;
    localparam real CONTROL_PERIOD=2*CONTROL_HALF,CORE_PERIOD=2*CORE_HALF;
    localparam integer STOP_HOLD=15;
    reg core_clk=0,control_clk=0;
    always #(CORE_HALF) core_clk=~core_clk;
    always #(CONTROL_HALF) control_clk=~control_clk;
    reg core_resetn=0,control_resetn=0;
    reg control_reset_request=0,core_reset_request=0;
    reg [23:0] core_memory_addr=0;
    reg core_memory_write=0,core_memory_valid=0,core_memory_burst=0,core_memory_busy=0;
    reg [63:0] core_memory_wdata=0;
    reg [7:0] core_memory_wstrb=0;
    wire core_memory_ready,core_memory_rvalid,core_memory_fault,quiescent;
    wire [63:0] core_memory_rdata;
    wire [31:0] araddr,awaddr;wire [3:0] arlen,awlen;
    wire [2:0] arsize,awsize;wire [1:0] arburst,awburst;
    wire arvalid,rready,awvalid,wvalid,wlast,bready;
    wire [63:0] wdata;wire [7:0] wstrb;
    reg arready=0,awready=0,wready=0,rvalid=0,rlast=0,bvalid=0;
    reg [63:0] rdata=0;reg [1:0] rresp=0,bresp=0;
    multitini_apple_iigs_ddr_path dut(.*);

    // ---------------- AXI slave model (HP2 on the machine clock) ----------
    // The PS7 AFI is not reset by the PL machine reset, so this model never
    // resets: it pairs AW with W and returns B/R exactly as the AFI would.
    localparam integer WORDS=256;
    reg [63:0] ram[0:WORDS-1];
    integer slave_seed=32'h1dd5;
    integer stall_percent=50;     // ready/valid stall probability
    integer rd_delay=0,rd_beat=0,rd_len=0,wr_delay=0;
    reg rd_active=0,have_aw=0,have_w=0;
    reg [31:0] rd_addr=0,aw_saved=0;
    reg [63:0] w_saved=0;reg [7:0] strobe_saved=0;
    // Error injection for the next transaction only.
    reg inject_rresp=0,inject_bresp=0,inject_missing_rlast=0,inject_early_rlast=0;
    // Directed holds: keep a ready low, or withhold R beats or B.
    reg hold_ar=0,hold_aw=0,hold_w=0,hold_r=0,hold_b=0;
    integer ar_handshakes=0,aw_handshakes=0,w_handshakes=0,b_handshakes=0,r_handshakes=0;
    function automatic bit stall;
        stall=($unsigned($random(slave_seed))%100)<stall_percent;
    endfunction
    function automatic integer pick(input integer range);
        pick=$unsigned($random(slave_seed))%range;
    endfunction
    always @(posedge core_clk)begin
        arready<=!rd_active && !stall() && !hold_ar;
        awready<=!have_aw && !bvalid && !stall() && !hold_aw;
        wready<=!have_w && !bvalid && !stall() && !hold_w;
        if(arvalid && arready)begin
            if(rd_active || have_aw || have_w || bvalid)$fatal(1,"read issued with another transaction outstanding");
            if(araddr[31:24]!=8'h24 || arsize!=3 || arburst!=1)$fatal(1,"read bounds/attributes %h",araddr);
            if(arlen==3 ? araddr[4:0]!=0 : (arlen!=0 || araddr[2:0]!=0))$fatal(1,"read length/alignment %h len %0d",araddr,arlen);
            rd_active<=1;rd_addr<=araddr;rd_len<=arlen;rd_beat<=0;rd_delay<=pick(8);
            ar_handshakes<=ar_handshakes+1;
        end
        if(rvalid && rready)begin
            rvalid<=0;r_handshakes<=r_handshakes+1;
            if(rd_beat==rd_len || (inject_early_rlast && rd_beat==1))begin
                rd_active<=0;inject_rresp<=0;inject_missing_rlast<=0;inject_early_rlast<=0;
            end
            rd_beat<=rd_beat+1;rd_delay<=pick(4);
        end else if(rd_active && !rvalid && !hold_r)begin
            if(rd_delay!=0)rd_delay<=rd_delay-1;
            else begin
                rvalid<=1;rdata<=ram[(rd_addr[10:3]+rd_beat)%WORDS];
                rresp<=inject_rresp && rd_beat==0 ? 2'd2 : 2'd0;
                rlast<=inject_early_rlast ? rd_beat==1 :
                    inject_missing_rlast ? 1'b0 : rd_beat==rd_len;
            end
        end
        if(awvalid && awready)begin
            if(rd_active || have_aw || bvalid)$fatal(1,"write address issued with another transaction outstanding");
            if(awaddr[31:24]!=8'h24 || awlen!=0 || awsize!=3 || awburst!=1 || awaddr[2:0]!=0)$fatal(1,"write bounds/attributes %h",awaddr);
            have_aw<=1;aw_saved<=awaddr;aw_handshakes<=aw_handshakes+1;wr_delay<=pick(6);
        end
        if(wvalid && wready)begin
            if(rd_active || have_w || bvalid)$fatal(1,"write data issued with another transaction outstanding");
            if(!wlast)$fatal(1,"write missing WLAST");
            have_w<=1;w_saved<=wdata;strobe_saved<=wstrb;w_handshakes<=w_handshakes+1;
        end
        if(have_aw && have_w && !bvalid && !hold_b)begin
            if(wr_delay!=0)wr_delay<=wr_delay-1;
            else begin
                for(integer i=0;i<8;i=i+1)if(strobe_saved[i])ram[aw_saved[10:3]][i*8+:8]<=w_saved[i*8+:8];
                bvalid<=1;bresp<=inject_bresp ? 2'd2 : 2'd0;have_aw<=0;have_w<=0;
            end
        end
        if(bvalid && bready)begin bvalid<=0;b_handshakes<=b_handshakes+1;inject_bresp<=0;end
    end

    // AXI master stability: a valid may not drop or change before ready, and
    // that includes every cycle of a machine reset.
    reg prev_arvalid=0,prev_awvalid=0,prev_wvalid=0;
    reg prev_arready=0,prev_awready=0,prev_wready=0;
    reg [31:0] prev_araddr,prev_awaddr;reg [3:0] prev_arlen;reg [63:0] prev_wdata;reg [7:0] prev_wstrb;
    always @(posedge core_clk)begin
        if(prev_arvalid && !prev_arready && (!arvalid || araddr!=prev_araddr || arlen!=prev_arlen))$fatal(1,"AR changed before ARREADY");
        if(prev_awvalid && !prev_awready && (!awvalid || awaddr!=prev_awaddr))$fatal(1,"AW changed before AWREADY");
        if(prev_wvalid && !prev_wready && (!wvalid || wdata!=prev_wdata || wstrb!=prev_wstrb))$fatal(1,"W changed before WREADY");
        if(!rready || !bready)$fatal(1,"response ready dropped");
        prev_arvalid<=arvalid;prev_awvalid<=awvalid;prev_wvalid<=wvalid;
        prev_arready<=arready;prev_awready<=awready;prev_wready<=wready;
        prev_araddr<=araddr;prev_arlen<=arlen;prev_awaddr<=awaddr;prev_wdata<=wdata;prev_wstrb<=wstrb;
    end

    // ---------------- Requester-side monitor ------------------------------
    // The requester shares the machine reset: reset discards its request, so
    // any later pulse for that request is a protocol error.
    reg outstanding=0,request_write=0,request_burst=0;
    reg [23:0] request_addr=0;
    integer expected_beats=0,response_count=0,b_at_request=0,pulses=0;
    reg [63:0] response_data[0:3];
    always @(posedge core_clk)begin
        if(core_memory_rvalid)begin
            if(!outstanding)$fatal(1,"response pulse without an outstanding request");
            if(request_write && b_handshakes==b_at_request)$fatal(1,"write completion before B response");
            response_data[response_count]<=core_memory_rdata;
            response_count<=response_count+1;pulses<=pulses+1;
            if(response_count+1==expected_beats)outstanding<=0;
        end
        if(core_memory_valid && core_memory_ready)begin
            if(outstanding)$fatal(1,"bench raised a request before the final response");
            outstanding<=1;response_count<=0;request_addr<=core_memory_addr;
            request_write<=core_memory_write;request_burst<=core_memory_burst && !core_memory_write;
            expected_beats<=core_memory_write || !core_memory_burst ? 1 : 4;
            b_at_request<=b_handshakes;
        end
        // One outstanding transaction: the DUT stays busy until its final pulse.
        if(core_resetn && outstanding && core_memory_ready && !core_memory_rvalid && response_count!=expected_beats)
            $fatal(1,"DUT idle with %0d of %0d responses",response_count,expected_beats);
        if(!core_resetn)outstanding<=0;
    end
    always @(posedge core_clk)if(arvalid && arready && outstanding)begin
        if(araddr!=(32'h24000000|(request_burst ? {8'd0,request_addr[23:5],5'd0} : {8'd0,request_addr[23:3],3'd0})))
            $fatal(1,"AR address %h for request %h burst %0d",araddr,request_addr,request_burst);
    end
    always @(posedge core_clk)if(awvalid && awready && outstanding)begin
        if(awaddr!=(32'h24000000|{8'd0,request_addr[23:3],3'd0}))$fatal(1,"AW address %h for request %h",awaddr,request_addr);
    end

    // ---------------- Quiescence monitor ----------------------------------
    // Busy as visible at the DUT boundary on each machine edge.
    real last_busy=0,busy_since=-1;
    real control_request_since=-1,core_request_since=-1;
    reg check_quiet=0;
    always @(posedge core_clk)begin
        if(!core_resetn || core_memory_busy || core_memory_valid || !core_memory_ready || core_memory_rvalid)begin
            last_busy=$realtime;
            if(busy_since<0)busy_since=$realtime;
        end else busy_since=-1;
        if(!core_reset_request)core_request_since=-1;
        else if(core_request_since<0)core_request_since=$realtime;
    end
    reg was_quiescent=0;
    always @(posedge control_clk)begin
        was_quiescent<=quiescent;
        // Drain may only be reported after a complete quiet window.
        if(check_quiet && control_resetn && quiescent && !was_quiescent && $realtime-last_busy<15*CONTROL_PERIOD)
            $fatal(1,"quiescent %0.2f ns after machine-side busy",$realtime-last_busy);
        if(check_quiet && control_resetn && quiescent && busy_since>=0 && $realtime-busy_since>4*CONTROL_PERIOD)
            $fatal(1,"quiescent while machine side has been busy %0.2f ns",$realtime-busy_since);
        // Drain is a stop proof: the request must have opened a complete
        // control window and been held in the machine domain.
        if(quiescent && (control_request_since<0 || $realtime-control_request_since<15*CONTROL_PERIOD-0.01))
            $fatal(1,"quiescent without a complete window after the control request");
        if(quiescent && (core_request_since<0 || $realtime-core_request_since<STOP_HOLD*CORE_PERIOD))
            $fatal(1,"quiescent before the machine-domain stop was held");
        // Update after the checks: the quiescent level sampled on this edge
        // was computed from the request sampled on the previous edge.
        if(!control_reset_request)control_request_since=-1;
        else if(control_request_since<0)control_request_since=$realtime;
    end

    // ---------------- Requester tasks -------------------------------------
    reg [63:0] shadow[0:WORDS-1];
    integer bench_seed=32'h51ed;
    // Raise a request and drop it once accepted; do not wait for responses.
    task automatic launch(input bit write_op,input bit burst,input [23:0] addr,input [63:0] value,input [7:0] strobe);
        integer n;
        begin
            @(negedge core_clk);
            core_memory_addr=addr;core_memory_wdata=value;core_memory_wstrb=strobe;
            core_memory_write=write_op;core_memory_burst=burst;core_memory_valid=1;
            n=0;while(!core_memory_ready && n<1000)begin @(negedge core_clk);n=n+1;end
            if(n==1000)$fatal(1,"request timeout");
            @(negedge core_clk);core_memory_valid=0;core_memory_burst=0;
            core_memory_addr=$random(bench_seed);core_memory_wdata=$random(bench_seed);
        end
    endtask
    task automatic issue(input bit write_op,input bit burst,input [23:0] addr,input [63:0] value,input [7:0] strobe);
        integer n;
        begin
            launch(write_op,burst,addr,value,strobe);
            n=0;while(outstanding && n<2000)begin @(negedge core_clk);n=n+1;end
            if(n==2000)$fatal(1,"response timeout: %0d of %0d",response_count,expected_beats);
        end
    endtask
    task automatic write_word(input [23:0] addr,input [63:0] value,input [7:0] strobe);
        integer b_before;
        begin
            b_before=b_handshakes;
            issue(1,0,addr,value,strobe);
            if(b_handshakes!=b_before+1)$fatal(1,"write did not complete exactly one B");
            for(integer i=0;i<8;i=i+1)if(strobe[i])shadow[addr[10:3]][i*8+:8]=value[i*8+:8];
        end
    endtask
    task automatic read_word(input [23:0] addr);
        begin
            issue(0,0,addr,0,0);
            if(response_count!=1)$fatal(1,"single read returned %0d beats",response_count);
            if(response_data[0]!==shadow[addr[10:3]])$fatal(1,"read %h returned %h expected %h",addr,response_data[0],shadow[addr[10:3]]);
        end
    endtask
    task automatic read_line(input [23:0] addr);
        begin
            issue(0,1,addr,0,0);
            if(response_count!=4)$fatal(1,"burst read returned %0d beats",response_count);
            for(integer i=0;i<4;i=i+1)
                if(response_data[i]!==shadow[{addr[10:5],2'b00}+i])
                    $fatal(1,"burst %h beat %0d returned %h expected %h",addr,i,response_data[i],shadow[{addr[10:5],2'b00}+i]);
        end
    endtask
    task automatic check_ram;
        for(integer i=0;i<WORDS;i=i+1)if(ram[i]!==shadow[i])$fatal(1,"DDR word %0d is %h expected %h",i,ram[i],shadow[i]);
    endtask
    // Firmware sets the control-domain request; its synchronized copy
    // reaches the machine domain core_delay machine edges later.
    task automatic stop_machine(input integer core_delay);
        begin
            @(negedge control_clk);control_reset_request=1;
            repeat(core_delay)@(negedge core_clk);
            core_reset_request=1;
        end
    endtask
    task automatic run_machine;
        begin
            @(negedge control_clk);control_reset_request=0;
            @(negedge control_clk);
            if(quiescent)$fatal(1,"quiescent after the request was released");
            repeat(2)@(negedge core_clk);core_reset_request=0;
        end
    endtask
    task automatic wait_quiescent(input integer limit);
        integer n;
        begin
            n=0;while(!quiescent && n<limit)begin @(negedge control_clk);n=n+1;end
            if(!quiescent)$fatal(1,"drain did not complete");
        end
    endtask
    // A complete firmware drain: request, wait for drain, release.
    task automatic drain_cycle;
        begin
            stop_machine(3);
            wait_quiescent(200);
            run_machine;
        end
    endtask
    task automatic machine_reset;
        begin
            @(negedge core_clk);core_resetn=0;
            repeat(4)@(negedge core_clk);
            if(quiescent)$fatal(1,"machine reset reported drained");
            core_resetn=1;
            if(core_memory_fault)$fatal(1,"fault survived machine reset");
            drain_cycle;
        end
    endtask
    task automatic wait_idle(input integer limit);
        integer n;
        begin
            n=0;while(!core_memory_ready && n<limit)begin @(negedge core_clk);n=n+1;end
            if(!core_memory_ready)$fatal(1,"DUT did not return to idle");
        end
    endtask

    integer reads_single=0,reads_burst=0,writes_done=0,before_r,n;
    integer before_ar,before_aw,before_w,before_b,before_pulses,window_cycles,orphans=0;
    reg [23:0] a;
    initial begin
        for(integer i=0;i<WORDS;i=i+1)begin
            ram[i]={$random(bench_seed),$random(bench_seed)};shadow[i]=ram[i];
        end
        repeat(5)@(negedge core_clk);control_resetn=1;
        repeat(3)@(negedge core_clk);
        if(quiescent)$fatal(1,"machine reset reported drained");
        core_resetn=1;check_quiet=1;
        // A running idle machine is never reported drained.
        repeat(100)@(negedge control_clk);
        if(quiescent)$fatal(1,"drained without a core reset request");
        drain_cycle;
        if(!rready || !bready)$fatal(1,"responses not ready");

        // Request window: firmware has set the request but the machine has
        // not seen it. With an idle requester the old design could report
        // drain here. Hold the machine copy back far longer than the 15-cycle
        // control window; drain must still wait for the machine-domain stop.
        repeat(50)@(negedge control_clk);
        @(negedge control_clk);control_reset_request=1;
        for(integer i=0;i<60;i=i+1)begin
            @(negedge control_clk);
            if(quiescent)$fatal(1,"quiescent before the request reached the machine (cycle %0d)",i);
        end
        @(negedge core_clk);core_reset_request=1;
        // A native write still commits on the edge after the request
        // arrives: its posted copy must hold the drain.
        repeat(2)@(negedge core_clk);core_memory_busy=1;
        repeat(6)@(negedge core_clk);core_memory_busy=0;
        window_cycles=0;
        while(!quiescent && window_cycles<200)begin @(negedge control_clk);window_cycles=window_cycles+1;end
        if(!quiescent)$fatal(1,"drain did not complete after the late write");
        // Releasing the request clears drain on the next control edge, and a
        // quick re-request must again wait for a full machine-side stop.
        @(negedge control_clk);control_reset_request=0;
        @(negedge control_clk);if(quiescent)$fatal(1,"drain held after release");
        control_reset_request=1;
        repeat(3)@(negedge control_clk);
        if(quiescent)$fatal(1,"drain inside the re-request window");
        wait_quiescent(200);
        run_machine;

        // The production synchronizer delay (2-3 machine edges): drain is
        // still never reported inside 15 control cycles of the request.
        stop_machine(3);
        for(integer i=0;i<15;i=i+1)begin
            @(negedge control_clk);
            if(quiescent)$fatal(1,"quiescent %0d control cycles after the request",i);
        end
        wait_quiescent(200);

        // A busy decode pulse that spans a control edge but ends before the
        // next machine edge is never sampled: the registered source hides it.
        @(negedge core_clk);#0.5;core_memory_busy=1;#7.6;core_memory_busy=0;
        repeat(6)@(negedge control_clk);
        if(!quiescent)$fatal(1,"unsampled busy glitch interrupted complete drain");
        run_machine;

        // Directed: ROM aperture, strobe lanes, single read and line burst.
        write_word(24'hfc0100,64'h8877665544332211,8'hff);
        write_word(24'hfc0100,64'h0000000000aa0000,8'h04);
        read_word(24'hfc0100);
        if(response_data[0]!==64'h8877665544aa2211)$fatal(1,"strobed write merged wrongly");
        read_line(24'hfc0100);
        read_line(24'h00071f);
        read_word(24'h00071f);

        // Posted writes in the requester hold the drain even when this path
        // is idle and the machine is stopped; release must still wait for
        // the full quiet window.
        stop_machine(3);
        @(negedge core_clk);core_memory_busy=1;
        repeat(200)@(negedge core_clk);
        if(quiescent)$fatal(1,"drained with posted writes");
        write_word(24'h000208,64'h0123456789abcdef,8'hf0);
        repeat(20)@(negedge core_clk);
        if(quiescent)$fatal(1,"drained with posted writes");
        core_memory_busy=0;
        wait_quiescent(100);
        run_machine;

        // Random traffic, random handshake stalls and response gaps.
        for(integer t=0;t<600;t=t+1)begin
            stall_percent=(t/150)*25;      // 0, 25, 50 and 75 percent
            a={$random(bench_seed)}%(WORDS*8);
            if(t%2)a[23:16]=8'hfc;
            case($unsigned($random(bench_seed))%3)
                0:begin write_word(a,{$random(bench_seed),$random(bench_seed)},$random(bench_seed));writes_done=writes_done+1;end
                1:begin read_word(a);reads_single=reads_single+1;end
                default:begin read_line(a);reads_burst=reads_burst+1;end
            endcase
            repeat($unsigned($random(bench_seed))%3)@(negedge core_clk);
        end
        stall_percent=50;
        check_ram;
        drain_cycle;
        if(core_memory_fault)$fatal(1,"fault without an AXI error");

        // Faults: each is sticky, the requester still drains, and the next
        // transaction still completes until machine reset clears the fault.
        inject_rresp=1;issue(0,0,24'h000040,0,0);
        if(!core_memory_fault || response_count!=1)$fatal(1,"RRESP error not surfaced");
        read_line(24'h000040);drain_cycle;
        if(!core_memory_fault)$fatal(1,"fault not sticky");
        machine_reset;

        inject_bresp=1;issue(1,0,24'h000048,64'h1111111111111111,8'h01);
        shadow[9][7:0]=8'h11;
        if(!core_memory_fault || response_count!=1)$fatal(1,"BRESP error not surfaced");
        drain_cycle;
        machine_reset;

        inject_missing_rlast=1;issue(0,1,24'h000060,0,0);
        if(!core_memory_fault || response_count!=4)$fatal(1,"missing RLAST not surfaced");
        drain_cycle;
        machine_reset;

        before_r=r_handshakes;
        inject_early_rlast=1;issue(0,1,24'h000080,0,0);
        if(!core_memory_fault || response_count!=4 || r_handshakes!=before_r+2)$fatal(1,"early RLAST not surfaced/padded");
        if(response_data[2]!==0 || response_data[3]!==0)$fatal(1,"padded beats not zero");
        drain_cycle;
        read_word(24'h000080);
        machine_reset;
        read_word(24'h000088);
        if(core_memory_fault)$fatal(1,"fault after clean read");

        // ---------- Machine reset in the middle of an AXI transaction -----
        stall_percent=0;
        // 1. Reset with AW and W both waiting for ready, released while W is
        // still waiting. Both valids must hold through reset; the orphan's B
        // is absorbed; ready and drain wait for it; no requester pulse.
        before_aw=aw_handshakes;before_w=w_handshakes;before_b=b_handshakes;before_pulses=pulses;
        hold_aw=1;hold_w=1;
        launch(1,0,24'h000100,64'hc0ffee00c0ffee00,8'hff);
        repeat(4)@(negedge core_clk);
        if(!awvalid || !wvalid)$fatal(1,"write valids not raised");
        core_resetn=0;control_reset_request=1;core_reset_request=1;
        repeat(5)@(negedge core_clk);
        if(!awvalid || !wvalid)$fatal(1,"machine reset dropped AWVALID/WVALID");
        hold_aw=0;
        n=0;while(aw_handshakes==before_aw && n<100)begin @(negedge core_clk);n=n+1;end
        if(aw_handshakes!=before_aw+1)$fatal(1,"orphan AW did not complete");
        @(negedge core_clk);core_resetn=1;
        repeat(10)@(negedge core_clk);
        if(core_memory_ready)$fatal(1,"ready while an orphan write waits for W");
        if(!wvalid)$fatal(1,"orphan WVALID dropped after reset release");
        repeat(40)@(negedge control_clk);
        if(quiescent)$fatal(1,"drained with an orphan write in flight");
        hold_w=0;
        wait_idle(200);
        if(w_handshakes!=before_w+1 || b_handshakes!=before_b+1)$fatal(1,"orphan write W/B count");
        if(pulses!=before_pulses)$fatal(1,"orphan write returned a requester pulse");
        if(core_memory_fault)$fatal(1,"orphan write set a fault");
        shadow[32]=64'hc0ffee00c0ffee00;   // the AFI completed it
        wait_quiescent(200);
        run_machine;orphans=orphans+1;
        // Later writes pair with their own address and data.
        write_word(24'h000108,64'h0102030405060708,8'hff);
        write_word(24'h000100,64'h00000000000000a5,8'h01);
        read_word(24'h000100);read_word(24'h000108);
        check_ram;

        // 2. Reset after AW completed but before W, with B held past the
        // reset release; an injected BRESP error on the orphan is ignored.
        before_aw=aw_handshakes;before_w=w_handshakes;before_b=b_handshakes;before_pulses=pulses;
        hold_w=1;hold_b=1;inject_bresp=1;
        launch(1,0,24'h000110,64'h5a5a5a5a5a5a5a5a,8'h0f);
        n=0;while(aw_handshakes==before_aw && n<100)begin @(negedge core_clk);n=n+1;end
        @(negedge core_clk);core_resetn=0;
        repeat(3)@(negedge core_clk);
        if(!wvalid)$fatal(1,"machine reset dropped WVALID");
        hold_w=0;
        n=0;while(w_handshakes==before_w && n<100)begin @(negedge core_clk);n=n+1;end
        repeat(2)@(negedge core_clk);core_resetn=1;
        repeat(10)@(negedge core_clk);
        if(core_memory_ready)$fatal(1,"ready while an orphan write waits for B");
        hold_b=0;
        wait_idle(200);
        if(aw_handshakes!=before_aw+1 || w_handshakes!=before_w+1 || b_handshakes!=before_b+1)
            $fatal(1,"orphan write AW/W/B count");
        if(pulses!=before_pulses || core_memory_fault)$fatal(1,"orphan write pulse or fault");
        shadow[34][31:0]=32'h5a5a5a5a;
        orphans=orphans+1;
        write_word(24'h000118,64'hfedcba9876543210,8'hff);
        read_word(24'h000110);read_word(24'h000118);
        check_ram;

        // 3. Reset in the middle of a line burst, released before the last
        // beats arrive: every beat is absorbed and nothing reaches the
        // requester.
        before_r=r_handshakes;before_ar=ar_handshakes;before_pulses=pulses;
        launch(0,1,24'h000140,0,0);
        n=0;while(r_handshakes<before_r+1 && n<200)begin @(negedge core_clk);n=n+1;end
        hold_r=1;
        @(negedge core_clk);core_resetn=0;
        repeat(3)@(negedge core_clk);core_resetn=1;
        repeat(10)@(negedge core_clk);
        if(core_memory_ready)$fatal(1,"ready while orphan burst beats remain");
        hold_r=0;
        wait_idle(200);
        if(ar_handshakes!=before_ar+1 || r_handshakes!=before_r+4)$fatal(1,"orphan burst AR/R count %0d",r_handshakes-before_r);
        if(pulses!=before_pulses+1 || core_memory_fault)$fatal(1,"orphan burst pulse or fault");
        orphans=orphans+1;
        read_line(24'h000140);read_word(24'h000148);

        // 4. Reset while AR waits for ready: ARVALID holds, the single beat is
        // absorbed after reset release.
        before_r=r_handshakes;before_ar=ar_handshakes;before_pulses=pulses;
        hold_ar=1;
        launch(0,0,24'h000150,0,0);
        repeat(3)@(negedge core_clk);
        core_resetn=0;
        repeat(3)@(negedge core_clk);
        if(!arvalid)$fatal(1,"machine reset dropped ARVALID");
        core_resetn=1;
        repeat(5)@(negedge core_clk);
        if(core_memory_ready || !arvalid)$fatal(1,"orphan AR abandoned after reset release");
        hold_ar=0;
        wait_idle(200);
        if(ar_handshakes!=before_ar+1 || r_handshakes!=before_r+1 || pulses!=before_pulses)$fatal(1,"orphan read count or pulse");
        orphans=orphans+1;

        // 5. Orphan burst ended early by RLAST: it ends there, with no pad
        // pulses and no fault.
        before_r=r_handshakes;before_pulses=pulses;
        inject_early_rlast=1;hold_r=1;
        launch(0,1,24'h000160,0,0);
        repeat(4)@(negedge core_clk);
        core_resetn=0;repeat(2)@(negedge core_clk);core_resetn=1;
        hold_r=0;
        wait_idle(200);
        if(r_handshakes!=before_r+2 || pulses!=before_pulses || core_memory_fault)$fatal(1,"early-RLAST orphan");
        orphans=orphans+1;
        read_line(24'h000160);
        drain_cycle;
        check_ram;
        stall_percent=50;
        for(integer t=0;t<60;t=t+1)begin
            a={$random(bench_seed)}%(WORDS*8);
            case($unsigned($random(bench_seed))%3)
                0:write_word(a,{$random(bench_seed),$random(bench_seed)},$random(bench_seed));
                1:read_word(a);
                default:read_line(a);
            endcase
        end
        check_ram;
        drain_cycle;
        if(core_memory_fault)$fatal(1,"fault after orphan recovery");

        $display("PASS: IIgs DDR AXI3 master on the machine clock: %0d random transactions (%0d single reads, %0d line bursts, %0d strobed writes), AXI AR=%0d R=%0d AW=%0d W=%0d B=%0d, one outstanding, random ready/valid stalls, RRESP/BRESP/RLAST faults with full response counts, posted-write busy, registered busy glitch suppression, drain only after the core reset request reached the machine (late-write window %0d control cycles), and %0d machine resets mid-AW/W/B/AR/burst finished on AXI without a pulse",
            reads_single+reads_burst+writes_done,reads_single,reads_burst,writes_done,
            ar_handshakes,r_handshakes,aw_handshakes,w_handshakes,b_handshakes,window_cycles,orphans);
        $finish;
    end
    initial begin #20000000;$fatal(1,"timeout");end
endmodule
