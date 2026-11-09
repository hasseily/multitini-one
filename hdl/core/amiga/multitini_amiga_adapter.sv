// SPDX-License-Identifier: GPL-3.0-only
`timescale 1ns/1ps
module multitini_amiga_adapter #(parameter bit AGA_PAL=1'b0, USE_AP68040=1'b0) (
    input wire core_clk,cpu_clk2x,reset,core_reset_request,pause_request,clear_load_error,
    input wire control_clk,control_resetn,host_write,
    input wire host_block_write,host_block_read,
    input wire [15:0] host_block_address,host_block_read_address,
    input wire [6:0] host_block_ram_read_address,
    input wire [3:0] host_block_wstrb,
    output wire [31:0] host_block_rdata,
    input wire video_quiescent,
    output wire video_switch_pending,
    output wire [1:0] video_capture_mode,
    input wire [31:0] host_command,output wire [31:0] host_status,
    input wire [15:0] input_code,input_value,input wire input_valid,output wire input_ready,
    input wire [7:0] load_data,load_kind,
    input wire load_valid,load_first,load_last,load_epoch,load_abort,load_abort_epoch,
    input wire [31:0] load_offset,load_total,output wire load_ready,
    output wire [24:0] memory_addr,output wire memory_write,
    output wire [63:0] memory_wdata,output wire [7:0] memory_wstrb,
    output wire memory_burst,memory_valid,memory_busy,input wire memory_ready,
    input wire [63:0] memory_rdata,input wire memory_rvalid,memory_fault,
    output wire [31:0] core_id,output wire ready,load_error,rom_loaded,
    output wire [7:0] video_r,video_g,video_b,
    output wire video_de,video_hsync,video_vsync,video_valid,video_frame_start,
    output wire video_frame_alt,video_frame_weave,
    output wire [1:0] video_frame_profile,
    output wire [11:0] video_x,video_y,output wire video_pal,
    output reg signed [23:0] audio_l,audio_r,output reg audio_valid
);
    wire chip_clk,chip_tick,chip_c1,chip_c3,chip_clk7_en,chip_clk7n_en;
    reg chip_tick_q;
    // Publish root-domain changes after the derived edge consumed them.
    always @(posedge core_clk)chip_tick_q<=!reset && chip_tick;
    wire [23:1] chip_addr;
    wire [15:0] chip_wdata,chip_wdata2,chip_rdata,chip_rdata2;
    wire [47:0] chip_rdata48;
    wire chip_oe_n,chip_we_n,chip_ub_n,chip_lb_n,chip_long;
    wire cpu_req,cpu_ub_n,cpu_lb_n,cpu_long,cpu_ready,cpu_reset_n;
    wire [28:1] cpu_addr;
    wire [15:0] cpu_wdata,cpu_wdata2,cpu_rdata,cpu_rdata2;
    wire cpu_line,cpu_line_ready;
    wire [127:0] cpu_line_data;
    wire [1:0] cpu_state;
    wire io_uio,io_fpga,io_strobe,io_wait;
    wire [15:0] io_din,io_dout,ide_wdata,ide_rdata;
    wire [5:0] ide_req;
    wire [5:0] ide_monitor;
    wire [7:0] memory_monitor;
    wire [4:0] ide_address;
    wire ide_write,ide_read;
    wire [23:0] rgb;
    wire hsync_n,vsync_n,hblank,vblank,field,lace,ce_pix,ntsc;
    wire [1:0] resolution;
    wire signed [14:0] native_audio_l,native_audio_r;
    wire [4:0] cpucfg;
    wire [6:0] memcfg;
    wire ide_fast;
    wire loader_error,config_valid,write_valid,write_ready,write_done,memory_run,memory_error;
    wire [127:0] config_bytes;
    wire [23:0] write_addr;wire [7:0] write_data;
    wire native_stopped=core_reset_request || !rom_loaded || !config_valid || load_error;
    reg cpu_double_speed=0;
    reg [15:0] joy1,joy2;
    reg [2:0] mouse_buttons;
    reg kms_level;
    reg [1:0] kbd_mouse_type;
    reg [7:0] kbd_mouse_data;
    reg [15:0] native_joy1,native_joy2;
    reg [2:0] native_mouse_buttons;
    reg native_kms_level,native_guest_reset;
    reg [1:0] native_kbd_mouse_type;
    reg [7:0] native_kbd_mouse_data;
    // The complete input/reset bundle settles during the inactive half of
    // the ungated service clock before a native edge may consume it.
    always @(negedge core_clk or posedge reset)begin
        if(reset)begin
            native_joy1<=16'hffff;native_joy2<=16'hffff;native_mouse_buttons<=0;
            native_kms_level<=0;native_kbd_mouse_type<=0;native_kbd_mouse_data<=0;native_guest_reset<=1;
            cpu_double_speed<=0;
        end else begin
            native_joy1<=joy1;native_joy2<=joy2;native_mouse_buttons<=mouse_buttons;
            native_kms_level<=kms_level;native_kbd_mouse_type<=kbd_mouse_type;native_kbd_mouse_data<=kbd_mouse_data;
            native_guest_reset<=native_stopped;
            if(native_guest_reset)cpu_double_speed<=USE_AP68040 && config_bytes[39:32]==8'd4;
        end
    end
    reg [2:0] input_hold;
    // Native CIA keyboard delivery is paced. Queue USB bursts while preserving
    // press/release ordering; mouse events need only a complete native sample.
    reg [31:0] input_queue[0:63];
    reg [5:0] input_head,input_tail;
    reg [6:0] input_count;
    reg [17:0] key_spacing;
    reg guest_reset_q;
    wire flush_input=core_reset_request && !guest_reset_q;
    wire [31:0] next_input=input_queue[input_head];
    wire dispatch=cpu_reset_n && !native_stopped && input_count!=0 && input_hold==0 &&
        (next_input[31:16]!=16'h6000 || key_spacing==0);
    wire enqueue=input_valid && input_ready;
    assign input_ready=!reset && !flush_input && input_count<64;
    always @(posedge core_clk)begin
        if(reset)begin
            joy1<=16'hffff;joy2<=16'hffff;mouse_buttons<=0;kms_level<=0;
            kbd_mouse_type<=0;kbd_mouse_data<=0;input_hold<=0;
            input_head<=0;input_tail<=0;input_count<=0;key_spacing<=0;
            guest_reset_q<=0;
        end else begin
            guest_reset_q<=core_reset_request;
            if(chip_tick_q)begin
                if(input_hold!=0)input_hold<=input_hold-1'b1;
                if(key_spacing!=0)key_spacing<=key_spacing-1'b1;
            end
            case({enqueue,dispatch})
                2'b10:input_count<=input_count+1'b1;
                2'b01:input_count<=input_count-1'b1;
                default:begin end
            endcase
            if(enqueue)begin input_queue[input_tail]<={input_code,input_value};input_tail<=input_tail+1'b1;end
            if(dispatch)begin
                input_head<=input_head+1'b1;
                case(next_input[31:16])
                    16'h6000:begin kms_level<=!kms_level;kbd_mouse_type<=2;kbd_mouse_data<=next_input[7:0];input_hold<=4;key_spacing<=227002;end
                    16'h6100:begin kms_level<=!kms_level;kbd_mouse_type<=0;kbd_mouse_data<=next_input[7:0];input_hold<=4;end
                    16'h6101:begin kms_level<=!kms_level;kbd_mouse_type<=1;kbd_mouse_data<=next_input[7:0];input_hold<=4;end
                    16'h6102:mouse_buttons<=next_input[2:0];
                    // Directions, Red, Blue, Yellow and Green are active high in the input packet.
                    16'h6200:joy1<={8'hff,~next_input[7:0]};
                    16'h6201:joy2<={8'hff,~next_input[7:0]};
                    default:begin end
                endcase
            end
            if(flush_input)begin
                input_head<=0;input_tail<=0;input_count<=0;input_hold<=0;key_spacing<=0;
                joy1<=16'hffff;joy2<=16'hffff;mouse_buttons<=0;kms_level<=0;
            end
        end
    end
    multitini_amiga_loader #(.AGA_PAL(AGA_PAL),.USE_AP68040(USE_AP68040)) loader(
        .clk(core_clk),.reset(reset),.clear_error(clear_load_error),
        .load_abort(load_abort),.load_abort_epoch(load_abort_epoch),.load_epoch(load_epoch),
        .load_kind(load_kind),.load_data(load_data),.load_valid(load_valid),.load_first(load_first),.load_last(load_last),
        .load_offset(load_offset),.load_total(load_total),.load_ready(load_ready),.error(loader_error),
        .rom_loaded(rom_loaded),.config_valid(config_valid),.config_bytes(config_bytes),
        .write_valid(write_valid),.write_addr(write_addr),.write_data(write_data),
        .write_ready(write_ready),.write_done(write_done),.memory_fault(memory_fault));
    multitini_amiga_memory #(.USE_AP68040(USE_AP68040)) memory_i(
        .clk(core_clk),.reset(reset),.chip_c1(chip_c1 && !native_stopped),.chip_addr(chip_addr),
        .chip_wdata(chip_wdata),.chip_wdata2(chip_wdata2),.chip_oe_n(chip_oe_n),.chip_we_n(chip_we_n),
        .chip_ub_n(chip_ub_n),.chip_lb_n(chip_lb_n),.chip_long(chip_long),
        .chip_rdata(chip_rdata),.chip_rdata2(chip_rdata2),.chip_rdata48(chip_rdata48),
        .cpu_req(cpu_req && cpu_reset_n && !native_stopped),.cpu_addr(cpu_addr),.cpu_wdata(cpu_wdata),.cpu_state(cpu_state),
        .cpu_ub_n(cpu_ub_n),.cpu_lb_n(cpu_lb_n),.cpu_long(cpu_long),.cpu_wdata2(cpu_wdata2),
        .cpu_rdata(cpu_rdata),.cpu_rdata2(cpu_rdata2),.cpu_ready(cpu_ready),
        .cpu_line(cpu_line),.cpu_line_stop(native_stopped || !cpu_reset_n),
        .cpu_line_ready(cpu_line_ready),.cpu_line_data(cpu_line_data),
        .load_valid(write_valid),.load_addr(write_addr),.load_data(write_data),.load_ready(write_ready),.load_done(write_done),
        .run_enable(memory_run),.busy(memory_busy),.fault(memory_error),.monitor(memory_monitor),
        .memory_addr(memory_addr),.memory_write(memory_write),.memory_wdata(memory_wdata),.memory_wstrb(memory_wstrb),
        .memory_burst(memory_burst),.memory_valid(memory_valid),.memory_ready(memory_ready),.memory_rdata(memory_rdata),
        .memory_rvalid(memory_rvalid),.memory_fault(memory_fault));
    wire [64:0] native_rtc;
    wire [15:0] native_display_request,display_read_data;
    wire [4:0] display_read_address;
    wire [8:0] diw_hstart,diw_hstop;
    wire [10:0] diw_vstart,diw_vstop;
    // The first 040 image reserves fabric for the FPU and caches. The
    // optional native profiling commands return the existing host error.
    multitini_amiga_host #(.AGA_PAL(AGA_PAL), .USE_AP68040(USE_AP68040), .ENABLE_DIAGNOSTICS(!USE_AP68040)) host_i(
        .control_clk(control_clk),.control_resetn(control_resetn),.write(host_write),.command(host_command),.status(host_status),
        .block_write(host_block_write),.block_read(host_block_read),
        .block_address(host_block_address),.block_read_address(host_block_read_address),
        .block_ram_read_address(host_block_ram_read_address),.block_wstrb(host_block_wstrb),.block_rdata(host_block_rdata),
        .guest_reset(native_stopped),
        .native_config({1'b0,cpu_double_speed,AGA_PAL,ide_fast,memcfg,cpucfg}),
        .diag_cpu_reset_n(cpu_reset_n),.diag_cpu_wait(cpu_req && !cpu_ready),.diag_cpu_write(cpu_state==2'b11),
        .diag_ide(ide_monitor),.diag_memory(memory_monitor),
        .clk(core_clk),.reset(reset),.chip_tick(chip_tick),
        .io_fpga(io_fpga),.io_uio(io_uio),.io_strobe(io_strobe),.io_din(io_din),.io_wait(io_wait),.io_dout(io_dout),
        .ide_req(ide_req),.ide_address(ide_address),.ide_read(ide_read),.ide_write(ide_write),.ide_wdata(ide_wdata),.ide_rdata(ide_rdata),
        .native_rtc(native_rtc),.native_display_request(native_display_request),
        .display_read_address(display_read_address),.display_read_data(display_read_data),
        .capture_mode(video_capture_mode),.display_switch_pending(video_switch_pending),
        .display_quiescent(video_quiescent));
    multitini_amiga_machine #(.AGA_PAL(AGA_PAL), .USE_AP68040(USE_AP68040)) amiga_machine(
        .clk_service28(core_clk),.clk_service56(cpu_clk2x),.cpu_double_speed(cpu_double_speed),
        .reset(reset),.guest_reset(native_guest_reset),
        .run_enable(memory_run),.clk_chip(chip_clk),.chip_tick(chip_tick),
        .chip_c1(chip_c1),.chip_c3(chip_c3),.chip_clk7_en(chip_clk7_en),.chip_clk7n_en(chip_clk7n_en),
        .chip_addr(chip_addr),.chip_wdata(chip_wdata),.chip_wdata2(chip_wdata2),.chip_oe_n(chip_oe_n),.chip_we_n(chip_we_n),
        .chip_ub_n(chip_ub_n),.chip_lb_n(chip_lb_n),.chip_long(chip_long),
        .chip_rdata(chip_rdata),.chip_rdata2(chip_rdata2),.chip_rdata48(chip_rdata48),
        .cpu_req(cpu_req),.cpu_addr(cpu_addr),.cpu_wdata(cpu_wdata),.cpu_state(cpu_state),.cpu_ub_n(cpu_ub_n),.cpu_lb_n(cpu_lb_n),
        .cpu_long(cpu_long),.cpu_wdata2(cpu_wdata2),.cpu_rdata(cpu_rdata),.cpu_rdata2(cpu_rdata2),.cpu_ready(cpu_ready),.cpu_ready_long(cpu_ready),
        .cpu_line(cpu_line),.cpu_line_ready(cpu_line_ready),.cpu_line_data(cpu_line_data),
        .io_uio(io_uio),.io_fpga(io_fpga),.io_strobe(io_strobe),.io_din(io_din),.io_wait(io_wait),.io_dout(io_dout),
        .ide_req(ide_req),.ide_address(ide_address),.ide_write(ide_write),.ide_read(ide_read),.ide_wdata(ide_wdata),.ide_rdata(ide_rdata),.ide_monitor(ide_monitor),
        .joy1(native_joy1),.joy2(native_joy2),.mouse_buttons(native_mouse_buttons),.kms_level(native_kms_level),
        .kbd_mouse_type(native_kbd_mouse_type),.kbd_mouse_data(native_kbd_mouse_data),
        .rtc(native_rtc),.display_request(native_display_request),
        .display_read_address(display_read_address),.display_read_data(display_read_data),
        .diw_hstart(diw_hstart),.diw_hstop(diw_hstop),.diw_vstart(diw_vstart),.diw_vstop(diw_vstop),
        .rgb(rgb),.hsync_n(hsync_n),.vsync_n(vsync_n),.hblank(hblank),.vblank(vblank),
        .field(field),.lace(lace),.ce_pix(ce_pix),.res(resolution),.ntsc(ntsc),
        .audio_l(native_audio_l),.audio_r(native_audio_r),.cpu_reset_n(cpu_reset_n),.cpucfg(cpucfg),.memcfg(memcfg),.ide_fast(ide_fast),.fdd_led(),.hdd_led());
    wire [15:0] normalized_rgb;
    wire [31:0] malformed_frames;
    multitini_amiga_video_normalizer normalizer(
        .clk(core_clk),.reset(reset),.ready(ready && !video_switch_pending),.chip_tick(chip_tick_q),.hsync_n(hsync_n),.vsync_n(vsync_n),
        .capture_mode(video_capture_mode),.native_lace(lace),.diw_hstart(diw_hstart),.diw_vstart(diw_vstart),
        .ntsc(ntsc),.hblank(hblank),.vblank(vblank),.field_short(field),.rgb(rgb),
        .scan_x(video_x),.scan_y(video_y),.rgb565(normalized_rgb),.frame_start(video_frame_start),
        .valid(video_valid),.frame_alt(video_frame_alt),.frame_weave(video_frame_weave),.frame_profile(video_frame_profile),.malformed_frames(malformed_frames));
    assign video_r={normalized_rgb[15:11],normalized_rgb[15:13]};
    assign video_g={normalized_rgb[10:5],normalized_rgb[10:9]};
    assign video_b={normalized_rgb[4:0],normalized_rgb[4:2]};
    assign video_de=video_valid,video_hsync=hsync_n,video_vsync=vsync_n,video_pal=!ntsc;
    assign core_id=32'h414d4947;
    assign load_error=loader_error || memory_error || memory_fault;
    assign ready=rom_loaded && config_valid && !load_error && !core_reset_request && cpu_reset_n;
    reg [26:0] audio_phase;
    always @(posedge core_clk)begin
        audio_valid<=0;
        if(reset)begin audio_phase<=0;audio_l<=0;audio_r<=0;end
        else if(audio_phase>=27'd28327196)begin
            audio_phase<=audio_phase-27'd28327196;
            audio_l<=ready ? {native_audio_l,9'd0} : 24'd0;
            audio_r<=ready ? {native_audio_r,9'd0} : 24'd0;audio_valid<=1;
        end else audio_phase<=audio_phase+27'd48000;
    end
endmodule
