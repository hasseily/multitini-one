// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
// Test-only native-machine interface stub for the production input FIFO.
module multitini_amiga_machine (
    input wire clk_service28,
    input wire reset,
    input wire guest_reset,
    input wire run_enable,
    output wire clk_chip,
    output wire chip_tick,
    output wire chip_c1,
    output wire chip_c3,
    output wire chip_clk7_en,
    output wire chip_clk7n_en,
    output wire [23:1] chip_addr,
    output wire [15:0] chip_wdata,
    output wire chip_oe_n,chip_we_n,chip_ub_n,chip_lb_n,
    output wire chip_long,
    output wire [15:0] chip_wdata2,
    input wire [15:0] chip_rdata,
    input wire [15:0] chip_rdata2,
    input wire [47:0] chip_rdata48,
    output wire cpu_req,
    output wire [28:1] cpu_addr,
    output wire [15:0] cpu_wdata,
    output wire [1:0] cpu_state,
    output wire cpu_ub_n,cpu_lb_n,
    output wire cpu_long,
    output wire [15:0] cpu_wdata2,
    input wire [15:0] cpu_rdata,
    input wire [15:0] cpu_rdata2,
    input wire cpu_ready,
    input wire cpu_ready_long,
    input wire io_uio,io_fpga,io_strobe,
    input wire [15:0] io_din,
    output wire io_wait,
    output wire [15:0] io_dout,
    output wire [5:0] ide_req,
    input wire [4:0] ide_address,
    input wire ide_write,ide_read,
    input wire [15:0] ide_wdata,
    output wire [15:0] ide_rdata,
    input wire [15:0] joy1,joy2,
    input wire [2:0] mouse_buttons,
    input wire kms_level,
    input wire [1:0] kbd_mouse_type,
    input wire [7:0] kbd_mouse_data,
    input wire [64:0] rtc,
    output wire [23:0] rgb,
    output wire hsync_n,vsync_n,hblank,vblank,field,lace,ce_pix,
    output wire [1:0] res,
    output wire ntsc,
    output wire signed [14:0] audio_l,audio_r,
    output wire cpu_reset_n,
    output wire [4:0] cpucfg,
    output wire [6:0] memcfg,
    output wire fdd_led,hdd_led
);
    // Test-only native machine. The production adapter, queue, loader,
    // memory and host bridge remain unchanged in this fixture.
    reg fixture_cpu_ready=0,fixture_stall=0;
    reg fixture_enable=1;
    always @(negedge clk_service28)fixture_enable<=(run_enable || reset) && !fixture_stall;
    assign #0.1 clk_chip=clk_service28 && fixture_enable;
    assign chip_tick=!reset && fixture_enable;
    assign cpu_reset_n=fixture_cpu_ready && !guest_reset && !reset;
    assign chip_c1=0,chip_c3=0,chip_clk7_en=0,chip_clk7n_en=0;
    assign chip_addr=0,chip_wdata=0,chip_wdata2=0,chip_oe_n=1,chip_we_n=1,chip_ub_n=1,chip_lb_n=1,chip_long=0;
    assign cpu_req=0,cpu_addr=0,cpu_wdata=0,cpu_state=1,cpu_ub_n=1,cpu_lb_n=1,cpu_long=0,cpu_wdata2=0;
    assign io_wait=0,io_dout=0,ide_req=0,ide_rdata=0;
    assign rgb=0,hsync_n=1,vsync_n=1,hblank=1,vblank=1,field=0,lace=0,ce_pix=0,res=0,ntsc=0;
    assign audio_l=0,audio_r=0,cpucfg=0,memcfg=0,fdd_led=0,hdd_led=0;
endmodule

`timescale 1ns/1ps
module tb_amiga_adapter_input;
    reg core_clk=0,control_clk=0,reset=1,core_reset_request=0;
    always #5 core_clk=~core_clk;
    always #7 control_clk=~control_clk;
    reg [15:0] input_code=0,input_value=0;
    reg input_valid=0;
    wire input_ready,ready;
    multitini_amiga_adapter dut(.core_clk(core_clk),.reset(reset),.core_reset_request(core_reset_request),
        .pause_request(1'b0),.clear_load_error(1'b0),.control_clk(control_clk),.control_resetn(!reset),
        .host_write(1'b0),.host_command(32'd0),.host_status(),
        .input_code(input_code),.input_value(input_value),.input_valid(input_valid),.input_ready(input_ready),
        .load_data(8'd0),.load_kind(8'd0),.load_valid(1'b0),.load_first(1'b0),.load_last(1'b0),
        .load_epoch(1'b0),.load_abort(1'b0),.load_abort_epoch(1'b0),.load_offset(32'd0),.load_total(32'd0),.load_ready(),
        .memory_addr(),.memory_write(),.memory_wdata(),.memory_wstrb(),.memory_valid(),.memory_busy(),
        .memory_ready(1'b1),.memory_rdata(64'd0),.memory_rvalid(1'b0),.memory_fault(1'b0),
        .core_id(),.ready(ready),.load_error(),.rom_loaded(),.video_r(),.video_g(),.video_b(),
        .video_de(),.video_hsync(),.video_vsync(),.video_valid(),.video_frame_start(),.video_x(),.video_y(),.video_pal(),
        .audio_l(),.audio_r(),.audio_valid());
    integer native_edges=0,event_count=0;
    integer event_edge[0:127];
    reg [1:0] event_type[0:127];
    reg [7:0] event_data[0:127];
    reg seen_kms=0;
    always @(posedge dut.chip_clk)begin
        native_edges=native_edges+1;
        if(reset || !dut.cpu_reset_n)seen_kms=dut.amiga_machine.kms_level;
        else if(seen_kms!=dut.amiga_machine.kms_level)begin
            event_edge[event_count]=native_edges;event_type[event_count]=dut.amiga_machine.kbd_mouse_type;
            event_data[event_count]=dut.amiga_machine.kbd_mouse_data;event_count=event_count+1;seen_kms=dut.amiga_machine.kms_level;
        end
    end
    task tick;begin @(posedge core_clk);#1;end endtask
    task input_event(input [15:0] code,value);
        integer timeout;
        begin
            @(negedge core_clk);timeout=0;
            while(!input_ready && timeout<1000)begin @(negedge core_clk);timeout=timeout+1;end
            if(timeout==1000)$fatal(1,"input event refused unexpectedly");
            input_code=code;input_value=value;input_valid=1;
            @(negedge core_clk);input_valid=0;
        end
    endtask
    task wait_events(input integer target);
        integer timeout;
        begin timeout=0;while(event_count<target && timeout<1200000)begin tick;timeout=timeout+1;end
            if(timeout==1200000)$fatal(1,"native queued input timed out target%0d got%0d",target,event_count);end
    endtask
    integer saved_edges,saved_events,i;
    initial begin
        repeat(6)tick;@(negedge core_clk);reset=0;
        // ROM/config framing is independently exercised across full streams.
        // This fixture isolates input delivery after the native reset delay.
        force dut.loader.rom_loaded=1'b1;
        force dut.loader.config_valid=1'b1;
        force dut.loader.config_bytes=128'h00000000000002080000000031304d41;
        input_event(16'h6000,16'h0020);input_event(16'h6000,16'h00a0);
        input_event(16'h6100,16'h00f0);input_event(16'h6101,16'h0011);
        repeat(40)tick;
        if(ready || event_count || dut.input_count!=4)$fatal(1,"native reset consumed queued input");
        dut.amiga_machine.fixture_cpu_ready=1;wait_events(1);
        if(!ready)$fatal(1,"native reset release did not publish readiness");
        repeat(10)tick;dut.amiga_machine.fixture_stall=1;repeat(5)tick;saved_edges=native_edges;
        repeat(1000)tick;
        if(native_edges!=saved_edges || event_count!=1)$fatal(1,"paused native time advanced keyboard pacing");
        dut.amiga_machine.fixture_stall=0;wait_events(4);
        if(event_type[0]!=2 || event_data[0]!=8'h20 || event_type[1]!=2 || event_data[1]!=8'ha0 ||
           event_type[2]!=0 || event_data[2]!=8'hf0 || event_type[3]!=1 || event_data[3]!=8'h11)
            $fatal(1,"queued keyboard/mouse ordering or data changed");
        if(event_edge[1]-event_edge[0]<227002 || event_edge[1]-event_edge[0]>227006)
            $fatal(1,"keyboard spacing differs from8ms native time:%0d",event_edge[1]-event_edge[0]);
        if(event_edge[2]-event_edge[1]<4 || event_edge[3]-event_edge[2]<4)
            $fatal(1,"mouse data did not retain four native samples");
        // Old pending input is flushed once at reset entry. New USB events
        // accepted during the reset interval must survive until guest release.
        input_event(16'h6000,16'h0031);input_event(16'h6100,16'h0032);
        @(negedge core_clk);core_reset_request=1;dut.amiga_machine.fixture_cpu_ready=0;
        repeat(8)tick;saved_events=event_count;
        if(dut.input_count || dut.joy1!=16'hffff || dut.mouse_buttons)$fatal(1,"guest reset retained stale input");
        input_event(16'h6000,16'h0044);repeat(10)tick;
        if(dut.input_count!=1 || event_count!=saved_events)$fatal(1,"reset interval discarded new input");
        @(negedge core_clk);core_reset_request=0;repeat(20)tick;
        if(ready || event_count!=saved_events)$fatal(1,"native reset delay released input early");
        dut.amiga_machine.fixture_cpu_ready=1;wait_events(saved_events+1);
        if(event_data[saved_events]!=8'h44 || dut.input_count)$fatal(1,"reset replayed stale input");
        // Capacity is exactly64 accepted events, with no write when ready is
        // low. Fast joystick levels make the capacity check independent of
        // the separately tested keyboard pacing interval.
        @(negedge core_clk);reset=1;dut.amiga_machine.fixture_cpu_ready=0;
        repeat(5)tick;@(negedge core_clk);reset=0;
        for(i=0;i<64;i=i+1)input_event(16'h6200,i);
        if(input_ready || dut.input_count!=64)$fatal(1,"input queue capacity/backpressure");
        @(negedge core_clk);input_code=16'h6200;input_value=5;input_valid=1;
        repeat(20)tick;
        if(input_ready || dut.input_count!=64)$fatal(1,"full queue accepted unacknowledged event");
        @(negedge core_clk);input_valid=0;dut.amiga_machine.fixture_cpu_ready=1;
        repeat(100)tick;
        if(dut.input_count || !input_ready || dut.joy1!=16'hffc0)$fatal(1,"full queue lost ordering/final joystick level:%h",dut.joy1);
        $display("PASS AMIGA adapter input FIFO:native reset retention,ordered keyboard/mouse,8ms native pacing,4edge hold,stale reset flush,64event backpressure");
        $finish;
    end
    initial begin #30000000;$fatal(1,"adapter input watchdog");end
endmodule
