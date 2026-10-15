import regs_savestates::*;
// Derived from pinned MiSTer NES. See third_party/nes/README.md.
module Mapper28(
    input ce_master,
	input        clk,         // System clock
	input        ce,          // M2 ~cpu_clk
	input        enable,      // Mapper enabled
	input [63:0] flags,       // Cart flags
	input [15:0] prg_ain,     // prg address
	output logic [21:0] prg_aout_b,  // prg address out
	input        prg_read,    // prg read
	input        prg_write,   // prg write
	input  [7:0] prg_din,     // prg data in
	output logic [7:0] prg_dout_b,  // prg data out
	output logic prg_allow_b, // Enable access to memory for the specified operation.
	input [13:0] chr_ain,     // chr address in
	output logic [21:0] chr_aout_b,  // chr address out
	output logic [7:0] chr_dout_b,  // chr data (non standard)
	input        chr_read,    // chr ram read
	output logic chr_allow_b, // chr allow write
	output logic vram_a10_b,  // Value for A10 address line
	output logic vram_ce_b,   // True if the address should be routed to the internal 2kB VRAM.
	output logic irq_b,       // IRQ
	input [15:0] audio_in,    // Inverted audio from APU
	output logic [15:0] audio_b,     // Mixed audio output
	output logic [15:0] flags_out_b, // flags {0, 0, 0, 0, has_savestate, prg_conflict, prg_bus_write, has_chr_dout}
	// savestates
	input       [63:0]  SaveStateBus_Din,
	input       [ 9:0]  SaveStateBus_Adr,
	input               SaveStateBus_wren,
	input               SaveStateBus_rst,
	input               SaveStateBus_load,
	output logic [63:0]  SaveStateBus_Dout
);
logic [21:0] prg_aout, chr_aout;
logic prg_allow;
logic chr_allow;
reg vram_a10;
reg [7:0] chr_dout;
logic vram_ce;
logic [15:0] flags_out;
logic prg_conflict, has_chr_dout;
reg [6:0] a53prg;
reg [1:0] a53chr;
reg [3:0] inner;
reg [5:0] mode;
reg [5:0] outer;
reg [2:0] selreg;
reg [3:0] security;
logic [7:0] mapper;
logic allow_select;
logic extend_bit;
logic prg_is_ram;
logic [21:0] prg_aout_tmp;
logic [21:0] prg_ram;
logic [4:0] submapper;
logic [63:0] SS_MAP1;
logic [63:0] SS_MAP1_BACK;
logic [63:0] SaveStateBus_Dout_active;


assign prg_aout_b   = enable ? prg_aout : 22'h0;
assign prg_dout_b   = enable ? 8'hFF : 8'h0;
assign prg_allow_b  = enable ? prg_allow : 1'h0;
assign chr_aout_b   = enable ? chr_aout : 22'h0;
assign chr_dout_b   = enable ? chr_dout : 8'h0;
assign chr_allow_b  = enable ? chr_allow : 1'h0;
assign vram_a10_b   = enable ? vram_a10 : 1'h0;
assign vram_ce_b    = enable ? vram_ce : 1'h0;
assign irq_b        = enable ? 1'b0 : 1'h0;
assign flags_out_b  = enable ? flags_out : 16'h0;
assign audio_b      = enable ? {1'b0, audio_in[15:1]} : 16'h0;







assign flags_out =  {12'h0, 1'b1, prg_conflict, 1'b0, has_chr_dout};



    // output logic PRG ROM (A14-A20 on ROM)
    // output logic CHR RAM (A13-A14 on RAM)

    // "inner" bank at 01h
     // mode register at 80h
    // "outer" bank at 81h
   // selector register
 // selector register

// Allow writes to 0x5000 only when launching through the proper mapper ID.
assign mapper =  flags[7:0];
assign allow_select =  (mapper == 8'd28);
assign extend_bit =  (mapper == 97);

always @(posedge clk) if (ce_master)
if (~enable) begin
	mode[5:2] <= 0;         // NROM mode, 32K mode
	outer[5:0] <= 6'h3f;    // last bank
	inner <= 0;
	selreg <= 1;

	// Set value for mirroring
	if (mapper == 2 || mapper == 0 || mapper == 3 || mapper == 94 || mapper == 180 || mapper == 185)
		mode[1:0] <= flags[14] ? 2'b10 : 2'b11;

	// UNROM #2 - Current bank in $8000-$BFFF and fixed top half of outer bank in $C000-$FFFF
	if (mapper == 2) begin
		mode[5:2] <= 4'b1111; // 256K banks, UNROM mode
	end

	// CNROM #3 - Fixed PRG bank, switchable CHR bank.
	if (mapper == 3)
		selreg <= 0;

	// CNROM #185 - Fixed PRG bank, Fixed CHR Bank, Security.
	if (mapper == 185)
		selreg <= 4;

	// UNROM #180 - Fixed first PRG bank, Fixed CHR Bank.
	if (mapper == 180) begin
		selreg <= 1;
		mode[5:2] <= 4'b1110;
		outer[5:0] <= 6'h00;
	end

	// IREM TAM-S1 IC #97 - Fixed first PRG bank, Fixed CHR Bank, Mirroring.
	if (mapper == 97) begin
		selreg <= 6;
		mode[5:2] <= 4'b1110;
		outer[5:0] <= 6'h3F;
	end

	// IREM TAM-S1 IC #94 - Fixed last PRG bank, Fixed CHR Bank
	if (mapper == 94) begin
		selreg <= 7;
		mode[5:2] <= 4'b1111;
	end

	// AxROM #7 - Switch 32kb rom bank + switchable nametables
	if (mapper == 7) begin
		mode[1:0] <= 2'b00;   // Switchable VRAM page.
		mode[5:2] <= 4'b1100; // 256K banks, (B)NROM mode
		outer[5:0] <= 6'h00;
	end
end else if (SaveStateBus_load) begin
	inner    <= SS_MAP1[ 3: 0];
	mode     <= SS_MAP1[ 9: 4];
	outer    <= SS_MAP1[15:10];
	selreg   <= SS_MAP1[18:16];
	security <= SS_MAP1[22:19];
	a53chr   <= SS_MAP1[24:23];
end else if (ce) begin
	if ((prg_ain[15:12] == 4'h5) & prg_write && allow_select)
			selreg <= {1'b0,prg_din[7], prg_din[0]};        // select register
	if (prg_ain[15] & prg_write) begin
		casez (selreg)
			3'b000:  {mode[0], a53chr}  <= {(mode[1] ? mode[0] : prg_din[4]), prg_din[1:0]};     // CHR RAM bank
			3'b001: begin
			         {mode[0], inner}   <= {(mode[1] ? mode[0] : prg_din[4]), prg_din[3:0]};     // "inner" bank
			         {outer[5:3]}       <= {(mapper == 2) ? prg_din[6:4] : outer[5:3]};          // Oversize mapper 2
			        end
			3'b010:  {mode}             <= {prg_din[5:0]};                                       // mode register
			3'b011:  {outer}            <= {prg_din[5:0]};                                       // "outer" bank
			3'b10?:  {security}         <= {prg_din[5:4],prg_din[1:0]};                          // security
			3'b110:  {mode[1:0],inner}  <= {prg_din[7] ^ prg_din[6], prg_din[6], prg_din[3:0]};  // "inner" bank
			3'b111:  {inner}            <= {prg_din[5:2]};                                       // "inner" bank
		endcase
	end
end

assign SS_MAP1_BACK[ 3: 0] = inner;
assign SS_MAP1_BACK[ 9: 4] = mode;
assign SS_MAP1_BACK[15:10] = outer;
assign SS_MAP1_BACK[18:16] = selreg;
assign SS_MAP1_BACK[22:19] = security;
assign SS_MAP1_BACK[24:23] = a53chr;
assign SS_MAP1_BACK[63:25] = 39'b0; // free to be used

always @* begin
	// mirroring mode
	casez(mode[1:0])
		2'b0? : vram_a10 = {mode[0]};        // 1 screen lower
		2'b10 : vram_a10 = {chr_ain[10]};    // vertical
		2'b11 : vram_a10 = {chr_ain[11]};    // horizontal
	endcase

	// PRG ROM bank size select
	casez({mode[5:2], prg_ain[14]})
		5'b00_0?_?: a53prg = {outer[5:0],             prg_ain[14]};  // 32K banks, (B)NROM mode
		5'b01_0?_?: a53prg = {outer[5:1], inner[0],   prg_ain[14]};  // 64K banks, (B)NROM mode
		5'b10_0?_?: a53prg = {outer[5:2], inner[1:0], prg_ain[14]};  // 128K banks, (B)NROM mode
		5'b11_0?_?: a53prg = {outer[5:3], inner[2:0], prg_ain[14]};  // 256K banks, (B)NROM mode

		5'b00_10_1,
		5'b00_11_0: a53prg = {outer[5:0], inner[0]};                 // 32K banks, UNROM mode
		5'b01_10_1,
		5'b01_11_0: a53prg = {outer[5:1], inner[1:0]};               // 64K banks, UNROM mode
		5'b10_10_1,
		5'b10_11_0: a53prg = {outer[5:2], inner[2:0]};               // 128K banks, UNROM mode
		5'b11_10_1,
		5'b11_11_0: a53prg = {outer[5:3], inner[3:0]};               // 256K banks, UNROM mode

		default: a53prg = {(mapper == 2) ? 6'h3F : outer[5:0], extend_bit ? outer[0] : prg_ain[14]};  // 16K fixed bank
	endcase

	chr_dout = 8'hFF;//chr_ain[7:0]; // return open bus = LSB of address? below when CHR disabled by security
end

assign vram_ce = chr_ain[13];
assign prg_is_ram =  (prg_ain[15:13] == 3'b011) && (|flags[29:26] | |flags[34:31]);
assign prg_aout_tmp =  {1'b0, a53prg, prg_ain[13:0]};
assign prg_ram =  {9'b11_1100_000, prg_ain[12:0]}; // assuming (flags[] == 7) => 8K
assign prg_aout = prg_is_ram ? prg_ram : prg_aout_tmp;
assign prg_allow = prg_ain[15] && !prg_write || prg_is_ram;
assign prg_conflict = prg_ain[15] && (((mapper == 3) && (submapper != 1)) || ((mapper == 2) && (submapper == 2)));
assign chr_allow = flags[15];
assign chr_aout = {7'b10_0000_0, a53chr, chr_ain[12:0]};
assign submapper =  flags[24:21];
assign has_chr_dout = (mapper == 185) && (((submapper == 0) && (security[1:0] == 2'b00))
		|| ((submapper == 4) && (security[1:0] != 2'b00))
		|| ((submapper == 5) && (security[1:0] != 2'b01))
		|| ((submapper == 6) && (security[1:0] != 2'b10))
		|| ((submapper == 7) && (security[1:0] != 2'b11)));

// savestate



eReg_SavestateV #(SSREG_INDEX_MAP1, 64'h0000000000000000) iREG_SAVESTATE_MAP1 (clk, SaveStateBus_Din, SaveStateBus_Adr, SaveStateBus_wren, SaveStateBus_rst, SaveStateBus_Dout_active, SS_MAP1_BACK, SS_MAP1);

assign SaveStateBus_Dout = enable ? SaveStateBus_Dout_active : 64'h0000000000000000;

endmodule

module Mapper66(
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
	// savestates
	input       [63:0]  SaveStateBus_Din,
	input       [ 9:0]  SaveStateBus_Adr,
	input               SaveStateBus_wren,
	input               SaveStateBus_rst,
	input               SaveStateBus_load,
	output logic [63:0]  SaveStateBus_Dout
);
logic [21:0] prg_aout, chr_aout;
logic prg_allow;
logic chr_allow;
logic vram_a10;
logic vram_ce;
logic [15:0] flags_out;
reg [4:0] prg_bank;
reg [6:0] chr_bank;
logic [7:0] mapper;
logic GXROM;
logic BitCorps;
logic Mapper140;
logic Mapper101;
logic Mapper46;
logic Mapper86;
logic Mapper87;
logic Mapper144;
logic Mapper145;
logic Mapper149;
logic prg_conflict;
logic prg_conflict_d0;
logic [63:0] SS_MAP1;
logic [63:0] SS_MAP1_BACK;
logic [63:0] SaveStateBus_Dout_active;


assign prg_aout_b   = enable ? prg_aout : 22'h0;
assign prg_dout_b   = enable ? 8'hFF : 8'h0;
assign prg_allow_b  = enable ? prg_allow : 1'h0;
assign chr_aout_b   = enable ? chr_aout : 22'h0;
assign chr_allow_b  = enable ? chr_allow : 1'h0;
assign vram_a10_b   = enable ? vram_a10 : 1'h0;
assign vram_ce_b    = enable ? vram_ce : 1'h0;
assign irq_b        = enable ? 1'b0 : 1'h0;
assign flags_out_b  = enable ? flags_out : 16'h0;
assign audio_b      = enable ? {1'b0, audio_in[15:1]} : 16'h0;






assign flags_out =  {11'h0, prg_conflict_d0, 1'b1, prg_conflict, 2'b00};




assign mapper =  flags[7:0];
assign GXROM =  (mapper == 66);
assign BitCorps =  (mapper == 38);
assign Mapper140 =  (mapper == 140);
assign Mapper101 =  (mapper == 101);
assign Mapper46 =  (mapper == 46);
assign Mapper86 =  (mapper == 86);
assign Mapper87 =  (mapper == 87);
assign Mapper144 =  (mapper == 144);
assign Mapper145 =  (mapper == 145);
assign Mapper149 =  (mapper == 149);

always @(posedge clk) if (ce_master)
if (~enable) begin
	prg_bank <= 0;
	chr_bank <= 0;
end else if (SaveStateBus_load) begin
	prg_bank <= SS_MAP1[ 4: 0];
	chr_bank <= SS_MAP1[11: 5];
end else if (ce) begin
	if (prg_ain[15] & prg_write) begin
		if (GXROM)
			{prg_bank, chr_bank} <= {3'b0, prg_din[5:4], 5'b0, prg_din[1:0]};
		else if (Mapper149)
			{chr_bank} <= {6'b0, prg_din[7]};
		else if (Mapper46)
			{chr_bank[2:0], prg_bank[0]} <= {prg_din[6:4], prg_din[0]};
		else // Color Dreams
			{chr_bank, prg_bank} <= {3'b0, prg_din[7:4], 3'b0, prg_din[1:0]};
	end else if ((prg_ain[15:12]==4'h7) & prg_write & BitCorps) begin
		{chr_bank, prg_bank} <= {5'b0, prg_din[3:2], 3'b0, prg_din[1:0]};
	end else if ((prg_ain[15:12]==4'h6) & prg_write) begin
		if (Mapper140) begin
			{prg_bank, chr_bank} <= {3'b0, prg_din[5:4], 3'b0, prg_din[3:0]};
		end else if (Mapper46) begin
			{chr_bank[6:3], prg_bank[4:1]} <= {prg_din[7:4], prg_din[3:0]};
		end else if (Mapper101) begin
			{chr_bank} <= {3'b0, prg_din[3:0]}; // All 8 bits instead?
		end else if (Mapper87) begin
			{chr_bank} <= {5'b00, prg_din[0], prg_din[1]};
		end else if (Mapper86) begin
			{prg_bank, chr_bank} <= {3'b0, prg_din[5:4], 4'b0, prg_din[6], prg_din[1:0]};
		end
	end else if ((prg_ain[15:8]==8'h41) & prg_write) begin
		if (Mapper145)
			{chr_bank} <= {6'b0, prg_din[7]};
	end
end

assign SS_MAP1_BACK[ 4: 0] = prg_bank;
assign SS_MAP1_BACK[11: 5] = chr_bank;
assign SS_MAP1_BACK[63:12] = 52'b0; // free to be used

assign prg_aout = {2'b00, prg_bank, prg_ain[14:0]};
assign prg_allow = prg_ain[15] && !prg_write;
assign chr_allow = flags[15];
assign chr_aout = {2'b10, chr_bank, chr_ain[12:0]};
assign vram_ce = chr_ain[13];
assign vram_a10 = flags[14] ? chr_ain[10] : chr_ain[11];
assign prg_conflict =  prg_ain[15] && (Mapper144 || Mapper149);
assign prg_conflict_d0 =  prg_ain[15] && (Mapper144);

// savestate



eReg_SavestateV #(SSREG_INDEX_MAP1, 64'h0000000000000000) iREG_SAVESTATE_MAP1 (clk, SaveStateBus_Din, SaveStateBus_Adr, SaveStateBus_wren, SaveStateBus_rst, SaveStateBus_Dout_active, SS_MAP1_BACK, SS_MAP1);

assign SaveStateBus_Dout = enable ? SaveStateBus_Dout_active : 64'h0000000000000000;


endmodule

module Mapper71(
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
	// savestates
	input       [63:0]  SaveStateBus_Din,
	input       [ 9:0]  SaveStateBus_Adr,
	input               SaveStateBus_wren,
	input               SaveStateBus_rst,
	input               SaveStateBus_load,
	output logic [63:0]  SaveStateBus_Dout
);
logic [21:0] prg_aout, chr_aout;
logic prg_allow;
logic chr_allow;
logic vram_a10;
logic vram_ce;
reg [15:0] flags_out = {12'h0, 1'b1, 3'b0};
reg [3:0] prg_bank;
reg ciram_select;
logic mapper232;
reg [3:0] prgout;
logic [63:0] SS_MAP1;
logic [63:0] SS_MAP1_BACK;
logic [63:0] SaveStateBus_Dout_active;


assign prg_aout_b   = enable ? prg_aout : 22'h0;
assign prg_dout_b   = enable ? 8'hFF : 8'h0;
assign prg_allow_b  = enable ? prg_allow : 1'h0;
assign chr_aout_b   = enable ? chr_aout : 22'h0;
assign chr_allow_b  = enable ? chr_allow : 1'h0;
assign vram_a10_b   = enable ? vram_a10 : 1'h0;
assign vram_ce_b    = enable ? vram_ce : 1'h0;
assign irq_b        = enable ? 1'b0 : 1'h0;
assign flags_out_b  = enable ? flags_out : 16'h0;
assign audio_b      = enable ? {1'b0, audio_in[15:1]} : 16'h0;










assign mapper232 =  (flags[7:0] == 232);
always @(posedge clk) if (ce_master)
if (~enable) begin
	prg_bank <= 0;
	ciram_select <= 0;
end else if (SaveStateBus_load) begin
	prg_bank     <= SS_MAP1[ 3: 0];
	ciram_select <= SS_MAP1[    4];
end else if (ce) begin
	if (prg_ain[15] && prg_write) begin
		if (!prg_ain[14] && mapper232) // $8000-$BFFF Outer bank select (only on iNES 232)
			prg_bank[3:2] <= prg_din[4:3];
		if (prg_ain[14:13] == 0)       // $8000-$9FFF Fire Hawk Mirroring
			ciram_select <= prg_din[4];
		if (prg_ain[14])               // $C000-$FFFF Bank select
			prg_bank <= {mapper232 ? prg_bank[3:2] : prg_din[3:2], prg_din[1:0]};
	end
end

assign SS_MAP1_BACK[ 3: 0] = prg_bank;
assign SS_MAP1_BACK[    4] = ciram_select;
assign SS_MAP1_BACK[63: 5] = 59'b0; // free to be used


always @* begin
	casez({prg_ain[14], mapper232})
		2'b0?: prgout = prg_bank;
		2'b10: prgout = 4'b1111;
		2'b11: prgout = {prg_bank[3:2], 2'b11};
	endcase
end

assign prg_aout = {4'b00_00, prgout, prg_ain[13:0]};
assign prg_allow = prg_ain[15] && !prg_write;
assign chr_allow = flags[15];
assign chr_aout = {9'b10_0000_000, chr_ain[12:0]};
assign vram_ce = chr_ain[13];
// XXX(ludde): Fire hawk uses flags[14] == 0 while no other game seems to do that.
// So when flags[14] == 0 we use ciram_select instead.
assign vram_a10 = flags[14] ? chr_ain[10] : ciram_select;

// savestate



eReg_SavestateV #(SSREG_INDEX_MAP1, 64'h0000000000000000) iREG_SAVESTATE_MAP1 (clk, SaveStateBus_Din, SaveStateBus_Adr, SaveStateBus_wren, SaveStateBus_rst, SaveStateBus_Dout_active, SS_MAP1_BACK, SS_MAP1);

assign SaveStateBus_Dout = enable ? SaveStateBus_Dout_active : 64'h0000000000000000;

endmodule

