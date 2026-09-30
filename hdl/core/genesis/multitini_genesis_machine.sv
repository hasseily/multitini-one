// SPDX-License-Identifier: GPL-3.0-only
// Board-independent Nuked-MD integration. The original chipset advances only
// on machine_clk; the cache, loader and save port run on the parent clk.
module multitini_genesis_machine (
    input wire clk, machine_clk, reset, pause_request,
    input wire region_jp, region_pal,
    input wire [1:0] six_button,
    input wire fm_chip,
    input wire [1:0] lowpass,
    input wire [11:0] buttons0, buttons1,
    input wire cart_download, cart_load_wr,
    input wire [22:0] cart_load_addr,
    input wire [15:0] cart_load_data,
    input wire [15:0] save_addr,
    input wire save_wr,
    input wire [7:0] save_data,
    output wire [7:0] save_q,
    output wire save_changed,
    output wire [23:0] rom_addr,
    output wire rom_request,
    input wire rom_data_valid,
    input wire [15:0] rom_data,
    output wire rom_stall, machine_ready,
    output wire [7:0] video_r,video_g,video_b,
    output wire video_ce,video_de,video_vs,video_240,
    output wire signed [15:0] audio_l,audio_r
);
    wire [14:0] ram_68k_address;
    wire [1:0] ram_68k_byteena;
    wire [15:0] ram_68k_data,ram_68k_q;
    wire ram_68k_write;
    wire [12:0] ram_z80_address;
    wire [7:0] ram_z80_data,ram_z80_q;
    wire ram_z80_write;
    wire [22:0] cart_address;
    wire [15:0] cart_data,cart_data_wr;
    wire cart_cs,cart_oe,cart_lwr,cart_uwr,cart_time,cart_dma,cart_data_en,cart_dtack;
    wire [6:0] pa_in,pa_out,pa_dir,pb_in,pb_out,pb_dir;
    wire [7:0] raw_r,raw_g,raw_b;
    wire raw_hs,raw_vs,vdp_hclk1,vdp_de_h,vdp_de_v,vdp_intfield,vdp_m2,vdp_m5,vdp_rs1;
    wire [8:0] mol,mor;
    wire [9:0] mol_2612,mor_2612;
    wire [15:0] psg;
    wire fm_clk1,fm_sel23,ym2612_quirk;

    reg reset_previous=0;
    reg [15:0] ram_initialize=0;
    always @(posedge clk) begin
        reset_previous<=reset;
        if(reset && !reset_previous) ram_initialize<=0;
        else if(!ram_initialize[15]) ram_initialize<=ram_initialize+1'b1;
    end
    wire chip_reset=reset || !ram_initialize[15];
    assign machine_ready=!chip_reset;
    multitini_genesis_ram #(.ADDRESS_WIDTH(15),.DATA_WIDTH(16)) ram68k_i (
        .clk(clk),.address(ram_68k_address),.data(ram_68k_data),
        .byte_enable(ram_68k_byteena),.write_enable(ram_68k_write && !chip_reset && !pause_request && !rom_stall),
        .q(ram_68k_q),.initialize(!ram_initialize[15]),
        .initialize_address(ram_initialize[14:0]),.initialize_data(16'h0000));
    multitini_genesis_ram #(.ADDRESS_WIDTH(13),.DATA_WIDTH(8)) ramz80_i (
        .clk(clk),.address(ram_z80_address),.data(ram_z80_data),
        .byte_enable(1'b1),.write_enable(ram_z80_write && !chip_reset && !pause_request && !rom_stall),
        .q(ram_z80_q),.initialize(!ram_initialize[15]),
        .initialize_address(ram_initialize[12:0]),.initialize_data(8'hc7));

    md_board board_i (
        .MCLK2(machine_clk),.ext_reset(chip_reset),.reset_button(1'b0),
        .ext_vres(1'b0),.ext_zres(1'b0),
        .ram_68k_address(ram_68k_address),.ram_68k_byteena(ram_68k_byteena),
        .ram_68k_data(ram_68k_data),.ram_68k_wren(ram_68k_write),.ram_68k_o(ram_68k_q),
        .ram_z80_address(ram_z80_address),.ram_z80_data(ram_z80_data),
        .ram_z80_wren(ram_z80_write),.ram_z80_o(ram_z80_q),
        .M3(1'b1),.cart_data(cart_data),.cart_data_en(cart_data_en),
        .cart_address(cart_address),.cart_cs(cart_cs),.vdp_dma_oe_early(cart_oe),
        .cart_lwr(cart_lwr),.cart_uwr(cart_uwr),.cart_time(cart_time),
        .cart_data_wr(cart_data_wr),.vdp_dma(cart_dma),.cart_m3_pause(1'b0),
        .ext_dtack(cart_dtack),.pal(region_pal),.jap(region_jp),
        .tmss_enable(1'b0),.tmss_data(16'hffff),
        .V_R(raw_r),.V_G(raw_g),.V_B(raw_b),.V_HS(raw_hs),.vdp_vsync2(raw_vs),
        .MOL(mol),.MOR(mor),.MOL_2612(mol_2612),.MOR_2612(mor_2612),.PSG(psg),
        .fm_clk1(fm_clk1),.fm_sel23(fm_sel23),
        .PA_i(pa_in),.PA_o(pa_out),.PA_d(pa_dir),
        .PB_i(pb_in),.PB_o(pb_out),.PB_d(pb_dir),.PC_i(7'h7f),
        .vdp_hclk1(vdp_hclk1),.vdp_intfield(vdp_intfield),
        .vdp_de_h(vdp_de_h),.vdp_de_v(vdp_de_v),.vdp_m2(vdp_m2),
        .vdp_m5(vdp_m5),.vdp_rs1(vdp_rs1),.vdp_cramdot_dis(1'b0),
        .ym2612_status_enable(ym2612_quirk),
        .dma_68k_req(1'b0),.dma_z80_req(1'b0));

    multitini_genesis_cartridge cartridge_i (
        .clk(clk),.reset(chip_reset),.pause_request(pause_request),
        .cart_dl(cart_download),.cart_dl_wr(cart_load_wr),
        .cart_load_addr(cart_load_addr),.cart_load_data(cart_load_data),
        .cart_addr(cart_address),.cart_data_wr(cart_data_wr),.cart_cs(cart_cs),.cart_oe(cart_oe),
        .cart_lwr(cart_lwr),.cart_uwr(cart_uwr),.cart_time(cart_time),.cart_dma(cart_dma),
        .cart_data(cart_data),.cart_data_en(cart_data_en),.cart_dtack(cart_dtack),
        .save_addr(save_addr),.save_wr(save_wr),.save_data(save_data),.save_q(save_q),.save_change(save_changed),
        .ym2612_quirk(ym2612_quirk),.rom_address(rom_addr),.rom_request(rom_request),
        .rom_data(rom_data),.rom_data_valid(rom_data_valid),.rom_stall(rom_stall));

    genvar port_number;
    generate for(port_number=0;port_number<2;port_number=port_number+1) begin: pads
        wire [11:0] buttons=port_number==0 ? buttons0 : buttons1;
        wire [6:0] pin_out=port_number==0 ? pa_out : pb_out;
        wire [6:0] pin_dir=port_number==0 ? pa_dir : pb_dir;
        wire [6:0] pin_in;
        if(port_number==0) assign pa_in=pin_in;
        else assign pb_in=pin_in;
        pad_io pad_i (
            .clk(machine_clk),.reset(chip_reset),.MODE(six_button[port_number]),.SMS(1'b0),
            .P_RIGHT(buttons[0]),.P_LEFT(buttons[1]),.P_DOWN(buttons[2]),.P_UP(buttons[3]),
            .P_A(buttons[4]),.P_B(buttons[5]),.P_C(buttons[6]),.P_START(buttons[7]),
            .P_MODE(buttons[8]),.P_X(buttons[9]),.P_Y(buttons[10]),.P_Z(buttons[11]),
            .GUN_EN(1'b0),.GUN_TYPE(1'b0),.GUN_SENSOR(1'b0),
            .GUN_A(1'b0),.GUN_B(1'b0),.GUN_C(1'b0),.GUN_START(1'b0),
            .MOUSE_EN(1'b0),.MOUSE_FLIPY(1'b0),.MOUSE(25'd0),
            .port_out(pin_in),.port_in(pin_out),.port_dir(pin_dir));
    end endgenerate

    wire horizontal_blank,vertical_blank;
    multitini_genesis_video_cond video_i (
        .clk(machine_clk),.vdp_hclk1(vdp_hclk1),.vdp_de_h(vdp_de_h),.vdp_de_v(vdp_de_v),
        .vdp_intfield(vdp_intfield),.vdp_m2(vdp_m2),.vdp_m5(vdp_m5),.vdp_rs1(vdp_rs1),
        .r_in(raw_r),.g_in(raw_g),.b_in(raw_b),.hs_in(raw_hs),.vs_in(raw_vs),
        .pal(region_pal),.border_en(1'b0),.h40corr(1'b0),.blender(1'b0),
        .ce_pix(video_ce),.r_out(video_r),.g_out(video_g),.b_out(video_b),
        .vs_out(video_vs),.hbl_out(horizontal_blank),.vbl_out(vertical_blank));
    assign video_de=!(horizontal_blank || vertical_blank) && !chip_reset;
    assign video_240=vdp_m2;

    // The upstream conditioner coefficients assume half the chipset clock.
    // This is an explicitly constrained generated clock, buffered globally.
    reg audio_divider=0;
    always @(posedge clk) audio_divider<=!audio_divider;
    wire audio_clk;
    BUFG audio_clock_i(.I(audio_divider),.O(audio_clk));
    audio_cond audio_i (
        .clk(audio_clk),.reset(chip_reset),.mute(pause_request || chip_reset),
        .lpf_mode(lowpass),.fm_mode(fm_chip),.fm_clk1(fm_clk1),.fm_sel23(fm_sel23),
        .MOL(mol),.MOR(mor),.MOL_2612(mol_2612),.MOR_2612(mor_2612),.PSG(psg),
        .sms_fm_audio(14'd0),.AUDIO_L(audio_l),.AUDIO_R(audio_r));
endmodule
