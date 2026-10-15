import regs_savestates::*;
// Derived from pinned MiSTer NES. See third_party/nes/README.md.
module MMC3 (
    input ce_master,
	input        clk,         // System clock
	input        ce,          // M2 ~cpu_clk
	input        enable,      // Mapper enabled
	input [31:0] flags,       // Cart flags
	input [15:0] prg_ain,     // prg address
	output logic [21:0] prg_aout_b,  // prg address out
	input        prg_read,    // prg read
	input        prg_write,   // prg write
	input  [7:0] prg_din,     // prg data in
	output logic [7:0] prg_dout_b,  // prg data out
	output logic prg_allow_b, // Enable access to memory for the specified operation.
	input [13:0] chr_ain,     // chr address in
	output logic [21:0] chr_aout_b,  // chr address out
	input        chr_read,    // chr ram read
	output logic chr_allow_b, // chr allow write
	output logic vram_a10_b,  // Value for A10 address line
	output logic vram_ce_b,   // True if the address should be routed to the internal 2kB VRAM.
	output logic irq_b,       // IRQ
	input [15:0] audio_in,    // Inverted audio from APU
	output logic [15:0] audio_b,     // Mixed audio output
	output logic [15:0] flags_out_b, // flags {0, 0, 0, 0, has_savestate, prg_conflict, prg_bus_write, has_chr_dout}
	input [13:0] chr_ain_o,
	input        m2_inv,
	input        paused,
	// savestates
	input       [63:0]  SaveStateBus_Din,
	input       [ 9:0]  SaveStateBus_Adr,
	input               SaveStateBus_wren,
	input               SaveStateBus_rst,
	input               SaveStateBus_load,
	output logic [63:0]  SaveStateBus_Dout
);
localparam SAVESTATE_MODULES    = 3;
logic [21:0] prg_aout, chr_aout;
reg [7:0] prg_dout;
logic prg_allow;
logic chr_allow;
logic vram_a10;
logic vram_ce;
logic irq;
logic [15:0] flags_out;
reg prg_bus_write;
reg [2:0] bank_select;
reg prg_rom_bank_mode;
reg chr_a12_invert;
reg mirroring;
reg irq_enable, irq_reload;
reg [7:0] irq_latch, counter;
reg [3:0] ram_enable, ram_protect;
reg ram6_enabled, ram6_enable, ram6_protect;
reg [7:0] chr_bank_0, chr_bank_1;
reg chr_bank_0_0, chr_bank_1_0;
reg [7:0] chr_bank_2, chr_bank_3, chr_bank_4, chr_bank_5;
reg [7:0] prg_bank_0, prg_bank_1, prg_bank_2;
reg last_a12;
logic prg_is_ram;
reg [6:0] irq_reg;
reg [7:0] m268_reg [5:0];
reg [7:0] m45_reg [3:0];
reg [2:0] m45_index;
logic m45_locked;
reg [7:0] m52_reg;
logic m52_locked;
logic mmc3_alt_behavior;
logic TQROM;
logic TxSROM;
logic mapper47;
logic mapper37;
logic DxROM;
logic mapper112;
logic mapper48;
logic mapper33;
logic mapper95;
logic mapper88;
logic mapper154;
logic mapper76;
logic mapper80;
logic mapper82;
logic mapper207;
logic mapper74;
logic mapper191;
logic mapper192;
logic mapper194;
logic mapper195;
logic mapper196;
logic mapper189;
logic mapper205;
logic mapper208;
logic MMC6;
logic acclaim;
logic mapper268;
logic mapper268_5k;
logic mapper45;
logic mapper52;
logic oversized;
logic gnrom;
logic lockout;
logic gnrom_lock;
logic mega_unrom;
logic weird_mode;
logic four_screen_mirroring;
reg mapper47_multicart;
reg [2:0] mapper37_multicart;
reg [3:0] mapper189_prgsel;
logic [7:0] new_counter;
reg [3:0] a12_ctr;
logic irq_support;
logic prg_invert_support;
logic chr_invert_support;
logic regs_7e;
logic internal_128;
logic prg_reg_odd;
logic [3:0] prota;
logic [3:0] prot;
logic no_prot;
reg [7:0] prgsel;
reg [8:0] chrsel;
logic use_chr_ain_12;
logic [24:13] map268p;
logic [17:10] map268c;
logic m268_chr_ram;
logic [5:0] m45_prg_and;
logic [7:0] m45_chr_and;
logic [7:0] m45_chr_or;
logic [7:0] m45_prg_final;
logic [7:0] m45_chr_final;
logic [4:0] m52_prg_and;
logic [7:0] m52_prg_or;
logic [7:0] m52_chr_and;
logic [9:0] m52_chr_or;
logic [7:0] m52_prg_final;
logic [9:0] m52_chr_final;
logic [21:0] prg_aout_tmp;
logic ram_enable_a;
logic ram_protect_a;
logic chr_ram_cs;
logic ram_a13_195;
logic ram_a13;
logic [21:0] prg_ram;
logic [63:0] SaveStateBus_wired_or[0:SAVESTATE_MODULES-1];
logic [63:0] SS_MAP1, SS_MAP2, SS_MAP3;
logic [63:0] SS_MAP1_BACK, SS_MAP2_BACK, SS_MAP3_BACK;
logic [63:0] SaveStateBus_Dout_active;


assign prg_aout_b   = enable ? prg_aout : 22'h0;
assign prg_dout_b   = enable ? prg_dout : 8'h0;
assign prg_allow_b  = enable ? prg_allow : 1'h0;
assign chr_aout_b   = enable ? chr_aout : 22'h0;
assign chr_allow_b  = enable ? chr_allow : 1'h0;
assign vram_a10_b   = enable ? vram_a10 : 1'h0;
assign vram_ce_b    = enable ? vram_ce : 1'h0;
assign irq_b        = enable ? irq : 1'h0;
assign flags_out_b  = enable ? flags_out : 16'h0;
assign audio_b      = enable ? {1'b0, audio_in[15:1]} : 16'h0;








assign flags_out =  {12'h0, 1'b1, 1'b0, prg_bus_write, 1'b0};


             // Register to write to next
             // Mode for PRG banking
                // Mode for CHR banking
                     // 0 = vertical, 1 = horizontal
        // IRQ enabled, and IRQ reload requested
      // IRQ latch value and current counter
       // RAM protection bits
 //extra bits for mmc6
  // Selected CHR banks
    // Keep extra bit

  // Selected PRG banks



assign irq = mapper48 ? irq_reg[3] & irq_enable : irq_reg[0];

  // Mapper 45 outer bank registers
      // Mapper 45 register index (0-3, wraps)
assign m45_locked =  m45_reg[3][6]; // Mapper 45 lock bit (D6, per Nintendulator)
        // Mapper 52 outer bank register
assign m52_locked =  m52_reg[7]; // Mapper 52 lock bit (D7)

// The alternative behavior has slightly different IRQ counter semantics.
assign mmc3_alt_behavior =  acclaim;

assign TQROM =      (flags[7:0] == 119); 	// TQROM maps 8kB CHR RAM
assign TxSROM =     (flags[7:0] == 118); 	// Connects CHR A17 to CIRAM A10
assign mapper47 =   (flags[7:0] == 47);		// Mapper 47 is a multicart that has 128k for each game. It has no RAM.
assign mapper37 =   (flags[7:0] == 37);    // European Triple Cart (Super Mario, Tetris, Nintendo World Cup)
assign DxROM =      (flags[7:0] == 206);
assign mapper112 =  (flags[7:0] == 112);   // Ntdec
assign mapper48 =   (flags[7:0] == 48);    // Taito's TC0690
assign mapper33 =   (flags[7:0] == 33);    // Taito's TC0190 (TC0690-like. No IRQ. Different Mirroring bit)
assign mapper95 =   (flags[7:0] == 95);    // NAMCOT-3425
assign mapper88 =   (flags[7:0] == 88);    // NAMCOT-3433
assign mapper154 =  (flags[7:0] == 154);   // NAMCOT-3453
assign mapper76 =   (flags[7:0] == 76);    // NAMCOT-3446
assign mapper80 =   (flags[7:0] == 80);    // Taito's X1-005
assign mapper82 =   (flags[7:0] == 82);    // Taito's X1-017
assign mapper207 =  (flags[7:0] == 207);   // Taito's X1-017
assign mapper74 =   (flags[7:0] == 74);    // Has 2KB CHR RAM
assign mapper191 =  (flags[7:0] == 191);   // Has 2KB CHR RAM
assign mapper192 =  (flags[7:0] == 192);   // Has 4KB CHR RAM
assign mapper194 =  (flags[7:0] == 194);   // Has 2KB CHR RAM
assign mapper195 =  (flags[7:0] == 195);   // Has 4KB CHR RAM
assign mapper196 =  (flags[7:0] == 196);   // PRG A0 line switcheroo
assign mapper189 =  (flags[7:0] == 189);		// Mapper 189 is a multicart
assign mapper205 =  (flags[7:0] == 205);		// Mapper 47 is a multicart similar to mapper 37
assign mapper208 =  (flags[7:0] == 208);		// Mapper 208 is a multicart similar to mapper 189 with copy protection. It has no RAM.
assign MMC6 =  ((flags[7:0] == 4) && (flags[24:21] == 1)); // mapper 4, submapper 1 = MMC6
assign acclaim =  ((flags[7:0] == 4) && (flags[24:21] == 3)); // Acclaim mapper
assign mapper268 =  ({flags[20:17],flags[7:0]} == 268); // Coolboy/Mindkids; Note: if mapper 268-256=12 was in this driver, it would need to check upper mapper bits
assign mapper268_5k =  (flags[24:21] == 1);
assign mapper45 =  (flags[7:0] == 45);    // Super 1000000-in-1 multicart
assign mapper52 =  (flags[7:0] == 52);    // Mario Party 7-in-1 multicart
assign oversized =  mapper268 || mapper45 || mapper52 || (flags[10:9] == 3); // If prg size in header is >= 1MB (prg_size==6 or 7) must be some way to access it. Allow oversize mmc3






assign four_screen_mirroring =  flags[16];// | DxROM; // not all DxROM are 4-screen



assign new_counter =  (counter == 0 || irq_reload) ? irq_latch : counter - 1'd1;

assign irq_support =  !DxROM && !mapper33 && !mapper95 && !mapper88 && !mapper154 && !mapper76
	&& !mapper80 && !mapper82 && !mapper207 && !mapper112; //82,207 not needed
assign prg_invert_support =  (irq_support && !mapper48);
assign chr_invert_support =  (irq_support && !mapper48) || mapper82;
assign regs_7e =  mapper80 || mapper82 || mapper207;
assign internal_128 =  mapper80 || mapper207;
assign prg_reg_odd =  (~mapper196) ? prg_ain[0] : ( |prg_ain[3:2] | (prg_ain[1] & ~prg_ain[14]) );
assign prota =  (m268_reg[4][6] ^ m268_reg[4][3]) ? {m268_reg[4][1:0],m268_reg[4][4],m268_reg[4][7]} : 4'hF; // m208 4'hF = 0x59
assign prot =  (m268_reg[4][3]) ? ~prota : prota;
assign no_prot =  mapper112;

always @(posedge clk) if (ce_master)
if (~enable) begin
	irq_reg <= 7'b0000000;
	bank_select <= 0;
	prg_rom_bank_mode <= 0;
	chr_a12_invert <= 0;
	mirroring <= flags[14];
	{irq_enable, irq_reload} <= 0;
	{irq_latch, counter} <= 0;
	ram_enable <= 0;
	ram_protect <= 0;
	{chr_bank_0, chr_bank_1} <= 0;
	{chr_bank_2, chr_bank_3, chr_bank_4, chr_bank_5} <= 0;
	{prg_bank_0, prg_bank_1} <= 0;
	prg_bank_2 <= 8'b11111110;
	a12_ctr <= 0;
	last_a12 <= 0;
	mapper37_multicart <= 3'b000;
	mapper189_prgsel <= 4'b1011; // mapper 208 requires 0xX011
	{m268_reg[0],m268_reg[1],m268_reg[2],m268_reg[3],m268_reg[4],m268_reg[5]} <= 0;
	{m45_reg[0],m45_reg[1],m45_reg[2],m45_reg[3]} <= {8'h00, 8'h00, 8'h0F, 8'h00}; // reg[2]=0x0F per NRS
	m45_index <= 0;
	m52_reg <= 0;
end else if (SaveStateBus_load) begin
	irq_reg            <= SS_MAP1[ 6: 0];
	bank_select        <= SS_MAP1[ 9: 7];
	prg_rom_bank_mode  <= SS_MAP1[   10];
	chr_a12_invert     <= SS_MAP1[   11];
	mirroring          <= SS_MAP1[   12];
	irq_enable         <= SS_MAP1[   13];
	irq_reload         <= SS_MAP1[   14];
	irq_latch          <= SS_MAP1[22:15];
	counter            <= SS_MAP1[30:23];
	ram_enable         <= SS_MAP1[34:31];
	ram_protect        <= SS_MAP1[38:35];
	chr_bank_0         <= SS_MAP1[46:39];
	chr_bank_1         <= SS_MAP1[54:47];
	chr_bank_2         <= SS_MAP1[62:55];
	chr_bank_3         <= SS_MAP2[ 7: 0];
	chr_bank_4         <= SS_MAP2[15: 8];
	chr_bank_5         <= SS_MAP2[23:16];
	prg_bank_0[5:0]    <= SS_MAP2[29:24];
	prg_bank_1[5:0]    <= SS_MAP2[35:30];
	prg_bank_2[5:0]    <= SS_MAP2[41:36];
	a12_ctr            <= SS_MAP2[45:42];
	last_a12           <= SS_MAP2[   46];
	mapper37_multicart <= SS_MAP2[49:47];
	mapper47_multicart <= SS_MAP2[   50];
	mapper189_prgsel   <= SS_MAP2[54:51];
	ram6_enabled       <= SS_MAP2[   55];
	ram6_enable        <= SS_MAP2[   56];
	ram6_protect       <= SS_MAP2[   57];
	prg_bank_0[7:6]    <= SS_MAP2[59:58];
	prg_bank_1[7:6]    <= SS_MAP2[61:60];
	prg_bank_2[7:6]    <= SS_MAP2[63:62];
	m268_reg[0]        <= SS_MAP3[ 7: 0];
	m268_reg[1]        <= SS_MAP3[15: 8];
	m268_reg[2]        <= SS_MAP3[23:16];
	m268_reg[3]        <= SS_MAP3[31:24];
	m268_reg[4]        <= SS_MAP3[39:32];
	m268_reg[5]        <= SS_MAP3[47:40];
	chr_bank_0_0       <= SS_MAP1[   48];
	chr_bank_1_0       <= SS_MAP1[   49];
end else begin
	if (ce) begin // M2
		if (!regs_7e && prg_write && prg_ain[15]) begin
			if (!mapper33 && !mapper48 && !mapper112) begin
				casez({prg_ain[14:13], prg_reg_odd})
					3'b00_0: {chr_a12_invert, prg_rom_bank_mode, ram6_enabled, bank_select} <= {prg_din[7:5], prg_din[2:0]}; // Bank select ($8000-$9FFE, even)
					3'b00_1: begin // Bank data ($8001-$9FFF, odd)
						case (bank_select)
							0: {chr_bank_0,chr_bank_0_0} <= {1'b0,prg_din};  // Select 2 KB CHR bank at PPU $0000-$07FF (or $1000-$17FF);
							1: {chr_bank_1,chr_bank_1_0} <= {1'b0,prg_din};  // Select 2 KB CHR bank at PPU $0800-$0FFF (or $1800-$1FFF);
							2: chr_bank_2 <= prg_din;       // Select 1 KB CHR bank at PPU $1000-$13FF (or $0000-$03FF);
							3: chr_bank_3 <= prg_din;       // Select 1 KB CHR bank at PPU $1400-$17FF (or $0400-$07FF);
							4: chr_bank_4 <= prg_din;       // Select 1 KB CHR bank at PPU $1800-$1BFF (or $0800-$0BFF);
							5: chr_bank_5 <= prg_din;       // Select 1 KB CHR bank at PPU $1C00-$1FFF (or $0C00-$0FFF);
							6: prg_bank_0 <= prg_din;       // Select 8 KB PRG ROM bank at $8000-$9FFF (or $C000-$DFFF);
							7: prg_bank_1 <= prg_din;       // Select 8 KB PRG ROM bank at $A000-$BFFF
						endcase
					end
					3'b01_0: if (!mapper208) mirroring <= !prg_din[0];  // Mirroring ($A000-$BFFE, even)
					3'b01_1: {ram_enable, ram_protect, ram6_enable, ram6_protect} <= {{4{prg_din[7]}},{4{prg_din[6]}}, prg_din[5:4]}; // PRG RAM protect ($A001-$BFFF, odd)
					3'b10_0: irq_latch <= prg_din;                      // IRQ latch ($C000-$DFFE, even)
					3'b10_1: irq_reload <= 1;                           // IRQ reload ($C001-$DFFF, odd)
					3'b11_0: {irq_enable, irq_reg[0]} <= 2'b00;         // IRQ disable ($E000-$FFFE, even)
					3'b11_1: irq_enable <= 1;                           // IRQ enable ($E001-$FFFF, odd)
				endcase
			end else if (!mapper112) begin
				casez({prg_ain[14:13], prg_ain[1:0], mapper48})
					5'b00_00_0: {mirroring, prg_bank_0[5:0]} <= prg_din[6:0] ^ 7'h40; // Select 8 KB PRG ROM bank at $8000-$9FFF
					5'b00_00_1: prg_bank_0[5:0] <= prg_din[5:0];  // Select 8 KB PRG ROM bank at $8000-$9FFF
					5'b00_01_?: prg_bank_1[5:0] <= prg_din[5:0];  // Select 8 KB PRG ROM bank at $A000-$BFFF
					5'b00_10_?: chr_bank_0 <= prg_din;  // Select 2 KB CHR bank at PPU $0000-$07FF
					5'b00_11_?: chr_bank_1 <= prg_din;  // Select 2 KB CHR bank at PPU $0800-$0FFF
					5'b01_00_?: chr_bank_2 <= prg_din;  // Select 1 KB CHR bank at PPU $1000-$13FF
					5'b01_01_?: chr_bank_3 <= prg_din;  // Select 1 KB CHR bank at PPU $1800-$1BFF
					5'b01_10_?: chr_bank_4 <= prg_din;  // Select 1 KB CHR bank at PPU $1800-$1BFF
					5'b01_11_?: chr_bank_5 <= prg_din;  // Select 1 KB CHR bank at PPU $1C00-$1FFF

					5'b10_00_1: irq_latch <= prg_din ^ 8'hFF;              // IRQ latch ($C000-$DFFC)
					5'b10_01_1: {irq_reload, irq_reg} <= 8'b10000000;      // IRQ reload ($C001-$DFFD)
					5'b10_10_1: irq_enable <= 1;                           // IRQ enable ($C002-$DFFE)
					5'b10_11_1: irq_enable <= 0;                           // IRQ disable ($C003-$DFFF)

					5'b11_00_1: mirroring <= !prg_din[6];  // Mirroring
				endcase
			end else begin
				casez({prg_ain[14:13], prg_ain[0]})
					3'b00_0: {bank_select} <= {prg_din[2:0]}; // Bank select ($8000-$9FFE)

					3'b01_0: begin // Bank data ($A000-$BFFF)
						case (bank_select)
						0: prg_bank_0 <= prg_din;       // Select 8 KB PRG ROM bank at $8000-$9FFF (or $C000-$DFFF);
						1: prg_bank_1 <= prg_din;       // Select 8 KB PRG ROM bank at $A000-$BFFF
						2: chr_bank_0 <= {1'b0,prg_din[7:1]};  // Select 2 KB CHR bank at PPU $0000-$07FF (or $1000-$17FF);
						3: chr_bank_1 <= {1'b0,prg_din[7:1]};  // Select 2 KB CHR bank at PPU $0800-$0FFF (or $1800-$1FFF);
						4: chr_bank_2 <= prg_din;       // Select 1 KB CHR bank at PPU $1000-$13FF (or $0000-$03FF);
						5: chr_bank_3 <= prg_din;       // Select 1 KB CHR bank at PPU $1400-$17FF (or $0400-$07FF);
						6: chr_bank_4 <= prg_din;       // Select 1 KB CHR bank at PPU $1800-$1BFF (or $0800-$0BFF);
						7: chr_bank_5 <= prg_din;       // Select 1 KB CHR bank at PPU $1C00-$1FFF (or $0C00-$0FFF);
						endcase
					end

					3'b11_0: mirroring <= !prg_din[0];  // Mirroring ($E000-$FFFE)
				endcase
			end

			if (mapper154)
				mirroring <= !prg_din[6];
			if (DxROM || mapper76 || mapper88)
				mirroring <= flags[14]; // Hard-wired mirroring
		end
		else if (regs_7e && prg_write && prg_ain[15:4]==12'h7EF) begin
			casez({prg_ain[3:0], mapper82})
				5'b0000_?: chr_bank_0 <= {1'b0,prg_din[7:1]};  // Select 2 KB CHR bank at PPU $0000-$07FF
				5'b0001_?: chr_bank_1 <= {1'b0,prg_din[7:1]};  // Select 2 KB CHR bank at PPU $0800-$0FFF
				5'b0010_?: chr_bank_2 <= prg_din;  // Select 1 KB CHR bank at PPU $1000-$13FF
				5'b0011_?: chr_bank_3 <= prg_din;  // Select 1 KB CHR bank at PPU $1800-$1BFF
				5'b0100_?: chr_bank_4 <= prg_din;  // Select 1 KB CHR bank at PPU $1800-$1BFF
				5'b0101_?: chr_bank_5 <= prg_din;  // Select 1 KB CHR bank at PPU $1C00-$1FFF
				5'b011?_0: {mirroring} <= prg_din[0]; // Select Mirroing
				5'b100?_0: {ram_enable[3], ram_protect[3]} <= {(prg_din==8'hA3),!(prg_din==8'hA3)};  // Enable RAM at $7F00-$7FFF
				5'b0110_1: {chr_a12_invert,mirroring} <= prg_din[1:0]; // Select Mirroing
				5'b0111_1: {ram_enable[0], ram_protect[0]} <= {(prg_din==8'hCA),!(prg_din==8'hCA)};  // Enable RAM at $6000-$67FF
				5'b1000_1: {ram_enable[1], ram_protect[1]} <= {(prg_din==8'h69),!(prg_din==8'h69)};  // Enable RAM at $6F00-$6FFF
				5'b1001_1: {ram_enable[2], ram_protect[2]} <= {(prg_din==8'h84),!(prg_din==8'h84)};  // Enable RAM at $7000-$73FF  //Using 6K; Require 5K instead?
				5'b101?_0: prg_bank_0[5:0] <= prg_din[5:0];  // Select 8 KB PRG ROM bank at $8000-$9FFF
				5'b110?_0: prg_bank_1[5:0] <= prg_din[5:0];  // Select 8 KB PRG ROM bank at $A000-$BFFF
				5'b111?_0: prg_bank_2[5:0] <= prg_din[5:0];  // Select 8 KB PRG ROM bank at $C000-$DFFF
				5'b1010_1: prg_bank_0[5:0] <= prg_din[7:2];  // Select 8 KB PRG ROM bank at $8000-$9FFF
				5'b1011_1: prg_bank_1[5:0] <= prg_din[7:2];  // Select 8 KB PRG ROM bank at $A000-$BFFF
				5'b1100_1: prg_bank_2[5:0] <= prg_din[7:2];  // Select 8 KB PRG ROM bank at $C000-$DFFF
			endcase
		end

		if (mapper268 && prg_write && ({mapper268_5k,prg_ain[15:12]}==5'h6 || {mapper268_5k,prg_ain[15:12]}==5'h15)) begin
			if (prg_ain[2:0]==3'h2) begin
				m268_reg[2][3:0] <= prg_din[3:0];
				if (!gnrom_lock)
					m268_reg[2][7:4] <= prg_din[7:4];
			end else if ((prg_ain[2:1]!=2'b11) && !lockout) begin
				m268_reg[prg_ain[2:0]] <= prg_din;
			end
		end

		// For Mapper 47
		// $6000-7FFF:  [.... ...B]  Block select
		if (prg_write && prg_is_ram)
			mapper47_multicart <= prg_din[0];

		// For Mapper 37 and 205
		// $6000-7FFF:  [.... .QBB]  Block select
		if (prg_write && prg_is_ram)
			mapper37_multicart <= prg_din[2:0];

		// Mapper 189
		// $4120-7FFF:  [AAAA BBBB] A,B:  PRG Reg
		if (prg_write && prg_ain[15:14] == 2'b01 && prg_ain[8] && mapper189)
			mapper189_prgsel <= (prg_din[7:4] | prg_din[3:0]); // Select 32 KB PRG ROM bank at $8000-$FFFF

		// Mapper 208
		// $4800-4FFF or 6800-6FFF:  [..mP ...P] :  mirror PRG Reg
		if (prg_write && prg_ain[15:14] == 2'b01 && !prg_ain[12] && prg_ain[11] && mapper208)
			{mirroring, mapper189_prgsel[1:0]} <= {!prg_din[5], prg_din[4], prg_din[0]}; // Select 32 KB PRG ROM bank at $8000-$FFFF

		// Mapper 208
		// $5000-57FF: Prot index
		if (prg_write && prg_ain[15:11] == 5'b01010 && mapper208)
			m268_reg[4] <= prg_din;

		// Mapper 208
		// $5800-5FFF: Prot index
		if (prg_write && prg_ain[15:11] == 5'b01011 && mapper208)
			m268_reg[{1'b0,prg_ain[1:0]}] <= prg_din ^ {1'b0,prot[3],1'b0,prot[2:1],2'b00,prot[0]};

		// Mapper 45: All writes to $6000-$7FFF update outer bank registers sequentially
		// Per Nintendulator: no even/odd distinction, just write to next register
		if (prg_write && prg_ain[15:13] == 3'b011 && mapper45 && !m45_locked) begin
			m45_reg[m45_index[1:0]] <= prg_din;
			m45_index <= m45_index + 1'd1;
		end

		// Mapper 52: Write to $6000-$7FFF sets outer bank register (once until reset)
		// D[7]=lock, D[6]=CHR mode, D[5:4]=CHR bank, D[3]=PRG mode, D[2:0]=PRG bank
		if (prg_write && prg_ain[15:13] == 3'b011 && mapper52 && !m52_locked) begin
			m52_reg <= prg_din;
		end
	end

	if (m2_inv) begin // Inverted M2
		irq_reg[6:1] <= irq_reg[5:0]; // 4 cpu cycle delay for mapper 48

		if (!acclaim) begin // nintendo mapper 'cools down' for 3 inverted M2 cycles
			a12_ctr <= (a12_ctr != 0) ? a12_ctr - 4'b0001 : a12_ctr;
		end
	end

	if (~paused) begin
		// Trigger IRQ counter on rising edge of chr_ain[12]
		// All MMC3A's and non-Sharp MMC3B's will generate only a single IRQ when $C000 is $00.
		// This is because this version of the MMC3 generates IRQs when the scanline counter is decremented to 0.
		// In addition, writing to $C001 with $C000 still at $00 will result in another single IRQ being generated.
		// In the community, this is known as the "alternate" or "old" behavior.
		// All MMC3C's and Sharp MMC3B's will generate an IRQ on each scanline while $C000 is $00.
		// This is because this version of the MMC3 generates IRQs when the scanline counter is equal to 0.
		// In the community, this is known as the "normal" or "new" behavior.

		last_a12 <= chr_ain_o[12];
		if ((acclaim && (!last_a12 && chr_ain_o[12]) && (a12_ctr == 6)) ||
			(~acclaim && (!last_a12 && chr_ain_o[12]) && (a12_ctr == 0))) begin
			counter <= new_counter;

			// MMC Scanline
			if ( (!mmc3_alt_behavior || counter != 0 || irq_reload) && new_counter == 0 && irq_enable && irq_support) begin
				irq_reg[0] <= 1;
			end
			irq_reload <= 0;
		end

		if (acclaim) begin
			if (!last_a12 && chr_ain_o[12]) // acclaim mapper counts down 8 pulses, or 16 edges total
				a12_ctr <= (a12_ctr != 0) ? a12_ctr - 4'b0001 : 4'b0111;
			if (prg_ain == 16'hC001 && prg_write) a12_ctr <= 4'b0111;
		end else if (chr_ain_o[12]) // Nintendo mapper cooldown resets on a12
			a12_ctr <= 4'b0011;
	end
end

assign SS_MAP1_BACK[ 6: 0] = irq_reg;
assign SS_MAP1_BACK[ 9: 7] = bank_select;
assign SS_MAP1_BACK[   10] = prg_rom_bank_mode;
assign SS_MAP1_BACK[   11] = chr_a12_invert;
assign SS_MAP1_BACK[   12] = mirroring;
assign SS_MAP1_BACK[   13] = irq_enable;
assign SS_MAP1_BACK[   14] = irq_reload;
assign SS_MAP1_BACK[22:15] = irq_latch;
assign SS_MAP1_BACK[30:23] = counter;
assign SS_MAP1_BACK[34:31] = ram_enable;
assign SS_MAP1_BACK[38:35] = ram_protect;
assign SS_MAP1_BACK[46:39] = chr_bank_0;
assign SS_MAP1_BACK[54:47] = chr_bank_1;
assign SS_MAP1_BACK[62:55] = chr_bank_2;
assign SS_MAP1_BACK[   63] = 1'b0; // free to be used

assign SS_MAP2_BACK[ 7: 0] = chr_bank_3;
assign SS_MAP2_BACK[15: 8] = chr_bank_4;
assign SS_MAP2_BACK[23:16] = chr_bank_5;
assign SS_MAP2_BACK[29:24] = prg_bank_0[5:0];
assign SS_MAP2_BACK[35:30] = prg_bank_1[5:0];
assign SS_MAP2_BACK[41:36] = prg_bank_2[5:0];
assign SS_MAP2_BACK[45:42] = a12_ctr;
assign SS_MAP2_BACK[   46] = last_a12;
assign SS_MAP2_BACK[49:47] = mapper37_multicart;
assign SS_MAP2_BACK[   50] = mapper47_multicart;
assign SS_MAP2_BACK[54:51] = mapper189_prgsel;
assign SS_MAP2_BACK[   55] = ram6_enabled;
assign SS_MAP2_BACK[   56] = ram6_enable;
assign SS_MAP2_BACK[   57] = ram6_protect;
assign SS_MAP2_BACK[59:58] = prg_bank_0[7:6];
assign SS_MAP2_BACK[61:60] = prg_bank_1[7:6];
assign SS_MAP2_BACK[63:62] = prg_bank_2[7:6];
//assign SS_MAP2_BACK[64:64] = 0'b0; // full

assign SS_MAP3_BACK[ 7: 0] = m268_reg[0];
assign SS_MAP3_BACK[15: 8] = m268_reg[1];
assign SS_MAP3_BACK[23:16] = m268_reg[2];
assign SS_MAP3_BACK[31:24] = m268_reg[3];
assign SS_MAP3_BACK[39:32] = m268_reg[4];
assign SS_MAP3_BACK[47:40] = m268_reg[5];
assign SS_MAP3_BACK[   48] = chr_bank_0_0;
assign SS_MAP3_BACK[   49] = chr_bank_1_0;
assign SS_MAP3_BACK[63:50] = 14'b0; // free to be used

// The PRG bank to load. Each increment here is 8kb. So valid values are 0..63.

always @* begin
	casez({prg_ain[14:13], prg_rom_bank_mode && prg_invert_support})
		3'b00_0: prgsel = prg_bank_0;  // $8000 mode 0
		3'b00_1: prgsel = prg_bank_2;  // $8000 fixed to second last bank
		3'b01_?: prgsel = prg_bank_1;  // $A000 mode 0,1
		3'b10_0: prgsel = prg_bank_2;  // $C000 fixed to second last bank
		3'b10_1: prgsel = prg_bank_0;  // $C000 mode 1
		3'b11_?: prgsel = 8'b11111111; // $E000 fixed to last bank
	endcase

	// mapper47 is limited to 128k PRG, the top bits are controlled by mapper47_multicart instead.
	if (mapper47) prgsel[7:4] = {3'b000, mapper47_multicart};
	if (mapper37) begin
	prgsel[7:4] = {3'b000, mapper37_multicart[2]};
		if (mapper37_multicart[1:0] == 3'd3)
			prgsel[3] = 1'b1;
		else if (mapper37_multicart[2] == 1'b0)
			prgsel[3] = 1'b0;
	end
	if (mapper205) prgsel[7:4] = {2'b00, mapper37_multicart[1], mapper37_multicart[0] | (prgsel[4] & !mapper37_multicart[1])};

	if (mapper189 || mapper208) prgsel = {2'b00,mapper189_prgsel,prg_ain[14:13]};
	if (!oversized) prgsel[7:6] = 2'b00;
end

// The CHR bank to load. Each increment here is 1kb. So valid values are 0..255.

assign use_chr_ain_12 =  chr_ain[12] ^ (chr_a12_invert && chr_invert_support);
always @* begin
	if (!mapper76) begin
		casez({use_chr_ain_12, chr_ain[11], chr_ain[10]})
			3'b00?: chrsel = {chr_bank_0, chr_ain[10]};
			3'b01?: chrsel = {chr_bank_1, chr_ain[10]};
			3'b100: chrsel = {1'b0, chr_bank_2};
			3'b101: chrsel = {1'b0, chr_bank_3};
			3'b110: chrsel = {1'b0, chr_bank_4};
			3'b111: chrsel = {1'b0, chr_bank_5};
		endcase
		// mapper47 is limited to 128k CHR, the top bit is controlled by mapper47_multicart instead.
		if (mapper47) chrsel[7] = mapper47_multicart;
		if (mapper37) chrsel[7] = mapper37_multicart[2];
		if (mapper205) chrsel[8:7] = {mapper37_multicart[1], mapper37_multicart[0] | (chrsel[7] & !mapper37_multicart[1])};
		if ((mapper88) || (mapper154)) chrsel[6] = chr_ain[12];
	end else begin
		case(chr_ain[12:11])
			2'b00: chrsel = {chr_bank_2, chr_ain[10]};
			2'b01: chrsel = {chr_bank_3, chr_ain[10]};
			2'b10: chrsel = {chr_bank_4, chr_ain[10]};
			2'b11: chrsel = {chr_bank_5, chr_ain[10]};
		endcase
	end
end

// Read from mapper
always @* begin
	prg_bus_write = 1'b1;
	if (!prg_write && mapper208 && prg_ain[15:11] == 5'b01011) begin // 5800
		prg_dout = m268_reg[{1'b0,prg_ain[1:0]}]; // Warning 10027 suppressed: index is correctly constrained to 0-3
	end else begin
		prg_dout = 8'hFF; // By default open bus.
		prg_bus_write = 0;
	end
end

assign gnrom = m268_reg[3][4];
assign lockout = m268_reg[3][7] && !gnrom;
assign gnrom_lock = m268_reg[2][7];
assign mega_unrom = m268_reg[5][4];
assign weird_mode = m268_reg[3][6];
 //only [20:13] used below => max size is 2MB

assign map268p[24:21] = {m268_reg[0][5:4],m268_reg[1][3:2]};
assign map268p[20] = m268_reg[1][5]?prgsel[7]:m268_reg[1][4];
assign map268p[19] = mega_unrom ? (prg_ain[14] | map268c[17]): m268_reg[1][6]?prgsel[6]:m268_reg[0][2];
assign map268p[18] = mega_unrom ? (prg_ain[14] | map268c[16]): !m268_reg[1][7]?prgsel[5]:m268_reg[0][1];
assign map268p[17] = !m268_reg[0][6]?prgsel[4]:m268_reg[0][0];
assign map268p[16:15] = gnrom ? m268_reg[3][3:2] : weird_mode && !prg_rom_bank_mode && prg_ain[14]?2'b00:prgsel[3:2];
assign map268p[14] = gnrom ? m268_reg[1][1]?prg_ain[14]:m268_reg[3][1] : weird_mode && !prg_rom_bank_mode && prg_ain[14]?1'b0:prgsel[1];
assign map268p[13] = gnrom ? prg_ain[13]: weird_mode && !prg_rom_bank_mode && prg_ain[14]?1'b0:prgsel[0];

assign map268c[17] = !m268_reg[0][7]?chrsel[7]:m268_reg[0][3];
assign map268c[16:13] = gnrom ? ({~m268_reg[2][6:4],1'b1} & m268_reg[2][3:0]): weird_mode && chr_ain[10] ? 4'h0 : chrsel[6:3];
assign map268c[12:11] = (weird_mode && chr_ain[10]) ? 2'h0 : chrsel[2:1];
assign map268c[10] = (weird_mode && chr_ain[10]) ? 1'b0 : (use_chr_ain_12 || !weird_mode) ? chrsel[0] : chr_ain[12] ? chr_bank_1_0 : chr_bank_0_0 ;

assign m268_chr_ram =  {map268c[17:11],1'b1} == m268_reg[4];

// Mapper 45 outer bank address calculation
// CHR: (chrsel & CHR_AND) | CHR_OR, plus high bits from reg[2]
assign m45_prg_and =  ~m45_reg[3][5:0];  // PRG-AND mask (inverted in register)
assign m45_chr_and =  8'hFF >> (4'hF - m45_reg[2][3:0]); // CHR-AND mask
assign m45_chr_or =  m45_reg[0]; // CHR A10-A17

// PRG final: upper bits from reg1[7:6], lower bits masked/OR'd
assign m45_prg_final =  {m45_reg[1][7:6], (prgsel[5:0] & m45_prg_and) | m45_reg[1][5:0]};
// CHR final: (chrsel & AND) | OR
assign m45_chr_final =  (chrsel[7:0] & m45_chr_and) | m45_chr_or;

// Mapper 52 outer bank address calculation (per Nintendulator)
// PRG: bit3=0: 256KB outer (5-bit mask), bit3=1: 128KB outer (4-bit mask)
// CHR: bit6=0: 256KB outer (8-bit mask), bit6=1: 128KB outer (7-bit mask)
// PRGbank << 4: bits go to positions [6:4], CHRbank << 7: bits go to positions [9:7]
assign m52_prg_and =  m52_reg[3] ? 5'b01111 : 5'b11111;
// PRG OR: bit3=1: {bits 2:0} << 4 = positions 6:4; bit3=0: {bits 2:1, 0} << 4 = positions 6:5
assign m52_prg_or =  m52_reg[3] ? {1'b0, m52_reg[2:0], 4'b0000} : {1'b0, m52_reg[2:1], 5'b00000};
assign m52_chr_and =  m52_reg[6] ? 8'b01111111 : 8'b11111111;
// CHR OR: {bit5, bit2, [bit4]} << 7 = positions 9:7 (bit4 only when bit6=1)
assign m52_chr_or =  m52_reg[6] ? {m52_reg[5], m52_reg[2], m52_reg[4], 7'b0000000} : {m52_reg[5], m52_reg[2], 8'b00000000};

assign m52_prg_final =  (prgsel[4:0] & m52_prg_and) | m52_prg_or;
assign m52_chr_final =  (chrsel[7:0] & m52_chr_and) | m52_chr_or;

assign prg_aout_tmp =  {1'b0, mapper268 ? map268p[20:13] : mapper45 ? m45_prg_final : mapper52 ? m52_prg_final : prgsel, prg_ain[12:0]};

assign ram_enable_a =  !MMC6 ? (ram_enable[prg_ain[12:11]])
						:   (ram6_enabled && ram6_enable && prg_ain[12] == 1'b1 && prg_ain[9] == 1'b0)
						 || (ram6_enabled && ram_enable[3] && prg_ain[12] == 1'b1 && prg_ain[9] == 1'b1);

assign ram_protect_a =  !MMC6 ? (ram_protect[prg_ain[12:11]])
						:   !(ram6_enabled && ram6_enable && ram6_protect && prg_ain[12] == 1'b1 && prg_ain[9] == 1'b0)
						 && !(ram6_enabled && ram_enable[3] && ram_protect[3] && prg_ain[12] == 1'b1 && prg_ain[9] == 1'b1);

assign chr_ram_cs =
		TQROM                 ? chrsel[6]               :
		mapper74              ? chrsel[7:1]==7'b0000100 :
		mapper191             ? chrsel[7]               :
		mapper192             ? chrsel[7:2]==6'b000010  :
		mapper194             ? chrsel[7:1]==7'b0000000 :
		mapper195             ? chrsel[7:2]==6'b000000  :
		flags[15];

assign chr_allow = chr_ram_cs | (four_screen_mirroring & chr_ain[13]);
assign chr_aout =
		(four_screen_mirroring & chr_ain[13])? {10'b11_1111_1100,              chr_ain[11:0]} :   // DxROM/TVROM 4kb NT RAM
		(TQROM & chr_ram_cs)                 ? {9'b11_1111_111,    chrsel[2:0], chr_ain[9:0]} :   // TQROM 8kb CHR-RAM
		(mapper74 & chr_ram_cs)              ? {11'b11_1111_1111_1,chrsel[0],   chr_ain[9:0]} :   // 2kb CHR-RAM
		(mapper191 & chr_ram_cs)             ? {11'b11_1111_1111_1,chrsel[0],   chr_ain[9:0]} :   // 2kb CHR-RAM
		(mapper192 & chr_ram_cs)             ? {10'b11_1111_1111,  chrsel[1:0], chr_ain[9:0]} :   // 4kb CHR-RAM
		(mapper194 & chr_ram_cs)             ? {11'b11_1111_1111_1,chrsel[0],   chr_ain[9:0]} :   // 2kb CHR-RAM
		(mapper195 & chr_ram_cs)             ? {10'b11_1111_1111,  chrsel[1:0], chr_ain[9:0]} :   // 4kb CHR-RAM
		(m268_chr_ram)                       ? {11'b11_1111_1111_1,            chr_ain[10:0]} :   // 2kb CHR-RAM
		(mapper268)                          ? {4'b10_00,              map268c, chr_ain[9:0]} :   // Mapper 268 override
		(mapper45 & flags[15])               ? {9'b11_1111_111, chr_ain[12:0]} : // Mapper 45 unbanked 8KB CHR-RAM
		(mapper45)                           ? {2'b10, m45_reg[2][5:4], m45_chr_final, chr_ain[9:0]} : // Mapper 45 CHR-ROM
		(mapper52)                           ? {2'b10,         m52_chr_final, chr_ain[9:0]} : // Mapper 52 CHR override
		                                       {3'b10_0,                chrsel, chr_ain[9:0]};    // Standard MMC3 CHR-ROM/RAM

// Waixing FS303 (mapper 195) has 4KB of PRG-RAM at $5000-$5FFF, always
// enabled and writable regardless of the MMC3 WRAM enable/protect bits.
// Waixing originals/translations (e.g. Kyattou Ninden Teyandee (Ch))
// use it as work RAM and freeze without it.
assign ram_a13_195 =  mapper195 && (prg_ain[15:12] == 4'h5);
assign ram_a13 =  (mapper268 && m268_reg[3][5] && (prg_ain[15:12] == 4'h5)) || ram_a13_195;
assign prg_is_ram = ram_a13_195
					|| ((ram_a13 || (prg_ain[15:13] == 3'b011) && ((prg_ain[12:8] == 5'b1_1111) | ~internal_128)) //(>= 'h6000 && < 'h8000) && (==7Fxx or external_ram)
					&& ((ram_enable_a && !(ram_protect_a && prg_write)) || no_prot)); // better slacks if set ram_enable and ram_protect registers for no_prot?
assign prg_allow = prg_ain[15] && !prg_write || (prg_is_ram && !mapper47 && !mapper208);
assign prg_ram =  {8'b11_1100_00, ram_a13, internal_128 ? 6'b000000 : MMC6 ? {3'b000, prg_ain[9:7]} : prg_ain[12:7], prg_ain[6:0]};
assign prg_aout = prg_is_ram  && !mapper47 && !mapper208 && !DxROM && !mapper95 && !mapper88 ? prg_ram : prg_aout_tmp;
assign vram_a10 = TxSROM ? chrsel[7] :              // TxSROM do not support mirroring
					mapper95 ? chrsel[5] :          // mapper95 does not support mirroring
					mapper154 ? mirroring :         // mapper154 does not support mirroring
					mapper207 ? chrsel[7] :         // mapper207 does not support mirroring
					(mirroring ? chr_ain[10] : chr_ain[11]);
assign vram_ce = chr_ain[13] && !four_screen_mirroring;

// savestate




assign SaveStateBus_Dout_active =  SaveStateBus_wired_or[0] | SaveStateBus_wired_or[1] | SaveStateBus_wired_or[2];

eReg_SavestateV #(SSREG_INDEX_MAP1, 64'h0000000000000000) iREG_SAVESTATE_MAP1 (clk, SaveStateBus_Din, SaveStateBus_Adr, SaveStateBus_wren, SaveStateBus_rst, SaveStateBus_wired_or[0], SS_MAP1_BACK, SS_MAP1);
eReg_SavestateV #(SSREG_INDEX_MAP2, 64'h0000000000000000) iREG_SAVESTATE_MAP2 (clk, SaveStateBus_Din, SaveStateBus_Adr, SaveStateBus_wren, SaveStateBus_rst, SaveStateBus_wired_or[1], SS_MAP2_BACK, SS_MAP2);
eReg_SavestateV #(SSREG_INDEX_MAP3, 64'h0000000000000000) iREG_SAVESTATE_MAP3 (clk, SaveStateBus_Din, SaveStateBus_Adr, SaveStateBus_wren, SaveStateBus_rst, SaveStateBus_wired_or[2], SS_MAP3_BACK, SS_MAP3);

assign SaveStateBus_Dout = enable ? SaveStateBus_Dout_active : 64'h0000000000000000;

endmodule

