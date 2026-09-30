//============================================================================
//  Megadrive/Master Cartridge implementation
//  Copyright (c) 2023 Alexey Melnikov
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
//  more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//============================================================================

// Multitini port, 2026-09-30: cached ROM interface and byte-addressed save RAM.
// Genesis mapper/decode logic retained. SMS, SVP, J-Cart and writable ROM
// hardware are excluded. Serial EEPROM engines are original Multitini RTL.
module multitini_genesis_cartridge (
 input wire clk, reset, pause_request,
 input wire cart_dl, cart_dl_wr,
 input wire [22:0] cart_load_addr,
 input wire [15:0] cart_load_data,
 input wire [23:1] cart_addr,
 input wire [15:0] cart_data_wr,
 input wire cart_cs, cart_oe, cart_lwr, cart_uwr, cart_time, cart_dma,
 output reg [15:0] cart_data,
 output wire cart_data_en, cart_dtack,
 input wire [15:0] save_addr,
 input wire [7:0] save_data,
 input wire save_wr,
 output reg [7:0] save_q,
 output reg save_change,
 output wire ym2612_quirk,
 output wire [23:0] rom_address,
 output wire rom_request,
 input wire [15:0] rom_data,
 input wire rom_data_valid,
 output wire rom_stall
);
// Declarations precede use for both Icarus and Vivado.
wire cart_ms;
wire [24:0] cart_dl_addr;
wire [15:0] cart_dl_data;
wire [23:0] md_addr;
reg [24:1] rom_mask=0;
reg [25:1] rom_sz=0;
reg old_download=0;
wire [7:0] sram_q;
wire [15:0] svp_data;
wire svp_cs;
wire jcart_cs;
wire [15:0] jcart_data;
reg [7:0] gun_sensor_delay;
reg gun_type;
wire special_read;
wire [15:0] sram_address;
wire [7:0] sram_data;
wire sram_write;
reg [7:0] sram_read;
reg [16:0] initialize_index=17'h10000;
wire initializing;
wire [15:0] runtime_save_addr;
wire serial_write;
wire runtime_save_write;
wire [7:0] runtime_save_data;
reg [5:0] md_bank[8];
reg md_bank_sram = 0;
reg md_bank_use = 0;
wire [24:1] md_cart_addr;
wire md_sram_cs;
wire md_eeprom_cs;
reg [15:0] md_eeprom_data;
reg eeprom_sdai;
wire eeprom_sdao;
reg eeprom_scl;
wire [12:0] eeprom_ram_a;
wire [7:0] eeprom_ram_d;
wire eeprom_ram_we;
wire [7:0] eeprom_ram_q;
reg eeprom_bank;
wire eeprom_sda;
reg ep_si, m95_so, ep_sck, ep_hold, ep_cs;
wire [7:0] m95_di, m95_q;
wire [11:0] m95_addr;
wire m95_rnw;
wire pier_prot_cs;
reg [15:0] pier_prot_data;
wire pier_eeprom_cs;
wire [15:0] pier_eeprom_data;
wire cart_cs_ext;
reg [21:17] realtec_bank;
reg [4:0] realtec_mask;
reg realtec_boot;
wire [23:1] realtec_addr;
wire sf_cs;
wire [15:0] sf_data;
reg [7:0] sf001_bank_reg;
reg [7:0] sf002_bank_reg;
reg sf004_sram_reg;
reg [7:0] sf004_bank_reg;
reg [2:0] sf004_first_page;
wire [23:1] sf001_rom_a;
wire sf001_sram_en;
wire [23:1] sf002_rom_a;
wire sf002_sram_en;
wire [23:1] sf004_rom_a;
wire sf004_sram_en;
wire [15:0] sf004_do;
wire sf_sram_en;
wire [23:1] sf_rom_addr;
wire sf_sram_wr;
reg chk_cs;
reg [15:0] chk_data;
reg sram00_quirk, fmbusy_quirk, noram_quirk, pier_quirk, svp_quirk, schan_quirk;
reg [3:0] eeprom_quirk;
reg realtec_quirk;
reg [2:0] sf_quirk;
reg [3:0] chk_quirk;
reg initialize_zero_previous=0;

assign cart_ms = 1'b0;
assign cart_dl_addr = {1'b0,cart_load_addr,1'b0};
 // Upstream's download bus is little-endian; the Multitini stream is big-endian.
assign cart_dl_data = {cart_load_data[7:0],cart_load_data[15:8]};
assign md_addr = {cart_addr,1'b0};



 always @(posedge clk) begin
  initialize_zero_previous<=sram00_quirk;
  old_download<=cart_dl;
  if(cart_dl && !old_download) begin rom_mask<=0; rom_sz<=0; end
  if(cart_dl && cart_dl_wr) begin
   rom_mask<=rom_mask | {1'b0,cart_load_addr};
   rom_sz<={2'b00,cart_load_addr}+1'b1;
  end
 end

assign svp_data = 0;
assign svp_cs = 0;
assign jcart_cs = 0;
assign jcart_data = 0;


assign special_read = md_sram_cs || md_eeprom_cs || pier_prot_cs || pier_eeprom_cs || sf_cs || chk_cs;
 assign rom_address={(md_cart_addr[23:1] & rom_mask[23:1]),1'b0};
 assign rom_request=cart_oe && (cart_cs || cart_cs_ext) && !special_read && !reset && !cart_dl;
 assign rom_stall=rom_request && !rom_data_valid;
 assign cart_dtack=cart_cs_ext && rom_data_valid;
 assign cart_data_en=cart_oe && (cart_cs || cart_cs_ext || pier_eeprom_cs || sf_cs || chk_cs);
 always @* begin
  cart_data=rom_data;
  if(md_sram_cs) cart_data={sram_q,sram_q};
  if(pier_prot_cs) cart_data=pier_prot_data;
  if(pier_eeprom_cs) cart_data=pier_eeprom_data;
  if(md_eeprom_cs) cart_data=md_eeprom_data;
  if(sf_cs) cart_data=sf_data;
  if(chk_cs) cart_data=chk_data;
 end
assign sram_address = pier_quirk ? {4'd0,m95_addr} :
                          eeprom_quirk ? {3'd0,eeprom_ram_a} :
                          sf_quirk ? {1'd0,cart_addr[15:1]} : cart_addr[16:1];
assign sram_data = pier_quirk ? m95_di : eeprom_quirk ? eeprom_ram_d : cart_data_wr[7:0];
assign sram_write = pier_quirk ? m95_rnw : eeprom_quirk ? eeprom_ram_we :
                   sf_quirk ? sf_sram_wr : md_sram_cs && cart_lwr;
 (* ram_style="block" *) reg [7:0] save_memory[0:65535];

 assign sram_q=sram_read;

assign initializing = !initialize_index[16];
assign runtime_save_addr = initializing ? initialize_index[15:0] : sram_address;
assign serial_write = (pier_quirk || eeprom_quirk) && sram_write;
assign runtime_save_write = initializing || (sram_write && !reset &&
                          (serial_write || (!pause_request && !rom_stall)));
assign runtime_save_data = initializing ? (sram00_quirk ? 8'h00 : 8'hff) : sram_data;
 // True dual-port RAM: exactly one address per port, including initialization.
 // Host writes take priority explicitly; there is no vendor-dependent
 // same-address dual-write collision.
 always @(posedge clk) begin
  sram_read<=save_memory[runtime_save_addr];
  if(runtime_save_write && !(save_wr && save_addr==runtime_save_addr))
   save_memory[runtime_save_addr]<=runtime_save_data;
  save_change<=!initializing && runtime_save_write;
  if(cart_dl && (!old_download || sram00_quirk!=initialize_zero_previous)) initialize_index<=0;
  else if(initializing) initialize_index<=initialize_index+1'b1;
 end
 always @(posedge clk) begin
  save_q<=save_memory[save_addr];
  if(save_wr) save_memory[save_addr]<=save_data;
 end
 // The complete ascending restore starts after initialization starts and is
 // no faster than the sweep, so every restored byte survives a short cart.

integer bank_i;
initial for(bank_i=0;bank_i<8;bank_i=bank_i+1) md_bank[bank_i]=bank_i;



always @(posedge clk) begin
	if(reset) begin
		for(integer k=0;k<8;k=k+1) md_bank[k]<=k;
		md_bank_sram <= 0;
		md_bank_use <= 0;
	end
	else if (cart_lwr && cart_time) begin
		if(rom_mask[24:22]) begin
			if(cart_addr[3:1]) begin
				md_bank_use <= 1;
				if(~pier_quirk) md_bank[cart_addr[3:1]] <= cart_data_wr[5:0]; //SSF2 banks
				else if(cart_addr[3:1] == 4) {ep_cs, ep_hold , ep_sck, ep_si} <= cart_data_wr[3:0]; // Pier EEPROM
				else if(~cart_addr[3]) md_bank[{1'b1,cart_addr[2:1]}] <= cart_data_wr[3:0]; // Pier Banks
			end
			else if(~pier_quirk) md_bank_sram <= cart_data_wr[0];
		end
		else if(~schan_quirk) md_bank_sram <= cart_data_wr[0];
	end
end

assign md_cart_addr = realtec_quirk ? realtec_addr       :
                           sf_quirk      ? sf_rom_addr        :
                           md_bank_use   ? {md_bank[cart_addr[21:19]], cart_addr[18:1]} :
                                           cart_addr;


// SRAM
assign md_sram_cs = ~cart_ms && (cart_addr[23:21] == 1) && (md_bank_sram || (cart_addr >= rom_sz && ~&cart_addr[20:19])) && ~noram_quirk;

// EEPROM
assign md_eeprom_cs = (eeprom_quirk[3:2] == 2'b01) ? (cart_addr[23:19] == 5'b00111) : (eeprom_quirk[2:0] && ((eeprom_bank & ~cart_addr[20]) || !eeprom_quirk[3]) && cart_addr[23:21] == 3'b001);


always_comb begin
	md_eeprom_data = 0;
	casex(eeprom_quirk)
		4'b0001: md_eeprom_data[7] = eeprom_sda;
		4'b0010: md_eeprom_data[0] = eeprom_sda;
		4'b0011: md_eeprom_data[1] = eeprom_sda;
		4'b01xx: md_eeprom_data[7] = eeprom_sda;
		4'b1xxx: md_eeprom_data[0] = eeprom_sda;
		default:;
	endcase
end









always @(posedge clk) begin
	if(reset || !eeprom_quirk) begin
		eeprom_bank <= 0;
		eeprom_sdai <= 1;
		eeprom_scl  <= 1;
	end
	else if(cart_addr[23:21] == 3'b001 && cart_cs && (cart_lwr | cart_uwr)) begin
		casex (eeprom_quirk)
			4'b0001: if(cart_lwr) {eeprom_sdai,eeprom_scl} <= cart_data_wr[7:6];
			4'b0010: if(cart_lwr) {eeprom_scl,eeprom_sdai} <= cart_data_wr[1:0];
			4'b0011: if(cart_lwr) {eeprom_scl,eeprom_sdai} <= cart_data_wr[1:0];
			4'b01xx: if(cart_addr[20:19] == 2'b10) {eeprom_scl,eeprom_sdai} <= cart_data_wr[1:0];
			4'b1xxx:      if (~cart_addr[20] &  cart_lwr & ~cart_uwr) eeprom_sdai <= cart_data_wr[0];
						else if (~cart_addr[20] & ~cart_lwr &  cart_uwr) eeprom_scl  <= cart_data_wr[8];
						else if (~cart_addr[20] &  cart_lwr &  cart_uwr) eeprom_bank <= ~cart_data_wr[0];
		endcase
	end
end

assign eeprom_sda = {eeprom_sdao & eeprom_sdai};

//                                        C01     C01     C02     C16      C65       C08      C04
function [12:0] eeprom_address_mask(input [2:0] kind);
 case(kind)
  1,2:eeprom_address_mask=13'h7f;
  3:eeprom_address_mask=13'hff;
  4:eeprom_address_mask=13'h7ff;
  5:eeprom_address_mask=13'h1fff;
  6:eeprom_address_mask=13'h3ff;
  7:eeprom_address_mask=13'h1ff;
  default:eeprom_address_mask=0;
 endcase
endfunction

multitini_genesis_i2c_eeprom e24cxx (
 .clk(clk),.reset(reset),
 .mode((eeprom_quirk[2:0]<=2) ? 2'd0 : eeprom_quirk[2:0]==5 ? 2'd2 : 2'd1),
 .address_mask(eeprom_address_mask(eeprom_quirk[2:0])),
 .sda_in(eeprom_sdai),.sda_out(eeprom_sdao),.scl(eeprom_scl),
 .ram_addr(eeprom_ram_a),.ram_q(sram_q),.ram_data(eeprom_ram_d),.ram_write(eeprom_ram_we)
);

// PIER EEPROM





multitini_genesis_spi_eeprom pier_eeprom (
 .clk(clk),.reset(reset || !pier_quirk),
 .so(m95_so),.si(ep_si),.sck(ep_sck),.hold_n(ep_hold),.select_n(ep_cs),
 .ram_addr(m95_addr),.ram_q(sram_q),.ram_data(m95_di),.ram_write(m95_rnw)
);

assign pier_prot_cs = pier_quirk && (cart_addr == 'hAF3 || cart_addr == 'hAF4);


always @(posedge clk) begin
	reg [3:0] pier_count;
	reg old_oe;

	old_oe <= cart_oe;
	
	if(reset) pier_count <= 0;
	else if(pier_prot_cs & ~old_oe & cart_oe) begin
		if (pier_count < 6) begin
			pier_count <= pier_count + 1'h1;
			pier_prot_data <= cart_addr[1] ? 16'h0000 : 16'h0010;
		end else begin
			pier_prot_data <= cart_addr[1] ? 16'h0001 : 16'h8010;
		end
	end
end

assign pier_eeprom_cs = pier_quirk && cart_time && cart_addr[3:1] == 'h5;
assign pier_eeprom_data = {15'h7FFF, m95_so};

// MK3U Trilogy 10MB version (13MB isn't compatible with real HW!)
assign cart_cs_ext = ~cart_ms && (cart_addr[23:22] && cart_addr[23:20]<'hA) && (cart_addr < rom_sz);

// Realtec



always @(posedge clk) begin
	if (reset | ~realtec_quirk) begin
		realtec_bank <= 0;
		realtec_mask <= 0;
		realtec_boot <= 1;
	end
	else begin
		if (cart_addr[23:16] == 8'h40 && !cart_addr[11:1] && cart_uwr) begin
			case(cart_addr[15:12])
				4'h0: begin realtec_bank[21:20] <= cart_data_wr[2:1]; realtec_boot <= ~cart_data_wr[0]; end
				4'h2: begin 
					case (cart_data_wr[5:0])
						6'd0,6'd1:                                      realtec_mask <= 5'b00000;
						6'd2:                                           realtec_mask <= 5'b00001;
						6'd3,6'd4:                                      realtec_mask <= 5'b00011;
						6'd5,6'd6,6'd7,6'd8:                            realtec_mask <= 5'b00111;
						6'd9,6'd10,6'd11,6'd12,6'd13,6'd14,6'd15,6'd16: realtec_mask <= 5'b01111;
						default:                                        realtec_mask <= 5'b11111;
					endcase
				end
				4'h4: begin realtec_bank[19:17] <= cart_data_wr[2:0]; end
			endcase
		end
	end
end

assign realtec_addr = realtec_boot ? {11'b00000111111,cart_addr[12:1]} : {2'b00,(cart_addr[21:17] & realtec_mask) + realtec_bank,cart_addr[16:1]};

//SF-001,SF-002,SF-004 mappers
assign sf_cs = sf_quirk && (sf_sram_en | cart_time);
assign sf_data = cart_time ? (sf_quirk[1:0] == 2'd3 ? sf004_do : 16'h0000) : {8'hff,sram_q};







always @(posedge clk) begin
	if(reset || !sf_quirk) begin
		sf001_bank_reg <= 8'h00;
		sf002_bank_reg <= 8'h00;
		sf004_sram_reg <= 0;
		sf004_bank_reg <= 8'h80;
		sf004_first_page <= '0;
	end
	else if(cart_lwr && !cart_addr[23:16]) begin
		
		case(sf_quirk[1:0])

			//sf-001 new rev
			1: if(sf_quirk[2] && !sf001_bank_reg[5] && cart_addr[11:8] == 4'he) sf001_bank_reg <= cart_data_wr[7:0];

			//sf-002
			2: sf002_bank_reg <= cart_data_wr[7:0];

			//sf-004
			3: if (sf004_bank_reg[7]) begin
					case (cart_addr[11:8])
						4'hd: sf004_sram_reg <= cart_data_wr[7];
						4'he: sf004_bank_reg <= cart_data_wr[7:0];
						4'hf: sf004_first_page <= cart_data_wr[6:4];
					endcase
				end
		endcase
	end
end

assign sf001_rom_a = (sf001_bank_reg[7] && !cart_addr[21:18]) ? {6'b001110,cart_addr[17:1]} : {2'b00,cart_addr[21:1]};
assign sf001_sram_en = (cart_addr[23:20] == 4 && !sf_quirk[2]) || (cart_addr[23:18] == 6'b001111 && sf001_bank_reg[7]);

assign sf002_rom_a = {2'b00,cart_addr[21] & ~sf002_bank_reg[7],cart_addr[20:1]};
assign sf002_sram_en = (cart_addr[23:18] == 6'b001111);
						
assign sf004_rom_a = (cart_addr[23:16] < 8'h14 && sf004_bank_reg[6]) ? {3'b000,sf004_first_page + cart_addr[20:18],cart_addr[17:1]} : {3'b000,sf004_first_page,cart_addr[17:1]};
assign sf004_sram_en = (cart_addr[23:20] == 2 && sf004_sram_reg);
assign sf004_do = {8'hff,1'b0,sf004_first_page,4'b0000};

assign sf_sram_en = sf_quirk[1:0] == 1 ? sf001_sram_en :
                            sf_quirk[1:0] == 2 ? sf002_sram_en : 
                                                 sf004_sram_en;

assign sf_rom_addr = sf_quirk[1:0] == 1 ? sf001_rom_a : 
                            sf_quirk[1:0] == 2 ? sf002_rom_a : 
                                                 sf004_rom_a;

assign sf_sram_wr = sf_sram_en & cart_lwr;

//Simple check



always @(posedge clk) begin
	chk_cs <= 0;
	case(chk_quirk)
		1: if(md_addr == 'h400000) begin
				chk_data <= 'h9000;
				chk_cs <= 1;
			end
			else if(md_addr == 'h401000) begin
				chk_data <= 'hd300;
				chk_cs <= 1;
			end

		2: if(md_addr == 'hA13000) begin
				chk_data <= 'h0a;
				chk_cs <= 1;
			end

		3: if(md_addr == 'hA13000) begin
				chk_data <= 'h1c;
				chk_cs <= 1;
			end
	endcase
end

//---------------------- Cart detect ---------------------------------------







always @(posedge clk) begin
	reg [87:0] cart_id;
	reg [15:0] crc = 0;
	reg [15:0] crc_real = 0;
	reg [31:0] realtec_id = 0;
	reg [31:0] sp = 0;
	reg old_dl;
	old_dl <= cart_dl;

	if(~old_dl && cart_dl) begin
		{sram00_quirk,fmbusy_quirk,noram_quirk,pier_quirk,svp_quirk,schan_quirk,eeprom_quirk,realtec_quirk,sf_quirk,chk_quirk} <= '0;
		gun_type <= 0;
		gun_sensor_delay <= 8'd44;
		crc_real <= 0;
		crc <= 0;
	end

	if(cart_dl_wr & cart_dl & ~cart_ms) begin
		if(cart_dl_addr == 'h000) sp[31:16] <= {cart_dl_data[7:0],cart_dl_data[15:8]};
		if(cart_dl_addr == 'h002) sp[15:00] <= {cart_dl_data[7:0],cart_dl_data[15:8]};
		if(cart_dl_addr == 'h180) cart_id[87:72] <= {cart_dl_data[7:0],cart_dl_data[15:8]};
		if(cart_dl_addr == 'h182) cart_id[71:56] <= {cart_dl_data[7:0],cart_dl_data[15:8]};
		if(cart_dl_addr == 'h184) cart_id[55:40] <= {cart_dl_data[7:0],cart_dl_data[15:8]};
		if(cart_dl_addr == 'h186) cart_id[39:24] <= {cart_dl_data[7:0],cart_dl_data[15:8]};
		if(cart_dl_addr == 'h188) cart_id[23:08] <= {cart_dl_data[7:0],cart_dl_data[15:8]};
		if(cart_dl_addr == 'h18A) cart_id[07:00] <= cart_dl_data[7:0];
		if(cart_dl_addr == 'h18E) crc <= {cart_dl_data[7:0],cart_dl_data[15:8]};
		if(cart_dl_addr == 'h190) begin
			     if(cart_id[63:0] == "T-50446 ") eeprom_quirk <= 4'b0001;  // X24C01 John Madden Football 93
			else if(cart_id[63:0] == "T-50516 ") eeprom_quirk <= 4'b0001;  // X24C01 John Madden Football 93 Championship Edition
			else if(cart_id[63:0] == "T-50396 ") eeprom_quirk <= 4'b0001;  // X24C01 NHLPA Hockey 93
			else if(cart_id[63:0] == "T-50176 ") eeprom_quirk <= 4'b0001;  // X24C01 Rings of Power
			else if(cart_id[63:0] == "T-50606 ") eeprom_quirk <= 4'b0001;  // X24C01 Bill Walsh College Football
			else if(cart_id[63:0] == "MK-1215 ") eeprom_quirk <= 4'b0010;  // X24C01 Evander Real Deal Holyfield's Boxing
			else if(cart_id[63:0] == "G-4060  ") eeprom_quirk <= 4'b0010;  // X24C01 Wonder Boy
			else if(cart_id[63:0] == "00001211") eeprom_quirk <= 4'b0010;  // X24C01 Sports Talk Baseball
			else if(cart_id[63:0] == "MK-1228 ") eeprom_quirk <= 4'b0010;  // X24C01 Greatest Heavyweights
			else if(cart_id[63:0] == "G-5538  ") eeprom_quirk <= 4'b0010;  // X24C01 Greatest Heavyweights JP
			else if(cart_id[63:0] == "PR-1993 ") eeprom_quirk <= 4'b0010;  // X24C01 Greatest Heavyweights (prototype)
			else if(cart_id[63:0] == "00004076") eeprom_quirk <= 4'b0010;  // X24C01 Honoo no Toukyuuji Dodge Danpei
			else if(cart_id[63:0] == "T-12046 ") eeprom_quirk <= 4'b0010;  // X24C01 Mega Man - The Wily Wars 
			else if(cart_id[63:0] == "T-12053 ") eeprom_quirk <= 4'b0010;  // X24C01 Rockman Mega World 
			else if(cart_id[63:0] == "G-4524  ") eeprom_quirk <= 4'b0010;  // X24C01 Ninja Burai Densetsu
			else if(cart_id[63:0] == "00054503") eeprom_quirk <= 4'b0010;  // X24C01 Game Toshokan
			else if(cart_id[63:0] == "T-81033 ") eeprom_quirk <= 4'b0011;  // 24C02 NBA Jam (J)
			else if(cart_id[63:0] == "T-081326") eeprom_quirk <= 4'b0011;  // 24C02 NBA Jam (U)(E)
			else if(cart_id[63:0] == "T-081276") eeprom_quirk <= 4'b1011;  // 24C02 NFL Quarterback Club
			else if(cart_id[63:0] == "T-81406 ") eeprom_quirk <= 4'b1111;  // 24C04 NBA Jam TE
			else if(cart_id[63:0] == "T-081586") eeprom_quirk <= 4'b1100;  // 24C16 NFL Quarterback Club '96
			else if(cart_id[63:0] == "T-81576 ") eeprom_quirk <= 4'b1101;  // 24C65 College Slam
			else if(cart_id[63:0] == "T-81476 ") eeprom_quirk <= 4'b1101;  // 24C65 Frank Thomas Big Hurt Baseball
			else if(cart_id[63:0] == "T-120106") eeprom_quirk <= 4'b0110;  // 24C08 Brian Lara Cricket
			else if(sp=="DNLD" && crc == 'h168B) eeprom_quirk <= 4'b0110;  // 24C08 JCART Micro Machines Military
			else if(sp=="DNLD" && crc == 'h165E) eeprom_quirk <= 4'b0100;  // 24C16 JCART Micro Machines Turbo Tournament 96
			else if(cart_id[63:0] == "T-120096") eeprom_quirk <= 4'b0100;  // 24C16 JCART Micro Machines 2 - Turbo Tournament
			else if(cart_id[63:0] == "T-120146") eeprom_quirk <= 4'b0101;  // 24C65 Brian Lara Cricket 96 / Shane Warne Cricket

			else if(cart_id[63:0] == "T-113016") noram_quirk  <= 1;        // Puggsy fake ram check
			else if(cart_id[63:0] == "T-574023") pier_quirk   <= 1;        // Pier Solar Reprint
			else if(cart_id[63:0] == "T-574013") pier_quirk   <= 1;        // Pier Solar 1st Edition
			else if(cart_id[63:0] == "MK-1229 ") svp_quirk    <= 1;        // Virtua Racing EU/US
			else if(cart_id[63:0] == "G-7001  ") svp_quirk    <= 1;        // Virtua Racing JP
			else if(cart_id[63:0] == "T-35036 ") fmbusy_quirk <= 1;        // Hellfire US
			else if(cart_id[63:0] == "T-25073 ") fmbusy_quirk <= 1;        // Hellfire JP
			else if(cart_id[63:0] == "MK-1137-") fmbusy_quirk <= 1;        // Hellfire EU
			else if(cart_id[63:0] == "G-4034  ") fmbusy_quirk <= 1;        // DAISENPU/TWIN HAWK JP/EU
			else if(cart_id[63:0] == "T-44016 ") fmbusy_quirk <= 1;        // Tecmo World Cup
			else if(cart_id[63:0] == "T-44023 ") fmbusy_quirk <= 1;        // Tecmo World Cup JP
			else if(cart_id[63:0] == "T-68???-") schan_quirk  <= 1;        // Game no Kanzume Otokuyou
			else if(cart_id[63:0] == " GM 0000") sram00_quirk <= 1;        // Sonic 1 Remastered
			else if(cart_id[87:40] == "SF-001")  sf_quirk     <= {crc == 16'h3E08,2'b01}; // Beggar Prince (Unl), Beggar Prince rev 1 (Unl)
			else if(cart_id[87:40] == "SF-002")  sf_quirk     <= {1'b1,2'b10}; // Legend of Wukong (Unl)
			else if(cart_id[87:40] == "SF-004")  sf_quirk     <= {1'b1,2'b11}; // Star Odyssey (Unl)

			// Lightgun device and timing offsets
			if(cart_id[63:0] == "MK-1533 ") begin						  // Body Count
				gun_type  <= 0;
				gun_sensor_delay <= 8'd100;
			end
			else if(cart_id[63:0] == "T-95096-") begin				  // Lethal Enforcers
				gun_type  <= 1;
				gun_sensor_delay <= 8'd52;
			end
			else if(cart_id[63:0] == "T-95136-") begin				  // Lethal Enforcers II
				gun_type  <= 1;
				gun_sensor_delay <= 8'd30;
			end
			else if(cart_id[63:0] == "MK-1658 ") begin				  // Menacer 6-in-1
				gun_type  <= 0;
				gun_sensor_delay <= 8'd120;
			end
			else if(cart_id[63:0] == "T-081156") begin				  // T2: The Arcade Game
				gun_type  <= 0;
				gun_sensor_delay <= 8'd126;
			end
		end

		if(cart_dl_addr == 'h7E100) realtec_id[31:16] <= {cart_dl_data[7:0],cart_dl_data[15:8]};
		if(cart_dl_addr == 'h7E102) realtec_id[15: 0] <= {cart_dl_data[7:0],cart_dl_data[15:8]};
		if(cart_dl_addr == 'h7E104 && realtec_id == "SEGA") realtec_quirk <= 1; // Earth Defend, Funny World & Balloon Boy, Whac-a-Critter
		
		if(cart_dl_addr[24:9]) crc_real <= crc_real + {cart_dl_data[7:0],cart_dl_data[15:8]};
		
		if(sf_quirk || svp_quirk || eeprom_quirk || pier_quirk) noram_quirk <= 1;
	end
	
	if(~cart_dl) begin
		if(crc == 'h0000 && crc_real == 'h7037) chk_quirk <= 1; // Ma Jiang Qing Ren - Ji Ma Jiang Zhi
		if(crc == 'h0000 && crc_real == 'h3b95) chk_quirk <= 1; // Super Majon Club
		if(crc == 'hffff && crc_real == 'h0474) chk_quirk <= 2; // Super Mario 2 1998
		if(crc == 'h2020 && crc_real == 'hb4eb) chk_quirk <= 3; // Super Mario World
	end
end

assign ym2612_quirk = fmbusy_quirk;

endmodule
