// Multitini ECS port; reproduced by scripts/port_amiga_core.py.

module cpu_cycles
(
	input             clk,
	input             reset,

	input             ntsc,
	input       [1:0] mode,

	input             opc_start,
	input      [15:0] opc,
	input             opc_cond,
	input      [15:0] opc_snd,
	input             opc_dbx,
	input             cpu_ena,

	input             bus_ack,
	input       [1:0] bus_state,
	input      [31:0] bus_addr,
	input             bus_long,
	input             bus_l1,
	input      [13:0] cacr,
	input             cacr_clr,

	output reg        hold
);
// Strict-tool declaration order; no functional change.
localparam [5:0] K_FS = 6'd0;
localparam [5:0] K_FN = 6'd1;
localparam [5:0] K_IB = 6'd0;
localparam [5:0] K_DR = 6'd0;
localparam [5:0] K_DN = 6'd1;
localparam [5:0] K_DW = 6'd2;
localparam [5:0] K_DB = 6'd4;
reg [13:0] rate;
reg  [5:0] chip_c;
reg  [5:0] rom_c;
wire run;
wire m020;
wire go;
reg [5:0] cyc_rom[0:16383];
reg [4:0] ea_rom[0:2047];
reg [15:0] opc_p, opc_l, opc_q;
reg [15:0] snd_l, snd_q;
reg [19:0] op_q;
reg  [2:0] v;
wire [13:0] cyc_a;
wire movem;
reg [4:0] rn;
integer i;
wire  [7:0] mv_cc;
wire [19:0] movem_e;
wire misc;
reg  [5:0] cyc_i;
reg  [4:0] ea_i;
reg [19:0] cyc_e;
reg [19:0] misc_e;
reg [15:0] ea_e;
wire [19:0] op_e;
wire [2:0] eacl;
wire [1:0] op_t;
wire [4:0] op_h;
wire       divsl;
wire       divsw;
wire [7:0] op_cc;
wire       ea_v;
wire ff_pre;
wire ff;
wire       ff_mi;
wire       ff_od;
wire [1:0] ff_bd;
wire [6:0] ff_cc;
wire       ff_ph;
wire [6:0] ea_cc;
wire [4:0] ea_h;
wire [1:0] ea_t;
wire       ea_ph;
wire       model;
wire bcc_b;
wire bcc_o;
reg  [1:0] prev_t;
wire [4:0] ov1;
wire [8:0] cc;
wire [5:0] head;
wire [5:0] ov2;
wire [8:0] net;
wire [8:0] cost;
reg  [1:0] b_v;
reg  [3:0] b_st;
reg [63:0] b_a;
reg  [1:0] b_lw;
reg  [1:0] b_l1;
reg        e_l1;
reg [27:0] b_cr;
reg  [1:0] b_cc;
reg        e_v;
reg  [1:0] e_st;
reg [31:0] e_a;
reg        e_lw;
wire [13:0] cr_d;
wire        e24;
wire        r_io;
wire        r_chp;
wire        r_rom;
wire        r_fst;
wire  [5:0] c_mem;
wire [23:0] tg;
wire  [3:0] ln;
wire  [1:0] lw;
wire  [5:0] sl;
reg  [23:0] itag[16];
reg  [23:0] dtag[16];
reg  [23:0] ctag[64];
reg  [63:0] ivl, dvl, cvl;
wire  [3:0] i_v;
wire  [3:0] d_v;
wire        c_hit;
reg  [29:0] last_f;
wire        f_ev;
wire        d_ev;
wire        r_cr;
wire        r_nc;
wire        ie;
wire        i_hit;
wire        i_bst;
wire        de;
wire        d_hit;
wire        d_bst;
wire        d_all;
wire        fst3;
wire  [5:0] f_c;
wire signed [7:0] d_c;
reg         clr_d;
reg         bl;
reg   [5:0] ovc;
reg         dbx_d;
reg  [11:0] acc;
reg  [12:0] debt;
reg   [2:0] add_b;
wire [14:0] acc_n;
wire        wr_e;
wire        dr_e;
wire        fm_e;
wire  [6:0] cr_n;
wire [11:0] ab_x;
wire  [5:0] ab;
wire  [8:0] add_t;
wire  [2:0] add_d;
wire signed [13:0] dn;
assign run = |rate;
assign m020 = (mode == 2'd1);
assign go = opc_start & cpu_ena & run;
assign cyc_a = (opc_p[5:3] == 3'b111) ? {opc_p[15:6], 1'b1, opc_p[2:0]}
                                           : {opc_p[15:6], 1'b0, opc_p[5:3]};
assign movem = (opc_l[15:12] == 4'h4) & opc_l[11] & ~opc_l[9] & ~opc_l[8]
           & opc_l[7] & |opc_l[5:3];
assign mv_cc = opc_l[10] ? (8'd8 + {1'b0, rn, 2'b00}) : (8'd4 + {2'b00, rn, 1'b0});
assign movem_e = {1'b1, 3'd4, 2'd0, 5'd2, mv_cc, 1'b0};
assign misc = (opc_l[15:3] == 13'h09CE);
assign op_e = misc ? misc_e : movem ? movem_e : cyc_e;
assign eacl = op_q[18:16];
assign op_t = op_q[15:14];
assign op_h = op_q[13:9];
assign divsl = (opc_q[15:6] == 10'h131) & snd_q[11];
assign divsw = ~m020 & (opc_q[15:12] == 4'h8) & (opc_q[8:6] == 3'b111);
assign op_cc = op_q[8:1] + (divsl ? 8'd12 : 8'd0) - (divsw ? 8'd6 : 8'd0);
assign ea_v = ea_e[15] & |eacl;
assign ff_pre = (opc_q[15:8] == 8'h08)
           || (opc_q[15:12] == 4'hE && opc_q[11] && &opc_q[7:6]);
assign ff = ea_v & eacl[0] & snd_q[8] & ~ff_pre
        & ((opc_q[5:3] == 3'b110) | (opc_q[5:0] == 6'b111011));
assign ff_mi = |snd_q[1:0];
assign ff_od = ff_mi & (snd_q[1:0] != 2'b01);
assign ff_bd = snd_q[5:4];
assign ff_cc = (ff_mi ? (ff_bd == 2'b10 ? 7'd12 : ff_bd == 2'b11 ? 7'd16 : 7'd10)
                          : (ff_bd == 2'b10 ? 7'd8  : ff_bd == 2'b11 ? 7'd12 : 7'd6))
                 + (ff_od ? 7'd2 : 7'd0);
assign ff_ph = ~ff_mi & (ff_bd != 2'b10) & (ff_bd != 2'b11) & (eacl != 3'd1);
assign ea_cc = ff ? ff_cc : ea_v ? ea_e[14:8] : 7'd0;
assign ea_h = ff ? (ff_ph ? 5'd6 : 5'd4) : ea_v ? ea_e[7:3] : 5'd0;
assign ea_t = ff ? 2'd0 : ea_v ? ea_e[2:1] : 2'd0;
assign ea_ph = ff ? ff_ph : (ea_v & ea_e[0]);
assign model = op_q[19] & (~|eacl | ea_e[15]);
assign bcc_b = (opc_q[15:12] == 4'h6) && (opc_q[11:8] > 4'h1)
          && (opc_q[7:0] != 8'h00) && (opc_q[7:0] != 8'hFF);
assign bcc_o = (opc[15:12] == 4'h6) && (opc[11:8] > 4'h1)
          && (opc[7:0] != 8'h00) && (opc[7:0] != 8'hFF);
assign ov1 = (op_h < {3'b0, ea_t}) ? op_h : {3'b0, ea_t};
assign cc = {1'b0, op_cc} + {2'b0, ea_cc} - {4'b0, ov1};
assign head = ea_v ? ({1'b0, ea_h} + (ea_ph ? {1'b0, op_h} : 6'd0)) : {1'b0, op_h};
assign ov2 = (head < {4'b0, prev_t}) ? head : {4'b0, prev_t};
assign net = cc - {3'b0, ov2};
assign cost = model ? (bcc_b ? net - 9'd2 : net) : 9'd2;
assign cr_d = b_cr[27:14];
assign e24 = m020 | ~|e_a[31:24];
assign r_io = e24 & (e_a[23:19] == 5'b11101);
assign r_chp = e24 & ((e_a[23:21] == 3'd0) | (e_a[23:21] == 3'd5) | (e_a[23:21] == 3'd6) | r_io);
assign r_rom = e24 & (e_a[23:21] == 3'd7) & ~r_io;
assign r_fst = ~r_chp & ~r_rom;
assign c_mem = r_chp ? chip_c : (~m020 & r_rom) ? rom_c : m020 ? 6'd3 : 6'd2;
assign tg = e_a[31:8];
assign ln = e_a[7:4];
assign lw = e_a[3:2];
assign sl = e_a[7:2];
assign i_v = (itag[ln] == tg) ? ivl[{ln, 2'b00} +: 4] : 4'd0;
assign d_v = (dtag[ln] == tg) ? dvl[{ln, 2'b00} +: 4] : 4'd0;
assign c_hit = cvl[sl] & (ctag[sl] == tg);
assign f_ev = e_v & (e_st == 2'd0) & (e_a[31:2] != last_f);
assign d_ev = e_v & e_st[1] & (~e_lw | e_l1);
assign r_cr = e24 & ((e_a[23:21] == 3'd0) | ((e_a[23:21] == 3'd6) & ~&e_a[20:19]));
assign r_nc = r_chp & ~r_cr;
assign ie = cr_d[0] & ~cr_d[1] & ~r_nc;
assign i_hit = cr_d[0] & (m020 ? c_hit : (i_v[lw] & ~r_nc));
assign i_bst = ~m020 & ie & cr_d[4] & ~r_chp & ~|lw & ~|i_v;
assign de = ~m020 & cr_d[8] & ~r_chp;
assign d_hit = de & d_v[lw] & ~e_st[0];
assign d_bst = de & ~cr_d[9] & cr_d[12] & ~|lw & ~|d_v & ~e_st[0];
assign d_all = de & ~cr_d[9] & (~e_st[0] | cr_d[13]) & ~d_hit;
assign fst3 = ~m020 & r_fst;
assign f_c = (~f_ev | i_hit) ? 6'd0
                : i_bst ? (r_fst ? K_IB : 6'd5)
                : fst3  ? (cr_d[0] ? K_FS : K_FN) : c_mem;
assign d_c = ~d_ev  ? 8'sd0
                      : d_hit  ? -8'sd1
                      : d_bst  ? (r_fst ? $signed({2'd0, K_DB}) : 8'sd3)
                      : r_chp  ? $signed({2'd0, c_mem}) - 8'sd2
                      : (~m020 & r_rom) ? $signed({2'd0, c_mem})
                      : m020   ? $signed({2'd0, c_mem}) - 8'sd2
                      : e_st[0] ? $signed({2'd0, K_DW}) : $signed({2'd0, cr_d[8] ? K_DR : K_DN});
assign acc_n = {3'b000, acc} + {1'b0, rate};
assign wr_e = d_ev & e_st[0] & ~d_c[7];
assign dr_e = d_ev & ~e_st[0] & ~d_hit;
assign fm_e = f_ev & ~i_hit;
assign cr_n = (wr_e ? d_c[6:0] : 7'd0) + (fm_e ? {1'b0, f_c} : 7'd0);
assign ab_x = {cost, 3'd0} - {3'd0, cost};
assign ab = (m020 & v[2]) ? ((ab_x[11:3] > {3'd0, ovc}) ? ovc : ab_x[8:3]) : 6'd0;
assign add_t = v[2] ? cost - {3'd0, ab} : 9'd0;
assign add_d = (opc_dbx & ~dbx_d & run) ? 3'd4 : 3'd0;
assign dn = {debt[12], debt} - {11'd0, acc_n[14:12]} + {5'd0, add_t} + {8'd0, f_c} + {{6{d_c[7]}}, d_c}
                      + {11'd0, add_b} + {11'd0, add_d};





always @* begin
	rate = 14'd0;
	chip_c = 6'd0;
	rom_c = 6'd0;
	if (mode == 2'd1) begin
		rate = 14'd2048;
		chip_c = 6'd9;
	end
	else if (mode == 2'd2) begin
		rate = ntsc ? 14'd5721 : 14'd5774;
		chip_c = 6'd21;
		rom_c = 6'd7;
	end
	else if (mode == 2'd3) begin
		rate = ntsc ? 14'd7152 : 14'd7218;
		chip_c = 6'd27;
		rom_c = 6'd8;
	end
end






initial $readmemh("m68k_cyc_idx.hex", cyc_rom);

initial $readmemh("m68k_ea_idx.hex", ea_rom);












always @* begin
	rn = 5'd0;
	for (i = 0; i < 16; i = i + 1) rn = rn + {4'd0, snd_l[i]};
end











`include "tg68k/m68k_cyc_pal.vh"
`include "tg68k/m68k_ea_pal.vh"
`include "tg68k/m68k_misc_030.vh"



always @(posedge clk) begin
	if (~reset | ~run) v <= 0;
	else v <= {v[1:0], go};
	if (go) opc_p <= opc;
	opc_l <= opc_p;
	snd_l <= opc_snd;
	cyc_i <= cyc_rom[cyc_a];
	opc_q <= opc_l;
	snd_q <= snd_l;
	op_q  <= op_e;
	ea_i  <= ea_rom[{op_e[18:16], movem ? 2'b01 : opc_l[7:6], opc_l[5:0]}];
end










































always @(posedge clk) begin
	b_l1 <= {b_l1[0], bus_l1};
	e_l1 <= b_l1[1];
end






always @(posedge clk) begin
	b_v  <= {b_v[0], bus_ack & run};
	b_st <= {b_st[1:0], bus_state};
	b_a  <= {b_a[31:0], bus_addr};
	b_lw <= {b_lw[0], bus_long};
	b_cr <= {b_cr[13:0], cacr};
	b_cc <= {b_cc[0], cacr_clr};
	e_v  <= b_v[1];
	e_st <= b_st[3:2];
	e_a  <= b_a[63:32];
	e_lw <= b_lw[1];
end
















































always @(posedge clk) begin
	clr_d <= b_cc[1];
	if (~reset | (b_cc[1] & ~clr_d)) begin
		ivl <= 0;
		dvl <= 0;
		cvl <= 0;
		last_f <= 30'h3FFFFFFF;
	end
	else begin
		if (f_ev) last_f <= e_a[31:2];
		if (f_ev & ~i_hit & ie) begin
			if (m020) begin
				ctag[sl] <= tg;
				cvl[sl] <= 1'b1;
			end
			else begin
				itag[ln] <= tg;
				ivl[{ln, 2'b00} +: 4] <= i_bst ? 4'hF : (i_v | (4'd1 << lw));
			end
		end
		if (d_ev & d_all) begin
			dtag[ln] <= tg;
			dvl[{ln, 2'b00} +: 4] <= d_bst ? 4'hF : (d_v | (4'd1 << lw));
		end
	end
end



















always @(posedge clk) begin
	dbx_d <= opc_dbx;
	add_b <= (go & bl & opc_cond) ? 3'd2 : 3'd0;
	if (~reset | ~run) begin
		acc    <= 0;
		ovc    <= 0;
		debt   <= 0;
		prev_t <= 0;
		bl     <= 0;
		hold   <= 0;
	end
	else begin
		acc  <= acc_n[11:0];
		ovc  <= (~m020 | dr_e) ? 6'd0 : |cr_n ? (cr_n[6] ? 6'd63 : cr_n[5:0]) : ovc - ab;
		debt <= (dn < -14'sd255) ? -13'sd255 : dn[12:0];
		hold <= (dn > 14'sd0);
		if (go) bl <= bcc_o;
		if (v[2]) prev_t <= model ? op_t : 2'd0;
	end
end

endmodule
