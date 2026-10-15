import regs_savestates::*;
// Derived from pinned MiSTer NES. See third_party/nes/README.md.
module MMC1(
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
logic mapper171;
reg [15:0] flags_out = {12'h0, 1'b1, 3'b0};
reg [4:0] shift;
reg [4:0] control;
reg [4:0] chr_bank_0;
reg [4:0] chr_bank_1;
reg [4:0] prg_bank;
reg delay_ctrl;
reg chr_write_disable;
logic [3:0] prg_ram_size;
logic [3:0] prg_nvram_size;
logic [2:0] chr_size;
reg [3:0] prgsel;
reg [4:0] chrsel;
logic [21:0] prg_aout_tmp;
reg vram_a10_t;
reg [1:0] prg_ram_a14_13;
logic prg_is_ram;
logic [21:0] prg_ram;
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






assign mapper171 =  (flags[7:0] == 171); //Mapper 171 has hardwired mirroring




// CPPMM
// |||||
// |||++- Mirroring (0: one-screen, lower bank; 1: one-screen, upper bank;
// |||               2: vertical; 3: horizontal)
// |++--- PRG ROM bank mode (0, 1: switch 32 KB at $8000, ignoring low bit of bank number;
// |                         2: fix first bank at $8000 and switch 16 KB bank at $C000;
// |                         3: fix last bank at $C000 and switch 16 KB bank at $8000)
// +----- CHR ROM bank mode (0: switch 8 KB at a time; 1: switch two separate 4 KB banks)


// CCCCC
// |||||
// +++++- Select 4 KB or 8 KB CHR bank at PPU $0000 (low bit ignored in 8 KB mode)


// CCCCC
// |||||
// +++++- Select 4 KB CHR bank at PPU $1000 (ignored in 8 KB mode)


// RPPPP
// |||||
// |++++- Select 16 KB PRG ROM bank (low bit ignored in 32 KB mode)
// +----- PRG RAM chip enable (0: enabled; 1: disabled; ignored on MMC1A)


	// used to prevent fast-write to the control register


assign prg_ram_size =  flags[29:26];
assign prg_nvram_size =  flags[34:31];
assign chr_size =  flags[13:11];

// Update shift register
always @(posedge clk) if (ce_master)
	if (~enable) begin
		shift <= 5'b10000;
		control <= 5'b0_11_00;
		chr_bank_0 <= 0;
		chr_bank_1 <= 0;
		prg_bank <= 5'b10000;
		delay_ctrl <= 0;
	end else if (SaveStateBus_load) begin
		shift      <= SS_MAP1[ 4: 0];
		control    <= SS_MAP1[ 9: 5];
		chr_bank_0 <= SS_MAP1[14:10];
		chr_bank_1 <= SS_MAP1[19:15];
		prg_bank   <= SS_MAP1[24:20];
		delay_ctrl <= SS_MAP1[   25];
	end else if (ce) begin // M2
		if (prg_write && prg_ain[15]) begin // /ROMSEL && /CPURnW
			delay_ctrl <= 1'b1;
			if (prg_din[7]) begin
				shift <= 5'b10000;  // Shift and Control are reset, nothing else
				control <= control | 5'b0_11_00;
			end else if (!delay_ctrl) begin // Only clocked if DIN bit 7 is not set, and this is the first falling edge of rw during M2
				if (shift[0]) begin
					casez(prg_ain[14:13])
						0: control    <= {prg_din[0], shift[4:1]};
						1: chr_bank_0 <= {prg_din[0], shift[4:1]};
						2: chr_bank_1 <= {prg_din[0], shift[4:1]};
						3: prg_bank   <= {prg_din[0], shift[4:1]};
					endcase
					shift <= 5'b10000;
				end else begin
					shift <= {prg_din[0], shift[4:1]};
				end
			end
		end else begin
			delay_ctrl <= 1'b0;
		end
	end

assign SS_MAP1_BACK[ 4: 0]	= shift;
assign SS_MAP1_BACK[ 9: 5] = control;
assign SS_MAP1_BACK[14:10] = chr_bank_0;
assign SS_MAP1_BACK[19:15] = chr_bank_1;
assign SS_MAP1_BACK[24:20] = prg_bank;
assign SS_MAP1_BACK[   25] = delay_ctrl;
assign SS_MAP1_BACK[63:26] = 38'b0; // free to be used

// The PRG bank to load. Each increment here is 16kb. So valid values are 0..15.
// prg_ain[14] selects bank0 ($8000) or bank1 ($C000)

always @* begin
	casez({control[3:2], prg_ain[14]})
		3'b0?_?: prgsel = {prg_bank[3:1], prg_ain[14]}; // Swap 32Kb
		3'b10_0: prgsel = 4'b0000;                      // Swap 16Kb at $C000 with access at $8000, so select page 0 (hardcoded)
		3'b10_1: prgsel = prg_bank[3:0];                // Swap 16Kb at $C000 with $C000 access, so select page based on prg_bank (register 3)
		3'b11_0: prgsel = prg_bank[3:0];                // Swap 16Kb at $8000 with $8000 access, so select page based on prg_bank (register 3)
		3'b11_1: prgsel = 4'b1111;                      // Swap 16Kb at $8000 with $C000 access, so select last page (hardcoded)
	endcase
end

// The CHR bank to load. Each increment here is 4 kb. So valid values are 0..31.

always @* begin
	casez({control[4], chr_ain[12]})
		2'b0_?: chrsel = {chr_bank_0[4:1], chr_ain[12]};
		2'b1_0: chrsel = chr_bank_0;
		2'b1_1: chrsel = chr_bank_1;
	endcase
end

assign chr_aout = {5'b100_00, chrsel, chr_ain[11:0]};

// for large PRG ROM, CHR A16 selects the 256KB PRG bank. ROM <= 256KB is not affected because of the mask in cart.sv
assign prg_aout_tmp =  {3'b000, chrsel[4], prgsel, prg_ain[13:0]};

// The a10 VRAM address line. (Used for mirroring)

always @* begin
	casez(mapper171 ? 2'b10 : control[1:0])   //if mapper 171 then set to vertical mirroring, else do normal MMC1 mirroring selection.
		2'b00: vram_a10_t = 0;             // One screen, lower bank
		2'b01: vram_a10_t = 1;             // One screen, upper bank
		2'b10: vram_a10_t = chr_ain[10];   // One screen, vertical
		2'b11: vram_a10_t = chr_ain[11];   // One screen, horizontal
	endcase
end

// PRG RAM banking

always @* begin
	// SOROM & SZROM: The battery backed RAM chip is selected when the chrsel bit is high
	// We only save the first 8KB so the battery RAM needs to be first.
	if (prg_ram_size == 4'd7 && prg_nvram_size == 4'd7) begin
		// CHR ROM 16-64KB = SZROM. Other is SOROM
		prg_ram_a14_13 = {1'b0, (chr_size >= 3'd1) ? ~chrsel[4] : ~chrsel[3]}; // 8+8KB
	end else if (prg_nvram_size == 4'd9) begin
		prg_ram_a14_13 = {chrsel[3],chrsel[2]}; // SXROM 32KB
	end else begin
		prg_ram_a14_13 = 2'b00; // 8KB, no banking
	end
end

assign vram_a10 = vram_a10_t;
assign vram_ce = chr_ain[13];

assign prg_is_ram =  prg_ain >= 'h6000 && prg_ain < 'h8000;
assign prg_allow = prg_ain[15] && !prg_write || prg_is_ram;
assign prg_ram =  {7'b11_1100_0, prg_ram_a14_13, prg_ain[12:0]};

assign prg_aout = prg_is_ram ? prg_ram : prg_aout_tmp;
assign chr_allow = flags[15];

// savestate



eReg_SavestateV #(SSREG_INDEX_MAP1, 64'h0000000000000000) iREG_SAVESTATE_MAP1 (clk, SaveStateBus_Din, SaveStateBus_Adr, SaveStateBus_wren, SaveStateBus_rst, SaveStateBus_Dout_active, SS_MAP1_BACK, SS_MAP1);

assign SaveStateBus_Dout = enable ? SaveStateBus_Dout_active : 64'h0000000000000000;

endmodule

