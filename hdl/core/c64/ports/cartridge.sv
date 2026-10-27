/*
	CRT cartridge handling for C64 L.C.Ashmore 2017

	Improvements by Sorgelig
*/

module cartridge
(
	input             clk32,				 	// 32mhz clock source
	input             reset_n,					// reset signal

	input             cart_loading,
	input       [7:0] cart_id,					// cart ID or cart type
	input             cart_exrom,				// CRT file EXROM status
	input             cart_game,				// CRT file GAME status
	input             cart_bank_hi,		   // bank is high
	input             cart_bank_16k,
	input       [7:0] cart_bank_num,
	input       [7:0] cart_bank_addr,		// chip packet address
	input             cart_bank_wr,
	input             cart_boot,

	output            exrom,					// exrom line
	output            game,						// game line

	input             romL,						// romL signal in
	input             romH,						// romH signal in
	input             UMAXromH,				// romH VIC II address signal
	input             IOE,						// IOE control signal
	input             IOF,						// IOF control signal
	input             mem_write,				// memory write active
	input             mem_ce,

	output            mem_ce_out,
	output reg        mem_write_out,
	input       [7:0] mem_in,
	output      [7:0] mem_out,
	output     [24:0] mem_addr, 	         // translated address output

	output            IO_rom,					// FLAG to enable IOE/IOF address relocation
	output            IO_rd,
	output reg  [7:0] IO_data,
	input      [15:0] addr_in,             // address from cpu
	input       [7:0] data_in,  			   // data from cpu going to sdram
	output      [7:0] data_out,

	input             freeze_key,
	input             mod_key,
	output reg        nmi,
	input             nmi_ack,
	output      [5:0] ef_bank,				// EasyFlash bank register
	output reg        ef_led					// EasyFlash $DE02 bit 7
);

reg  [7:0] bank_lo;
reg  [7:0] bank_hi;
reg [12:0] mask_lo;

reg  [7:0] IOE_bank;
reg  [7:0] IOF_bank;

reg        exrom_overide;
reg        game_overide;
reg        force_ultimax;
assign     {exrom, game} = force_ultimax ? 2'b10 : {exrom_overide, game_overide};

// The loader stores admitted CHIP packets in bank order, so every mapper
// except type 0 uses its bank number as the 8 KiB slot. Type 0 records
// where bank 0's ROML and ROMH halves landed.
reg  [6:0] normal_lo;
reg  [6:0] normal_hi;

reg  [7:0] bank_cnt;
always @(posedge clk32) begin
	reg old_loading;
	old_loading <= cart_loading;

	if(~old_loading & cart_loading) begin
		bank_cnt <= 0;
		normal_lo <= 0;
		normal_hi <= 0;
	end
	if(cart_bank_wr) begin
		bank_cnt <= bank_cnt + 1'd1;
		if(cart_bank_num == 0) begin
			if(!cart_bank_hi) begin
				normal_lo <= cart_bank_addr[6:0];
				if(cart_bank_16k) normal_hi <= cart_bank_addr[6:0]+1'd1;
			end
			else normal_hi <= cart_bank_addr[6:0];
		end
	end
end

reg IOE_ena,IOF_ena;

assign IO_rom = (IOE & IOE_ena) | (IOF & IOF_ena);

reg old_ioe, old_iof;
always @(posedge clk32) begin
	old_ioe <= IOE;
	old_iof <= IOF;
end

wire stb_ioe = (~old_ioe & IOE);
wire stb_iof = (~old_iof & IOF);

wire ioe_wr = stb_ioe & mem_write;
wire ioe_rd = stb_ioe & ~mem_write;

wire iof_wr = stb_iof & mem_write;
wire iof_rd = stb_iof & ~mem_write;

reg  old_freeze = 0;
wire freeze_req = (~old_freeze & freeze_key);

reg  old_nmiack = 0;
wire freeze_ack = (nmi & ~old_nmiack & nmi_ack);
wire freeze_crt = freeze_ack & ~mod_key;

reg  cart_disable = 0;
reg  rom_kbb;

// EasyFlash 256-byte RAM at $DF00-$DFFF, the only cartridge RAM admitted.
(* ram_style = "distributed" *) reg [7:0] ef_ram[0:255];
integer ef_i;
initial for(ef_i=0;ef_i<256;ef_i=ef_i+1) ef_ram[ef_i] = 8'h00;
wire ef_io2 = cart_id == 32 && IOF;
always @(posedge clk32) if(ef_io2 && iof_wr) ef_ram[addr_in[7:0]] <= data_in;
assign IO_rd   = ef_io2 && !mem_write;
assign ef_bank = bank_lo[6:1];

// 0018 - EXROM line status
// 0019 - GAME line status

always @(posedge clk32) begin
	// Initialized block state must persist between clock edges in strict tools.
	static reg init_n = 0;
	static reg allow_freeze = 0;
	static reg saved_d6 = 0;
	reg [15:0] count;
	reg        count_ena;
	reg [15:0] old_id;

	old_freeze <= freeze_key;
	if(freeze_req & (allow_freeze | mod_key)) nmi <= 1;

	old_nmiack <= nmi_ack;
	if(freeze_ack) nmi <= 0;

	init_n <= 1;
	old_id <= cart_id;

	if(~reset_n || (old_id != cart_id)) begin
		cart_disable <= 0;
		bank_lo <= 0;
		bank_hi <= 0;
		IOE_ena <= 0;
		IOF_ena <= 0;
		init_n <= 0;
		allow_freeze <= 1;
		nmi <= 0;
		saved_d6 <= 0;
		mask_lo <= 13'h1FFF;
		exrom_overide <= 1;
		game_overide <= 1;
		rom_kbb <= 0;
		ef_led <= 0;
	end
	else
	case(cart_id)

		// Generic 8k(exrom=0,game=1), 16k(exrom=0,game=0), ULTIMAX(exrom=1,game=0)
		0:	begin
				exrom_overide <= cart_exrom;
				game_overide <= cart_game;
				bank_lo <= normal_lo;
				bank_hi <= normal_hi;
			end

		// Final Cart III - (64k 4x16k banks)
		// all banks @ $8000-$BFFF - switching by $DFFF
		3:	begin
				if(!init_n) begin
					game_overide <= 0;
					exrom_overide<= 0;
					cart_disable <= 0;
					bank_lo <= 0;
					bank_hi <= 1;
					IOE_ena <= 1;
					IOE_bank<= 0;
					IOF_ena <= 1;
					IOF_bank<= 0;
				end
				else if(!cart_disable) begin
					if(iof_wr && &addr_in[7:0]) begin
						bank_lo <= {data_in[1:0],1'd0};
						bank_hi <= {data_in[1:0],1'd1};
						IOE_bank<= {data_in[1:0],1'd0};
						IOF_bank<= {data_in[1:0],1'd0};
						exrom_overide <= data_in[4];
						game_overide  <= data_in[5];
						saved_d6 <= data_in[6];
						if(~freeze_key & saved_d6 & ~data_in[6]) nmi <= 1;
						if(data_in[6]) allow_freeze <= 1;
						cart_disable <= data_in[7];
					end
				end
				if(freeze_crt) begin
					cart_disable <= 0;
					game_overide <= 0;
					allow_freeze <= 0;
				end
			end

		// Simons Basic - (game=0, exrom=0, 2 banks by 8k)
		// Read to IOE switches 8k config
		// Write to IOE switches 16k config
		4: begin
				if(!init_n) begin
					exrom_overide <= 0;
					game_overide <= 0;
					bank_lo <= 0;
					bank_hi <= 1;
				end
				if(ioe_wr) game_overide <= 0;
				if(ioe_rd) game_overide <= 1;
			end

		// Ocean Type 1 - (game=0, exrom=0, 128k,256k or 512k in 8k banks)
		// BANK is written to lower 6 bits of $DE00 - bit 8 is always set
		// best to mirror banks at $8000 and $A000
		5:	begin
				exrom_overide <= 0;
				game_overide  <= (bank_cnt > 32);
				if(ioe_wr) begin
					bank_lo <= data_in[5:0];
					bank_hi <= data_in[5:0];
				end
			end

		// PowerPlay, FunPlay
		7:	begin
				if(~init_n) begin
					exrom_overide <= 0;
					game_overide  <= 1;
				end

				if(ioe_wr) begin
					bank_lo <= {data_in[0],data_in[5:3]};
					if({data_in[7:6],data_in[2:1]} == 'b1011) exrom_overide <= 1;
					if({data_in[7:6],data_in[2:1]} == 'b0000) exrom_overide <= 0;
				end
			end

		// "Super Games"
		8:	begin
				if(~init_n) begin
					exrom_overide <= 0;
					game_overide  <= 0;
					bank_lo <= 0;
					bank_hi <= 1;
				end

				if(~cart_disable & iof_wr) begin
					bank_lo <= {data_in[1:0],1'd0};
					bank_hi <= {data_in[1:0],1'd1};
					game_overide  <= data_in[2];
					exrom_overide <= data_in[2];
					cart_disable  <= data_in[3];
				end
			end

		// Epyx Fastload - (game=1, exrom=0, 8k bank)
		// any access to romL or $DE00 charges a capacitor
		// Once discharged the exrom drops to ON disabling cart
		10: begin
				if(!init_n) count_ena <= 0;
				if(IOE || romL) count_ena <= 1;

				if(!init_n || IOE || romL) begin
					game_overide  <= 1;
					exrom_overide <= 0;
					count <= 16384;
					IOF_ena <= 1;
					IOF_bank<= 0;
				end
				else
				if(count_ena) begin
					if(count) count <= count - 1'd1;
					else exrom_overide <= 1;
				end
			end

		// Westermann: an IO2 read removes only the upper 8 KiB ROM.
		11: begin
				if(!init_n) begin
					bank_lo <= 0;
					bank_hi <= 1;
					exrom_overide <= 0;
					game_overide <= 0;
				end
				else if(iof_rd) game_overide <= 1;
			end

		// FINAL CARTRIDGE 1,2
		// 16k rom - IOE turns off rom / IOF turns rom on
		13: begin
				if(!init_n) begin
					bank_lo <= 0;
					bank_hi <= 1;
					game_overide  <= 0;
					exrom_overide <= 0;

					// Last 2 pages visible at IOE / IOF
					IOE_bank <= 0;
					IOF_bank <= 0;
					IOE_ena  <= 1;
					IOF_ena  <= 1;
				end

				if(freeze_crt) begin
					game_overide <= 0;
					allow_freeze <= 0;
				end

				if(IOE) begin
					game_overide  <= 1;
					exrom_overide <= 1;
					allow_freeze  <= 1;
				end

				if(IOF) begin
					game_overide  <= 0;
					exrom_overide <= 0;
				end
			end

		// C64GS - (game=1, exrom=0, 64 banks by 8k)
		// 8k config
		// Reading from IOE ($DE00 $DEFF) switches to bank 0
		15: begin
				game_overide  <= 1;
				exrom_overide <= 0;
				if(ioe_rd) bank_lo <= 0;
				if(ioe_wr) bank_lo <= addr_in[5:0];
			end

		// Dinamic - (game=1, exrom=0, 16 banks by 8k)
		17: begin
				game_overide  <= 1;
				exrom_overide <= 0;
				if(ioe_rd) bank_lo <= addr_in[3:0];
			end

		// Zaxxon, Super Zaxxon (game=0, exrom=0 - 4Kb + 2x8KB)
		18: begin
				mask_lo <= 'hFFF;
				game_overide  <= 0;
				exrom_overide <= 0;
				if(romL & mem_ce & ~addr_in[12]) bank_hi <= 1;
				if(romL & mem_ce &  addr_in[12]) bank_hi <= 2;
			end

		// Magic Desk - (game=1, exrom=0, up to 128 banks of 8k)
		19: begin
				if(!init_n) begin
					game_overide  <= 1;
					exrom_overide <= 0;
					bank_lo <= 0;
				end

				if(ioe_wr) begin
					if(bank_cnt <= 16) bank_lo <= data_in[3:0];
					else if(bank_cnt <= 32) bank_lo <= data_in[4:0];
					else if(bank_cnt <= 64) bank_lo <= data_in[5:0];
					else bank_lo <= data_in[6:0];
					exrom_overide <= data_in[7];
				end
			end

		// Comal80 - (game=0, exrom=0, 4 banks by 16k)
		21: begin
				if(!init_n) begin
					bank_lo <= 0;
					bank_hi <= 1;
					game_overide  <= 0;
					exrom_overide <= 0;
				end
				if(ioe_wr) begin
					case(data_in[7:5])
						'b010:
							begin
								exrom_overide <= 0;
								game_overide  <= 1;
							end
						'b111:
							begin
								exrom_overide <= 1;
								game_overide  <= 1;
							end
						default:
							begin
								exrom_overide <= 0;
								game_overide  <= 0;
							end
					endcase

					bank_lo <= {data_in[1:0], 1'b0};
					bank_hi <= {data_in[1:0], 1'b1};
				end
			end

		// Waterloo Structured BASIC (game=1, exrom=0, two 8k banks)
		22: begin
				if(!init_n) begin
					game_overide  <= 1;
					exrom_overide <= 0;
					bank_lo       <= 0;
					bank_hi       <= 0;
				end
				else if(stb_ioe) begin
					bank_lo       <= addr_in[1];
					exrom_overide <= addr_in[0];
				end
			end

		// Mikro Assembler - (game=1, exrom=0, 8k)
		28: begin
				game_overide  <= 1;
				exrom_overide <= 0;
				IOE_bank <= 0;
				IOE_ena  <= 1;
				IOF_bank <= 0;
				IOF_ena  <= 1;
			end

		// EasyFlash: the loader stores bank b's ROML in slot 2b and its ROMH
		// in slot 2b+1. With $DE02 M=0 the jumper drives GAME: Boot starts
		// the cartridge in Ultimax mode, Disable leaves it hidden (VICE table).
		32: begin
				if(!init_n) begin
					exrom_overide <= 1;
					game_overide  <= ~cart_boot;
					bank_lo <= 0;
					bank_hi <= 1;
				end

				if(ioe_wr) begin
					if(addr_in[1]) begin
						game_overide  <= data_in[2] ? ~data_in[0] : ~cart_boot;
						exrom_overide <= ~data_in[1];
						ef_led        <= data_in[7];
					end
					else begin
						bank_lo <= {data_in[5:0],1'b0};
						bank_hi <= {data_in[5:0],1'b1};
					end
				end
			end

		// prophet64
		43: begin
				if(!init_n) begin
					exrom_overide <= 0;
					game_overide  <= 1;
					bank_lo       <= 0;
				end
				else if(iof_wr) begin
					bank_lo       <= data_in[4:0];
					exrom_overide <= data_in[5];
				end
			end

		// Kingsoft Business Basic
		54: begin
				bank_lo       <= 0;
				bank_hi       <= 1;

				if(!init_n || ioe_rd) begin
					rom_kbb <= 0;
					game_overide <= 0;
					exrom_overide <= 0;
				end
				else if(ioe_wr) begin
					rom_kbb <= 1;
					// Dynamic Ultimax is asserted only in the ROM ranges below.
					game_overide <= 1;
					exrom_overide <= 1;
				end
			end

		// RGCD (game=1, exrom=0, 8 banks by 8k)
		57: begin
				if(!init_n) begin
					game_overide  <= 1;
					exrom_overide <= 0;
					bank_lo <= 0;
				end

				if(~cart_disable & ioe_wr) begin
					bank_lo <= data_in[2:0];
					if(data_in[3]) begin
						cart_disable  <= 1;
						game_overide  <= 1;
						exrom_overide <= 1;
					end
				end
			end

		// Blackbox V8 decodes IO2 address bits, not the written data.
		// Inverted bank bits select one of two or four 16 KiB ROM banks.
		64: begin
				if(!init_n) begin
					bank_lo <= {5'd0, (bank_cnt[1:0] - 2'd1), 1'b0};
					bank_hi <= {5'd0, (bank_cnt[1:0] - 2'd1), 1'b1};
					game_overide <= 0;
					exrom_overide <= 0;
				end
				else if(iof_wr) begin
					bank_lo <= {5'd0, (~addr_in[3:2] & (bank_cnt[1:0] - 2'd1)), 1'b0};
					bank_hi <= {5'd0, (~addr_in[3:2] & (bank_cnt[1:0] - 2'd1)), 1'b1};
					game_overide <= addr_in[0];
					exrom_overide <= addr_in[1];
				end
			end

		// Blackbox V3: IO1 writes disable, IO2 writes re-enable 8 KiB ROM.
		65: begin
				if(!init_n) begin
					bank_lo <= 0;
					game_overide <= 1;
					exrom_overide <= 0;
				end
				else if(ioe_wr) exrom_overide <= 1;
				else if(iof_wr) exrom_overide <= 0;
			end

		// Blackbox V4: IO2 reads disable, IO1 reads re-enable 16 KiB ROM.
		66: begin
				if(!init_n) begin
					bank_lo <= 0;
					bank_hi <= 1;
					game_overide <= 0;
					exrom_overide <= 0;
				end
				else if(iof_rd) begin
					game_overide <= 1;
					exrom_overide <= 1;
				end
				else if(ioe_rd) begin
					game_overide <= 0;
					exrom_overide <= 0;
				end
			end

		// Blackbox V9: IO1 address selects mode and bank. Writes invert A7.
		// IO1 reads expose the newly selected bank's $9Exx ROM mirror.
		71: begin
				if(!init_n) begin
					bank_lo <= 2;
					bank_hi <= 3;
					IOE_bank <= 2;
					IOE_ena <= 1;
					game_overide <= 0;
					exrom_overide <= 1;
				end
				else if(ioe_rd || ioe_wr) begin
					bank_lo <= {6'd0, (addr_in[7] ^ mem_write), 1'b0};
					bank_hi <= {6'd0, (addr_in[7] ^ mem_write), 1'b1};
					IOE_bank <= {6'd0, (addr_in[7] ^ mem_write), 1'b0};
					game_overide <= addr_in[0];
					exrom_overide <= addr_in[6];
				end
			end

		// Magic Desk 2, (game=0, exrom=0, up to 128 16k banks)
		85: begin
				if(!init_n) begin
					game_overide  <= 0;
					exrom_overide <= 0;
					bank_lo       <= 0;
					bank_hi       <= 1;
				end
				else if(ioe_wr) begin
					bank_lo       <= {data_in[6:0], 1'b0};
					bank_hi       <= {data_in[6:0], 1'b1};
					game_overide  <= data_in[7];
					exrom_overide <= data_in[7];
				end
			end

		// Protovision Megabyter: 8 KiB banks and inverted GAME mode bit.
		// The 8-bit counter wraps at 256 banks, yielding a full 0xff mask.
		86: begin
				if(!init_n) begin
					game_overide <= 1;
					exrom_overide <= 0;
					bank_lo <= 0;
					bank_hi <= 0;
				end
				else if(ioe_wr) begin
					if(addr_in[1]) begin
						game_overide <= ~data_in[0];
						exrom_overide <= data_in[1];
					end else begin
						bank_lo <= data_in & (bank_cnt - 8'd1);
						bank_hi <= data_in & (bank_cnt - 8'd1);
					end
				end
			end

		// Protovision TwoMegabyter. IO1 A1 selects bank or GAME/EXROM.
		// Admitted ROM sizes are powers of two up to 2 MiB. Higher bank
		// address bits mirror that ROM; the 4 MiB profile is not admitted.
		87: begin
				if(!init_n) begin
					game_overide <= 0;
					exrom_overide <= 0;
					bank_lo <= 0;
					bank_hi <= 1;
				end
				else if(ioe_wr) begin
					if(addr_in[1]) begin
						game_overide <= data_in[0];
						exrom_overide <= data_in[1];
					end else begin
						bank_lo <= {(data_in[6:0] & (bank_cnt[6:0] - 7'd1)), 1'b0};
						bank_hi <= {(data_in[6:0] & (bank_cnt[6:0] - 7'd1)), 1'b1};
					end
				end
			end
	endcase
end

reg  [24:0] addr_out;

assign mem_addr = addr_out;
assign mem_out  = data_in;
assign data_out = mem_in;


// ************************************************************************************************************
// ****** Address handling - Redirection to SDRAM CRT file
// ************************************************************************************************************

// IO1 and IO2 expose read-only ROM windows only.
wire cs_ioe = IOE && !mem_write && IOE_ena;
wire cs_iof = IOF && !mem_write && IOF_ena;

assign mem_ce_out = mem_ce | (cs_ioe & stb_ioe) | (cs_iof & stb_iof);

//ROM banks are mapped to 0x200000 (2MB max)
function [8:0] get_bank;
	input [7:0] bank;
begin
	get_bank = {1'b1, bank[7:0]};
end
endfunction

always @* begin
	IO_data = ef_ram[addr_in[7:0]];
	force_ultimax = 0;

	//prohibit to write in ultimax mode into underlaying (actually non-existent) RAM
	mem_write_out = ~((romL | romH) & exrom_overide & ~game_overide) & mem_write;
	addr_out = addr_in;

	if(reset_n) begin
		if(romH & ~mem_write) addr_out[24:13] =  get_bank(bank_hi);
		if(romL & ~mem_write) addr_out        = {get_bank(bank_lo), addr_in[12:0] & mask_lo};

		if(cs_ioe) addr_out[24:13] = get_bank(IOE_bank); // read DExx
		if(cs_iof) addr_out[24:13] = get_bank(IOF_bank); // read DFxx

		if(UMAXromH) addr_out[24:12] = {get_bank(bank_hi), 1'b1}; // ULTIMAX CharROM

		case(cart_id)
			54: if(rom_kbb && addr_in[15] && addr_in[14:13] != 2'b10) begin
					force_ultimax = 1;
					// Ultimax leaves $A000-$BFFF unmapped and suppresses RAM
					// writes in all three ROM ranges. Other RAM/IO stays normal.
					mem_write_out = 0;
					if(addr_in[15:13] == 3'b111) addr_out[24:13] = get_bank(2);
				end
			71: if(IOE && !mem_write) begin
					// Select from this read's address before the cache request,
					// not from the register updated by the same IO1 strobe.
					addr_out[24:13] = get_bank({6'd0, addr_in[7], 1'b0});
				end
		default:;
		endcase
	end
end

endmodule
