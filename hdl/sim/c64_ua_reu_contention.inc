// SPDX-License-Identifier: GPL-3.0-only
// Inserted into the existing native REU bench by test_c64_ua_contention.py.
// One synthetic DDRC service resource serializes HP2 and HP3. A waiting HP2
// request wins admission; a previously admitted HP3 read must drain first.
    wire [83:0] ua_pending_addr;wire [3:0] ua_pending_valid;
    wire [20:0] ua_write_addr,ua_done_addr,ua_snoop_addr;
    wire ua_write_valid,ua_write_done,ua_snoop_valid,reu_waiting;
    wire hp2_arready,hp2_awready,hp2_wready;
    wire [23:0] ua_address;wire ua_valid,ua_ready,ua_rvalid,ua_busy,ua_fault,ua_quiescent;
    wire [63:0] ua_data;wire [31:0] ua_status,ua_fetch_count;
    wire [7:0] ua_reg_data;wire ua_irq,ua_held;wire signed [17:0] ua_left,ua_right;
    multitini_c64_ua #(.CORE_CLOCK_HZ(NTSC ? 32727266 : 31527954)) ua(
        .clk(clk),.hard_reset(hard_reset),.guest_reset(reset),.enable(UA_TRAFFIC!=0),.paused(1'b0),.hold(1'b0),
        .reg_write(1'b0),.reg_addr(8'b0),.reg_wdata(8'b0),.reg_rdata(ua_reg_data),
        .diagnostics_armed(1'b1),.audio_tick(1'b0),.board_clip(1'b0),
        .cache_pending_addr(ua_pending_addr),.cache_pending_valid(ua_pending_valid),
        .cache_inflight_write_addr(ua_write_addr),.cache_inflight_write_valid(ua_write_valid),
        .cache_write_done(ua_write_done),.cache_write_done_addr(ua_done_addr),
        .cache_write_snoop_valid(ua_snoop_valid),.cache_write_snoop_addr(ua_snoop_addr),.reu_memory_busy(reu_waiting),
        .irq(ua_irq),.held(ua_held),.status(ua_status),.fetch_count(ua_fetch_count),.audio_l(ua_left),.audio_r(ua_right),
        .memory_addr(ua_address),.memory_valid(ua_valid),.memory_busy(ua_busy),.memory_ready(ua_ready),
        .memory_rdata(ua_data),.memory_rvalid(ua_rvalid),.memory_fault(ua_fault));
    // Directed background setup is separate from register/decode tests.
    always @(negedge clk)if(UA_TRAFFIC && !reset && !hard_reset)begin
        for(integer u=0;u<7;u=u+1)begin
            ua.control[u]=8'h53;ua.rate[u]=0;ua.start_address[u]=24'he00000+u*4096;
            ua.sample_length[u]=4096;ua.repeat_a[u]=0;ua.repeat_b[u]=4096;
        end
    end
    wire [31:0] ua_araddr,ua_awaddr;wire [3:0] ua_arlen,ua_awlen;
    wire [2:0] ua_arsize,ua_awsize;wire [1:0] ua_arburst,ua_awburst;
    wire ua_arvalid,ua_rready,ua_awvalid,ua_wvalid,ua_wlast,ua_bready;
    wire [63:0] ua_wdata;wire [7:0] ua_wstrb;
    reg ua_axi_rvalid=0;reg [63:0] ua_axi_rdata=0;
    reg ua_read_active=0;reg [23:0] ua_read_address=0;integer ua_delay=0;
    wire ua_arready=!hard_reset && !ua_read_active && !read_active && !have_aw && !have_w && !bvalid &&
        !arvalid && !awvalid && !wvalid;
    assign hp2_arready=arready && !ua_read_active;
    assign hp2_awready=awready && !ua_read_active;
    assign hp2_wready=wready && !ua_read_active;
    multitini_c64_ddr_path ua_bridge(
        .core_clk(clk),.core_resetn(!hard_reset),.control_clk(clk),.control_resetn(!hard_reset),
        .control_reset_request(reset),.core_reset_request(reset),
        .core_memory_addr(ua_address),.core_memory_write(1'b0),.core_memory_wdata(64'b0),.core_memory_wstrb(8'b0),
        .core_memory_valid(ua_valid),.core_memory_burst(1'b0),.core_memory_busy(ua_busy),
        .core_memory_ready(ua_ready),.core_memory_rdata(ua_data),.core_memory_rvalid(ua_rvalid),
        .core_memory_fault(ua_fault),.quiescent(ua_quiescent),
        .araddr(ua_araddr),.arlen(ua_arlen),.arsize(ua_arsize),.arburst(ua_arburst),.arvalid(ua_arvalid),.arready(ua_arready),
        .rdata(ua_axi_rdata),.rresp(2'b0),.rlast(1'b1),.rvalid(ua_axi_rvalid),.rready(ua_rready),
        .awaddr(ua_awaddr),.awlen(ua_awlen),.awsize(ua_awsize),.awburst(ua_awburst),.awvalid(ua_awvalid),.awready(1'b0),
        .wdata(ua_wdata),.wstrb(ua_wstrb),.wlast(ua_wlast),.wvalid(ua_wvalid),.wready(1'b0),
        .bresp(2'b0),.bvalid(1'b0),.bready(ua_bready));
    integer ua_accepted_total=0,ua_replies_total=0,ua_blocked_hp2=0,ua_playing_cycles=0;
    always @(posedge clk)begin
        if(hard_reset)begin ua_read_active<=0;ua_axi_rvalid<=0;end
        else begin
            if(ua_fault)$fatal(1,"Unexpected UA fault in shared DDRC");
            if(ua_status[14:8]==7'h7f)ua_playing_cycles<=ua_playing_cycles+1;
            if(ua_read_active && (arvalid||awvalid||wvalid))ua_blocked_hp2<=ua_blocked_hp2+1;
            if(ua_arvalid && ua_arready)begin
                if(ua_araddr[31:24]!=8'h25 || ua_araddr[2:0]!=0 || ua_arlen!=0 || ua_arsize!=3 || ua_arburst!=1)
                    $fatal(1,"UA HP3 aperture or burst");
                if(read_active || have_aw || have_w || bvalid)$fatal(1,"DDRC simultaneous owners");
                ua_read_active<=1;ua_read_address<=ua_araddr[23:0];ua_delay<=6;
                ua_accepted_total<=ua_accepted_total+1;
            end
            if(ua_read_active && !ua_axi_rvalid)begin
                if(ua_delay>0)ua_delay<=ua_delay-1;
                else begin ua_axi_rdata<=ddr_word(ua_read_address);ua_axi_rvalid<=1;end
            end
            if(ua_axi_rvalid && ua_rready)begin
                ua_axi_rvalid<=0;ua_read_active<=0;ua_replies_total<=ua_replies_total+1;
            end
        end
    end
