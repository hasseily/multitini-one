import regs_savestates::*;
module cart_top (
    input ce_master,
	input             clk,
	input             ce,             // M2
	input             cpu_ce,         // CPU Phi1 clock (several mappers use m2 inverted)
	input             paused,         // This indicates the core is paused so anything using the master clock won't get messed up
	input             reset,
	input             vs_mode,        // Vs. motherboard memory configuration
	input      [63:0] flags,          // Misc flags from ines header {prg_size(3), chr_size(3), mapper(8)}
	input      [15:0] prg_ain,        // Better known as "CPU Address in"
	output reg [24:0] prg_aout,       // PRG Input / Output Address Lines ([25:22] extended Lines [Misc ROM])
	input             prg_read,       // PRG Read / write signals
	input             prg_write,
	input       [7:0] prg_din,        // CPU Data In
	output reg  [7:0] prg_dout,       // CPU Data Out
	input       [7:0] prg_from_ram,   // PRG Data from RAM
	output reg        prg_allow,      // PRG Allow write access
	output reg        prg_bus_write,  // PRG Data Driven
	output reg        prg_conflict,   // PRG Data is ROM & prg_din
	output reg        has_savestate,  // mapper supports savestates
	output reg        prg_conflict_d0,   // PRG Data is ROM & (prg_din | 1)
	output reg        has_flashsaves, // Homebrew mapper that saves to PRG-ROM in flash memory
	input       [9:0] prg_mask,       // PRG Mask for SDRAM translation
	input       [9:0] chr_mask,       // CHR Mask for SDRAM translation
	input             chr_ex,         // chr_addr is from an extra sprite read if high
	input             chr_read,       // Read from CHR
	input             chr_write,      // Write to CHR
	input       [7:0] chr_din,        // PPU Data In
	input      [13:0] chr_ain_orig,   // Better known as "PPU Address in"
	input      [13:0] chr_ain_ex,     // Address for extra sprite fetches
	output reg [21:0] chr_aout,       // CHR Input / Output Address Lines
	output reg  [7:0] chr_dout,       // Value to override CHR data with
	output reg        has_chr_dout,   // True if CHR data should be overridden
	output reg        chr_allow,      // CHR Allow write
	output reg        vram_a10,       // CHR Value for A10 address line
	output reg        vram_ce,        // CHR True if the address should be routed to the internal 2kB VRAM.
	output reg [17:0] mapper_addr,
	input       [7:0] mapper_data_in,
	output reg  [7:0] mapper_data_out,
	output reg        mapper_prg_write,
	output reg        mapper_ovr,
	output reg        irq,
	input      [15:0] audio_in,
	output reg [15:0] audio,          // External Audio
	output reg  [1:0] diskside,
	input             fds_busy,       // FDS Disk Swap Busy
	input             fds_eject,      // FDS Disk Swap Pause
	input             fds_auto_eject, // FDS Auto Swap Enabled
	input       [1:0] max_diskside,   // FDS disk side count
	input             fds_fast,       // FDS disk access speed
	// savestates
	input       [63:0]  SaveStateBus_Din,
	input       [ 9:0]  SaveStateBus_Adr,
	input               SaveStateBus_wren,
	input               SaveStateBus_rst,
	input               SaveStateBus_load,
	output logic [63:0]  SaveStateBus_Dout,

	input         Savestate_MAPRAMactive,
	input  [12:0] Savestate_MAPRAMAddr,
	input         Savestate_MAPRAMRdEn,
	input         Savestate_MAPRAMWrEn,
	input  [7:0]  Savestate_MAPRAMWriteData,
	output logic [7:0]  Savestate_MAPRAMReadData
);
logic [13:0] chr_ain;
reg [1023:0] me;
logic [21:0] prg_addr_b[0:6];
logic [21:0] chr_addr_b[0:6];
logic [7:0] prg_dout_b[0:6];
logic [7:0] chr_dout_b[0:6];
logic [0:0] prg_allow_b[0:6];
logic [0:0] chr_allow_b[0:6];
logic [0:0] vram_a10_b[0:6];
logic [0:0] vram_ce_b[0:6];
logic [0:0] irq_b[0:6];
logic [15:0] flags_out_b[0:6];
logic [15:0] audio_out_b[0:6];
logic [21:0] mux_prg_addr_b;
logic [21:0] mux_chr_addr_b;
logic [7:0] mux_prg_dout_b;
logic [7:0] mux_chr_dout_b;
logic [0:0] mux_prg_allow_b;
logic [0:0] mux_chr_allow_b;
logic [0:0] mux_vram_a10_b;
logic [0:0] mux_vram_ce_b;
logic [0:0] mux_irq_b;
logic [15:0] mux_flags_out_b;
logic [15:0] mux_audio_out_b;

assign chr_ain =  chr_ex ? chr_ain_ex : chr_ain_orig;

always @* begin me=0; me[flags[7:0]]=1; end











MMC1 mmc1(
    .ce_master(ce_master),
	.clk        (clk),
	.ce         (ce),
	.enable((me[1]) & ~reset),
	.flags      (flags),
	.prg_ain    (prg_ain),
	.prg_aout_b (prg_addr_b[0]),
	.prg_read   (prg_read),
	.prg_write  (prg_write),
	.prg_din    (prg_din),
	.prg_dout_b (prg_dout_b[0]),
	.prg_allow_b(prg_allow_b[0]),
	.chr_ain    (chr_ain),
	.chr_aout_b (chr_addr_b[0]),
	.chr_read   (chr_read),
	.chr_allow_b(chr_allow_b[0]),
	.vram_a10_b (vram_a10_b[0]),
	.vram_ce_b  (vram_ce_b[0]),
	.irq_b      (irq_b[0]),
	.flags_out_b(flags_out_b[0]),
	.audio_in   (audio_in),
	.audio_b    (audio_out_b[0]),
	// savestates
	.SaveStateBus_Din  (SaveStateBus_Din ),
	.SaveStateBus_Adr  (SaveStateBus_Adr ),
	.SaveStateBus_wren (SaveStateBus_wren),
	.SaveStateBus_rst  (SaveStateBus_rst ),
	.SaveStateBus_load (SaveStateBus_load ),
	.SaveStateBus_Dout()
);
assign chr_dout_b[0]=8'd0;
Mapper28 map28(
    .ce_master(ce_master),
	.clk        (clk),
	.ce         (ce),
	.enable((me[0]|me[2]|me[3]|me[7]) & ~reset),
	.flags      (flags),
	.prg_ain    (prg_ain),
	.prg_aout_b (prg_addr_b[1]),
	.prg_read   (prg_read),
	.prg_write  (prg_write),
	.prg_din    (prg_din),
	.prg_dout_b (prg_dout_b[1]),
	.prg_allow_b(prg_allow_b[1]),
	.chr_ain    (chr_ain),
	.chr_aout_b (chr_addr_b[1]),
	.chr_read   (chr_read),
	.chr_dout_b (chr_dout_b[1]), // Special port
	.chr_allow_b(chr_allow_b[1]),
	.vram_a10_b (vram_a10_b[1]),
	.vram_ce_b  (vram_ce_b[1]),
	.irq_b      (irq_b[1]),
	.flags_out_b(flags_out_b[1]),
	.audio_in   (audio_in),
	.audio_b    (audio_out_b[1]),
	// savestates
	.SaveStateBus_Din  (SaveStateBus_Din ),
	.SaveStateBus_Adr  (SaveStateBus_Adr ),
	.SaveStateBus_wren (SaveStateBus_wren),
	.SaveStateBus_rst  (SaveStateBus_rst ),
	.SaveStateBus_load (SaveStateBus_load ),
	.SaveStateBus_Dout()
);
MMC2 mmc2(
    .ce_master(ce_master),
	.clk        (clk),
	.ce         (ce),
	.enable((me[9]) & ~reset),
	.flags      (flags),
	.prg_ain    (prg_ain),
	.prg_aout_b (prg_addr_b[2]),
	.prg_read   (prg_read),
	.prg_write  (prg_write),
	.prg_din    (prg_din),
	.prg_dout_b (prg_dout_b[2]),
	.prg_allow_b(prg_allow_b[2]),
	.chr_ain    (chr_ain),
	.chr_aout_b (chr_addr_b[2]),
	.chr_read   (chr_read),
	.chr_allow_b(chr_allow_b[2]),
	.vram_a10_b (vram_a10_b[2]),
	.vram_ce_b  (vram_ce_b[2]),
	.irq_b      (irq_b[2]),
	.flags_out_b(flags_out_b[2]),
	.audio_in   (audio_in),
	.audio_b    (audio_out_b[2]),
	// Special ports
	.chr_ain_o  (chr_ain_orig),
	.paused     (paused),
	// savestates
	.SaveStateBus_Din  (SaveStateBus_Din ),
	.SaveStateBus_Adr  (SaveStateBus_Adr ),
	.SaveStateBus_wren (SaveStateBus_wren),
	.SaveStateBus_rst  (SaveStateBus_rst ),
	.SaveStateBus_load (SaveStateBus_load ),
	.SaveStateBus_Dout()
);
assign chr_dout_b[2]=8'd0;
MMC3 mmc3 (
    .ce_master(ce_master),
	.clk        (clk),
	.ce         (ce),
	.enable((me[4]) & ~reset),
	.flags      (flags),
	.prg_ain    (prg_ain),
	.prg_aout_b (prg_addr_b[3]),
	.prg_read   (prg_read),
	.prg_write  (prg_write),
	.prg_din    (prg_din),
	.prg_dout_b (prg_dout_b[3]),
	.prg_allow_b(prg_allow_b[3]),
	.chr_ain    (chr_ain),
	.chr_aout_b (chr_addr_b[3]),
	.chr_read   (chr_read),
	.chr_allow_b(chr_allow_b[3]),
	.vram_a10_b (vram_a10_b[3]),
	.vram_ce_b  (vram_ce_b[3]),
	.irq_b      (irq_b[3]),
	.flags_out_b(flags_out_b[3]),
	.audio_in   (audio_in),
	.audio_b    (audio_out_b[3]),
	// Special ports
	.chr_ain_o  (chr_ain_orig),
	.m2_inv     (cpu_ce),
	.paused     (paused),
	// savestates
	.SaveStateBus_Din  (SaveStateBus_Din ),
	.SaveStateBus_Adr  (SaveStateBus_Adr ),
	.SaveStateBus_wren (SaveStateBus_wren),
	.SaveStateBus_rst  (SaveStateBus_rst ),
	.SaveStateBus_load (SaveStateBus_load ),
	.SaveStateBus_Dout()
);
assign chr_dout_b[3]=8'd0;
MMC4 mmc4(
    .ce_master(ce_master),
	.clk        (clk),
	.ce         (ce),
	.enable((me[10]) & ~reset),
	.flags      (flags),
	.prg_ain    (prg_ain),
	.prg_aout_b (prg_addr_b[4]),
	.prg_read   (prg_read),
	.prg_write  (prg_write),
	.prg_din    (prg_din),
	.prg_dout_b (prg_dout_b[4]),
	.prg_allow_b(prg_allow_b[4]),
	.chr_ain    (chr_ain),
	.chr_aout_b (chr_addr_b[4]),
	.chr_read   (chr_read),
	.chr_allow_b(chr_allow_b[4]),
	.vram_a10_b (vram_a10_b[4]),
	.vram_ce_b  (vram_ce_b[4]),
	.irq_b      (irq_b[4]),
	.flags_out_b(flags_out_b[4]),
	.audio_in   (audio_in),
	.audio_b    (audio_out_b[4]),
	// Special ports
	.chr_ain_o  (chr_ain_orig),
	.paused     (paused),
	// savestates
	.SaveStateBus_Din  (SaveStateBus_Din ),
	.SaveStateBus_Adr  (SaveStateBus_Adr ),
	.SaveStateBus_wren (SaveStateBus_wren),
	.SaveStateBus_rst  (SaveStateBus_rst ),
	.SaveStateBus_load (SaveStateBus_load ),
	.SaveStateBus_Dout()
);
assign chr_dout_b[4]=8'd0;
Mapper66 map66(
    .ce_master(ce_master),
	.clk        (clk),
	.ce         (ce),
	.enable((me[11]|me[66]) & ~reset),
	.flags      (flags),
	.prg_ain    (prg_ain),
	.prg_aout_b (prg_addr_b[5]),
	.prg_read   (prg_read),
	.prg_write  (prg_write),
	.prg_din    (prg_din),
	.prg_dout_b (prg_dout_b[5]),
	.prg_allow_b(prg_allow_b[5]),
	.chr_ain    (chr_ain),
	.chr_aout_b (chr_addr_b[5]),
	.chr_read   (chr_read),
	.chr_allow_b(chr_allow_b[5]),
	.vram_a10_b (vram_a10_b[5]),
	.vram_ce_b  (vram_ce_b[5]),
	.irq_b      (irq_b[5]),
	.flags_out_b(flags_out_b[5]),
	.audio_in   (audio_in),
	.audio_b    (audio_out_b[5]),
	// savestates
	.SaveStateBus_Din  (SaveStateBus_Din ),
	.SaveStateBus_Adr  (SaveStateBus_Adr ),
	.SaveStateBus_wren (SaveStateBus_wren),
	.SaveStateBus_rst  (SaveStateBus_rst ),
	.SaveStateBus_load (SaveStateBus_load ),
	.SaveStateBus_Dout()
);
assign chr_dout_b[5]=8'd0;
Mapper71 map71(
    .ce_master(ce_master),
	.clk        (clk),
	.ce         (ce),
	.enable((me[71]) & ~reset),
	.flags      (flags),
	.prg_ain    (prg_ain),
	.prg_aout_b (prg_addr_b[6]),
	.prg_read   (prg_read),
	.prg_write  (prg_write),
	.prg_din    (prg_din),
	.prg_dout_b (prg_dout_b[6]),
	.prg_allow_b(prg_allow_b[6]),
	.chr_ain    (chr_ain),
	.chr_aout_b (chr_addr_b[6]),
	.chr_read   (chr_read),
	.chr_allow_b(chr_allow_b[6]),
	.vram_a10_b (vram_a10_b[6]),
	.vram_ce_b  (vram_ce_b[6]),
	.irq_b      (irq_b[6]),
	.flags_out_b(flags_out_b[6]),
	.audio_in   (audio_in),
	.audio_b    (audio_out_b[6]),
	// savestates
	.SaveStateBus_Din  (SaveStateBus_Din ),
	.SaveStateBus_Adr  (SaveStateBus_Adr ),
	.SaveStateBus_wren (SaveStateBus_wren),
	.SaveStateBus_rst  (SaveStateBus_rst ),
	.SaveStateBus_load (SaveStateBus_load ),
	.SaveStateBus_Dout()
);
assign chr_dout_b[6]=8'd0;
assign mux_prg_addr_b =  prg_addr_b[0] | prg_addr_b[1] | prg_addr_b[2] | prg_addr_b[3] | prg_addr_b[4] | prg_addr_b[5] | prg_addr_b[6];
assign mux_chr_addr_b =  chr_addr_b[0] | chr_addr_b[1] | chr_addr_b[2] | chr_addr_b[3] | chr_addr_b[4] | chr_addr_b[5] | chr_addr_b[6];
assign mux_prg_dout_b =  prg_dout_b[0] | prg_dout_b[1] | prg_dout_b[2] | prg_dout_b[3] | prg_dout_b[4] | prg_dout_b[5] | prg_dout_b[6];
assign mux_chr_dout_b =  chr_dout_b[0] | chr_dout_b[1] | chr_dout_b[2] | chr_dout_b[3] | chr_dout_b[4] | chr_dout_b[5] | chr_dout_b[6];
assign mux_prg_allow_b =  prg_allow_b[0] | prg_allow_b[1] | prg_allow_b[2] | prg_allow_b[3] | prg_allow_b[4] | prg_allow_b[5] | prg_allow_b[6];
assign mux_chr_allow_b =  chr_allow_b[0] | chr_allow_b[1] | chr_allow_b[2] | chr_allow_b[3] | chr_allow_b[4] | chr_allow_b[5] | chr_allow_b[6];
assign mux_vram_a10_b =  vram_a10_b[0] | vram_a10_b[1] | vram_a10_b[2] | vram_a10_b[3] | vram_a10_b[4] | vram_a10_b[5] | vram_a10_b[6];
assign mux_vram_ce_b =  vram_ce_b[0] | vram_ce_b[1] | vram_ce_b[2] | vram_ce_b[3] | vram_ce_b[4] | vram_ce_b[5] | vram_ce_b[6];
assign mux_irq_b =  irq_b[0] | irq_b[1] | irq_b[2] | irq_b[3] | irq_b[4] | irq_b[5] | irq_b[6];
assign mux_flags_out_b =  flags_out_b[0] | flags_out_b[1] | flags_out_b[2] | flags_out_b[3] | flags_out_b[4] | flags_out_b[5] | flags_out_b[6];
assign mux_audio_out_b =  audio_out_b[0] | audio_out_b[1] | audio_out_b[2] | audio_out_b[3] | audio_out_b[4] | audio_out_b[5] | audio_out_b[6];

always @* begin
    {prg_aout, prg_allow, chr_aout, vram_a10, vram_ce, chr_allow,
     prg_dout, chr_dout, irq, audio} =
    {3'b000, mux_prg_addr_b, mux_prg_allow_b, mux_chr_addr_b,
     mux_vram_a10_b, mux_vram_ce_b, mux_chr_allow_b,
     mux_prg_dout_b, mux_chr_dout_b, mux_irq_b, mux_audio_out_b};
    mapper_addr=0; mapper_data_out=0; mapper_prg_write=0; mapper_ovr=0;
    diskside=0;
    has_chr_dout=mux_flags_out_b[0]; prg_bus_write=mux_flags_out_b[1];
    prg_conflict=mux_flags_out_b[2]; has_savestate=0;
    prg_conflict_d0=mux_flags_out_b[4]; has_flashsaves=0;
    if (!prg_aout[21]) prg_aout[20:0]={prg_aout[20:11]&prg_mask,prg_aout[10:0]};
    if (chr_aout[21:20]==2'b10) chr_aout[19:0]={chr_aout[19:11]&chr_mask[8:0],chr_aout[10:0]};
    if(vram_ce) chr_aout={11'b11_1010_0000_0,vram_a10,chr_ain[9:0]};
    if(prg_ain<16'h2000) begin prg_aout={14'b00011_1000_0000_0,prg_ain[10:0]}; prg_allow=1; end
end
assign SaveStateBus_Dout=64'd0;
assign Savestate_MAPRAMReadData=8'd0;
endmodule
