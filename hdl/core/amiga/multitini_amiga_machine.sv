// SPDX-License-Identifier: GPL-3.0-only
// Board-independent ECS machine wiring around the pinned Minimig chipset/CPUs.
// CPU phase relation derives from Minimig.sv by Alexey Melnikov, 2017-2020,
// MiSTer Minimig e47c3457b486d09daaf47c8934bda90cf7723045 (GPL-3.0-or-later).
// The exact reference and complete notices are retained in
// third_party/amiga/upstream/Minimig.sv, README.md and gpl.txt.
// Memory lives in the adapter. run_enable stops the entire virtual timeline;
// fixed chip-DMA deadlines must be met before releasing it. Chip, CIA, CPU and
// Paula therefore retain their native clock ratios even during a memory miss.
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
    input wire [15:0] display_request,
    input wire [4:0] display_read_address,
    output wire [15:0] display_read_data,
    output wire [23:0] rgb,
    output wire hsync_n,vsync_n,hblank,vblank,field,lace,ce_pix,
    output wire [1:0] res,
    output wire ntsc,
    output wire [8:0] diw_hstart,diw_hstop,
    output wire [10:0] diw_vstart,diw_vstop,
    output wire signed [14:0] audio_l,audio_r,
    output wire cpu_reset_n,
    output wire [4:0] cpucfg,
    output wire [6:0] memcfg,
    output wire ide_fast,
    output wire fdd_led,hdd_led
);
    // The memory/host/video service runs on the ungated sibling clock. Its
    // buses therefore have a complete native period for timing closure.
    wire native_enable=run_enable | reset;
    reg accepted_native_edge=0;
    always @(negedge clk_service28)accepted_native_edge<=native_enable;
    BUFGCE machine_clock_gate(.I(clk_service28),.CE(accepted_native_edge),.O(clk_chip));
    assign chip_tick=accepted_native_edge && !reset;

    wire cck;
    wire [9:0] eclk;
    amiga_clk clocks(
        .clk_28(clk_chip),.clk7_en(chip_clk7_en),.clk7n_en(chip_clk7n_en),
        .c1(chip_c1),.c3(chip_c3),.cck(cck),.eclk(eclk),.reset_n(!reset));

    // Exact values sampled by FX68K in the upstream 113MHz div/C1 phase
    // generator. Unlike clk7_en, this table preserves its custom-bus phase.
    // C1/C3 stop with the native clock, so no independent fast counter drifts
    // during a memory hold. See the held-clock/reset phase equivalence proof.
    wire cpu_ph1=cpu_reset_n && !chip_c1 && chip_c3;
    wire cpu_ph2=cpu_reset_n && chip_c1 && !chip_c3;

    wire [23:1] cpu_chip_addr;
    wire [15:0] cpu_chip_din,cpu_chip_dout;
    wire [15:0] chipset_cpu_data,display_guest_data;
    wire display_guest_selected;
    assign cpu_chip_dout=display_guest_selected ? display_guest_data : chipset_cpu_data;
    wire cpu_chip_as,cpu_chip_uds,cpu_chip_lds,cpu_chip_rw,cpu_chip_dtack;
    wire [2:0] cpu_chip_ipl;
    wire cpu_chip_long,chip32_en;
    wire cpu_reset_out;
    wire [31:0] cpu_nmi_addr;
    wire [3:0] cpu_cacr;
    wire [2:0] cachecfg;
    wire bootrom,ide_ena;
    wire [15:0] fastchip_dout,classic_ide_rdata,fast_ide_rdata;
    wire [5:0] classic_ide_req,fast_ide_req;
    wire fastchip_sel,fastchip_lds,fastchip_uds,fastchip_rnw,fastchip_lw;
    wire fastchip_selack,fastchip_ready,fast_ide_irq,fast_ide_led,classic_ide_led;
    assign ide_req=ide_fast ? fast_ide_req : classic_ide_req;
    assign ide_rdata=ide_fast ? fast_ide_rdata : classic_ide_rdata;
    assign hdd_led=ide_fast ? fast_ide_led : classic_ide_led;
    wire ar_fetch;
    wire cpu_shared;
    wire cpu_ram_sel;
    wire cpu_ram_long;
    wire cpu_step;
    wire [15:0] cpu_ram_wdata2;
    reg cpu_retirement_hold=0;
    wire native_cpu_ready=cpu_ready && !cpu_retirement_hold;
    // Give each TG68K retirement a native-cycle CS/ACK gap, including repeated
    // identical addresses. No raw CPU/MMU bus is sampled by a fast domain.
    always @(posedge clk_chip or posedge reset)
        if(reset)cpu_retirement_hold<=0;
        else if(!cpu_reset_n || cpu_retirement_hold)cpu_retirement_hold<=0;
        else if(cpucfg[1] && cpu_ram_sel && native_cpu_ready && cpu_step)cpu_retirement_hold<=1;
    assign cpu_req=cpu_ram_sel && !cpu_retirement_hold;
    assign cpu_long=cpucfg[1] && cpu_ram_long;
    assign cpu_wdata2=cpu_long ? cpu_ram_wdata2 : 16'd0;
    wire z2ram_ena,z3ram_ena0,z3ram_ena1,dcache_sw_en;
    wire [4:0] z3ram_base0;
    wire [3:0] z3ram_base1;
    wire [15:0] chip_cpu_din2;

    cpu_wrapper cpu(
        .reset(cpu_reset_n),.reset_out(cpu_reset_out),.clk(clk_chip),
        .ph1(cpu_ph1),.ph2(cpu_ph2),
        .cpucfg(cpucfg),.cpu_step(cpu_step),.ntsc(ntsc),.fastramcfg(memcfg[6:4]),
        .cachecfg(cachecfg),.bootrom(bootrom),
        .chip_addr(cpu_chip_addr),.chip_dout(cpu_chip_dout),.chip_din(cpu_chip_din),
        .chip_as(cpu_chip_as),.chip_uds(cpu_chip_uds),.chip_lds(cpu_chip_lds),
        .chip_rw(cpu_chip_rw),.chip_dtack(cpu_chip_dtack),.chip_ipl(cpu_chip_ipl),
        .chip32_en(chip32_en),.chip_long(cpu_chip_long),
        .chip_din2(chip_cpu_din2),.chip_dout2(chip_rdata2),
        .fastchip_dout(fastchip_dout),.fastchip_sel(fastchip_sel),.fastchip_lds(fastchip_lds),.fastchip_uds(fastchip_uds),
        .fastchip_rnw(fastchip_rnw),.fastchip_lw(fastchip_lw),.fastchip_selack(fastchip_selack),.fastchip_ready(fastchip_ready),
        .ramsel(cpu_ram_sel),.ramaddr(cpu_addr),.ramdin(cpu_wdata),
        .ramdout(cpu_rdata),.ramdout2(cpu_rdata),.ramready(native_cpu_ready),
        .ramlds(cpu_lb_n),.ramuds(cpu_ub_n),.ramshared(cpu_shared),
        .toccata_ena(),.toccata_base(),.a2065_ena(),.a2065_base(),
        .cdtv_mode(1'b0),.cdtv_base(),.ar_on(1'b0),.ar_fetch(ar_fetch),
        .cdtv_din(16'd0),.cdtv_selack(1'b0),
        .cpustate(cpu_state),.cacr(cpu_cacr),.nmi_addr(cpu_nmi_addr),
        .z2ram_ena(z2ram_ena),.z3ram_base0(z3ram_base0),.z3ram_ena0(z3ram_ena0),
        .z3ram_base1(z3ram_base1),.z3ram_ena1(z3ram_ena1),
        .ramlong1(cpu_ram_long),.ramdin_lo(cpu_ram_wdata2),.ramdout_hi(cpu_rdata2),
        .ramready_l(cpu_ready_long && !cpu_retirement_hold),.dcache_sw_en(dcache_sw_en));
    assign chip_wdata2=chip_cpu_din2;

    // The upstream CPU-side IDE path shares the accepted native clock here.
    // Management strobes belong exclusively to the selected backend. A mode
    // change is configured with the guest held reset, before media presence.
    fastchip fastchip_i(
        .clk_sys(clk_chip),.reset(!cpu_reset_n || !cpu_reset_out),
        .sel(fastchip_sel),.sel_ack(fastchip_selack),.ready(fastchip_ready),
        .addr({cpu_chip_addr,1'b0}),.din(cpu_chip_din),.dout(fastchip_dout),
        .lds(!fastchip_lds),.uds(!fastchip_uds),.rnw(fastchip_rnw),.longword(fastchip_lw),
        .ide_ena(ide_ena && ide_fast),.ide_irq(fast_ide_irq),.ide_req(fast_ide_req),
        .ide_address(ide_address),.ide_write(ide_write && ide_fast),.ide_read(ide_read && ide_fast),
        .ide_writedata(ide_wdata),.ide_readdata(fast_ide_rdata),.ide_led(fast_ide_led));

    multitini_amiga_display_mailbox display_i(
        .service_clk(clk_service28),.native_clk(clk_chip),.reset(reset),.guest_reset(guest_reset),
        .request(display_request),.address(cpu_chip_addr),.as_n(cpu_chip_as),.rw(cpu_chip_rw),
        .uds_n(cpu_chip_uds),.lds_n(cpu_chip_lds),.dtack_n(cpu_chip_dtack),.write_data(cpu_chip_din),
        .read_selected(display_guest_selected),.read_data(display_guest_data),
        .arm_address(display_read_address),.arm_data(display_read_data));

    minimig chipset(
        .cpu_address(cpu_chip_addr),.cpu_data(chipset_cpu_data),.cpudata_in(cpu_chip_din),
        ._cpu_ipl(cpu_chip_ipl),._cpu_as(cpu_chip_as),._cpu_uds(cpu_chip_uds),
        ._cpu_lds(cpu_chip_lds),.cpu_r_w(cpu_chip_rw),.cpu_long(cpu_chip_long),
        .chip32_en(chip32_en),.ram_long(chip_long),._cpu_dtack(cpu_chip_dtack),
        ._cpu_reset(cpu_reset_n),._cpu_reset_in(cpu_reset_out),
        .nmi_addr(cpu_nmi_addr),.ovr(),.cart_ar(1'b0),.ar_fetch(ar_fetch),
        .ram_data(chip_wdata),.ramdata_in(chip_rdata),.ram_address(chip_addr),
        ._ram_bhe(chip_ub_n),._ram_ble(chip_lb_n),._ram_we(chip_we_n),
        ._ram_oe(chip_oe_n),.chip48(chip_rdata48),
        .rst_ext(guest_reset | reset),.rst_out(),.clk(clk_chip),
        .clk7_en(chip_clk7_en),.clk7n_en(chip_clk7n_en),.c1(chip_c1),.c3(chip_c3),
        .cck(cck),.eclk(eclk),
        .rxd(1'b1),.txd(),.cts(1'b1),.rts(),.dtr(),.dsr(1'b1),.cd(1'b1),.ri(1'b1),
        ._joy1(joy1),._joy2(joy2),._joy3(16'hffff),._joy4(16'hffff),
        .joya1(16'd0),.joya2(16'd0),.mouse_btn(mouse_buttons),
        .kms_level(kms_level),.kbd_mouse_type(kbd_mouse_type),.kbd_mouse_data(kbd_mouse_data),
        .pwr_led(),.fdd_led(fdd_led),.hdd_led(classic_ide_led),.rtc(rtc),
        .IO_UIO(io_uio),.IO_FPGA(io_fpga),.IO_STROBE(io_strobe),
        .IO_WAIT(io_wait),.IO_DIN(io_din),.IO_DOUT(io_dout),
        ._hsync(hsync_n),._vsync(vsync_n),._csync(),.field1(field),.lace(lace),
        .hblank(hblank),.vblank(vblank),.red(rgb[23:16]),.green(rgb[15:8]),.blue(rgb[7:0]),
        .ar(),.scanline(),.ce_pix(ce_pix),.res(res),.ntsc(ntsc),
        .diw_hstart(diw_hstart),.diw_hstop(diw_hstop),
        .diw_vstart(diw_vstart),.diw_vstop(diw_vstop),
        .ldata(audio_l),.rdata(audio_r),.ldata_okk(),.rdata_okk(),.aud_mix(),
        .toccata_ena(1'b0),.toccata_base(8'd0),.cdtv_base(8'd0),
        .toccata_aud_left(),.toccata_aud_right(),.a2065_ena(1'b0),.a2065_base(8'd0),
        .cdtv_mode(),.cdtv_din(),.cdtv_selack(),
        .cdtv_cs(1'b0),.cdtv_cs_sec(1'b0),.cdtv_cs_stch(1'b0),
        .cdtv_cs_nvr(1'b0),.cdtv_cs_card(1'b0),.cdtv_wr(1'b0),.cdtv_rd(1'b0),
        .cdtv_uio_din(16'd0),.cdtv_uio_dout(),.cdtv_req(),.cdtv_subq_push(1'b0),
        .cdtv_subq_byte(8'd0),.cdtv_sten_pulse(1'b0),.cdtv_scor_pulse(1'b0),.cdtv_sbcp_pulse(1'b0),
        .cdtv_dma_req(),.cdtv_dma_we(),.cdtv_dma_baddr(),.cdtv_dma_wbyte(),.cdtv_dma_ack(1'b0),
        .cdtv_nvr_dirty(),.cdtv_card_dirty(),.cdtv_cdda_volume(),.cdtv_cdda_volume_valid(),
        .cpucfg(cpucfg),.cachecfg(cachecfg),.memcfg(memcfg),.bootrom(bootrom),.ide_ena(ide_ena),
        .ide_fast(ide_fast),.ide_ext_irq(fast_ide_irq),.akiko_irq(1'b0),.ide_req(classic_ide_req),
        .ide_address(ide_address),.ide_write(ide_write && !ide_fast),.ide_writedata(ide_wdata),
        .ide_read(ide_read && !ide_fast),.ide_readdata(classic_ide_rdata),
        .USER_IN(7'h7f),.USER_OUT(),.user_port_mode(),.mister_floppy_status(),.drive_snd(),
        .a2065_clk_ddr(clk_chip),.a2065_mem_address(),.a2065_mem_burstcount(),.a2065_mem_read(),
        .a2065_mem_readdata(64'd0),.a2065_mem_readdatavalid(1'b0),
        .a2065_mem_writedata(),.a2065_mem_byteenable(),.a2065_mem_write(),.a2065_mem_waitrequest(1'b0));
endmodule
