-- -----------------------------------------------------------------------
--
--                                 FPGA 64
--
--     A fully functional commodore 64 implementation in a single FPGA
--
-- -----------------------------------------------------------------------
-- Peter Wendrich (pwsoft@syntiac.com)
-- http://www.syntiac.com/fpga64.html
-- -----------------------------------------------------------------------
--
-- System runs on 32 Mhz
-- The VIC-II runs in 4 cycles of first 16 cycles.
-- The CPU runs in the last 16 cycles. Effective cpu speed is 1 Mhz.
--
-- -----------------------------------------------------------------------
-- Dar 08/03/2014
--
-- Based on fpga64_cone
-- add external selection for 15KHz(TV)/31KHz(VGA)
-- add external selection for power on NTSC(60Hz)/PAL(50Hz)
-- add external conection in/out for IEC signal
-- add sid entity
-- -----------------------------------------------------------------------
--
-- Alexey Melnikov 2021
--
-- add dma engine
-- implement up to 4x turbo of C128 and smart types.
-- add user port signals
-- various fixes and tweaks
--
-- -----------------------------------------------------------------------


library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.std_logic_unsigned.ALL;
use IEEE.numeric_std.all;

-- -----------------------------------------------------------------------

entity fpga64_sid_iec is
port(
	clk32       : in  std_logic;
	reset_n     : in  std_logic;
	bios        : in  std_logic_vector(1 downto 0);

	pause       : in  std_logic := '0';
	-- Multitini local-port extension: stretch the active bus phase while
	-- cartridge data is fetched from external PSRAM.
	memory_wait : in  std_logic := '0';
	pause_out   : out std_logic;

	-- keyboard interface (use any ordinairy PS2 keyboard)
	ps2_key     : in  std_logic_vector(10 downto 0);
	kbd_reset   : in  std_logic := '0';
	shift_mod   : in  std_logic_vector(1 downto 0);

	-- external memory
	ramAddr     : out unsigned(15 downto 0);
	ramDin      : in  unsigned(7 downto 0);
	ramDout     : out unsigned(7 downto 0);
	ramCE       : out std_logic;
	ramWE       : out std_logic;

	io_cycle    : out std_logic;
	ext_cycle   : out std_logic;
	refresh     : out std_logic;

	cia_mode    : in  std_logic;
	turbo_mode  : in  std_logic_vector(1 downto 0);
	turbo_speed : in  std_logic_vector(1 downto 0);
	-- Extra CPU transactions require deterministic local memory. The adapter
	-- clears this permission while an external cartridge is installed.
	local_turbo_allowed : in std_logic := '0';

	-- VGA/SCART interface
	vic_variant : in  std_logic_vector(1 downto 0);
	ntscMode    : in  std_logic;
	hsync       : out std_logic;
	vsync       : out std_logic;
	palette     : in  unsigned(2 downto 0);
	r           : out unsigned(7 downto 0);
	g           : out unsigned(7 downto 0);
	b           : out unsigned(7 downto 0);

	-- cartridge port
	game        : in  std_logic;
	exrom       : in  std_logic;
	io_rom      : in  std_logic;
	io_ext      : in  std_logic;
	io_data     : in  unsigned(7 downto 0);
	irq_n       : in  std_logic;
	nmi_n       : in  std_logic;
	nmi_ack     : out std_logic;
	romL        : out std_logic;
	romH        : out std_logic;
	UMAXromH 	: out std_logic;
	IOE			: out std_logic;
	IOF			: out std_logic;
	freeze_key  : out std_logic;
	mod_key     : out std_logic;
	tape_play   : out std_logic;
	tape_rew    : out std_logic;
	tape_stop   : out std_logic;
	tape_ff     : out std_logic;
	tape_reset_counter : out std_logic;

	-- dma access
	dma_req     : in  std_logic := '0';
	-- A memory-backed DMA master may own the CPU while waiting for its store.
	-- Only valid slots touch the machine bus; native device clocks keep running.
	dma_valid   : in  std_logic := '1';
	-- A REC verify hold consumes native time without a C64 memory access.
	dma_idle    : in  std_logic := '0';
	dma_peek    : in  std_logic := '0';
	dma_last_write : in std_logic := '0';
	dma_ack     : out std_logic;
	cpu_bus_valid : out std_logic;
	cpu_bus_addr : out unsigned(15 downto 0);
	cpu_bus_wdata : out unsigned(7 downto 0);
	cpu_bus_write : out std_logic;
	dma_cycle   : out std_logic;
	dma_addr    : in  unsigned(15 downto 0) := (others => '0');
	dma_dout    : in  unsigned(7 downto 0) := (others => '0');
	dma_din     : out unsigned(7 downto 0);
	dma_we      : in  std_logic := '0';
	irq_ext_n   : in  std_logic := '1';

	-- joystick interface
	joyA        : in  std_logic_vector(6 downto 0);
	joyB        : in  std_logic_vector(6 downto 0);
	pot1        : in  std_logic_vector(7 downto 0);
	pot2        : in  std_logic_vector(7 downto 0);
	pot3        : in  std_logic_vector(7 downto 0);
	pot4        : in  std_logic_vector(7 downto 0);

	--SID
	audio_l     : out std_logic_vector(17 downto 0);
	audio_r     : out std_logic_vector(17 downto 0);
	sid_filter  : in  std_logic_vector(1 downto 0);
	sid_ver     : in  std_logic_vector(1 downto 0);
	sid_mode    : in  unsigned(2 downto 0);
	sid_cfg     : in  std_logic_vector(3 downto 0);
	sid_fc_off_l: in  std_logic_vector(12 downto 0);
	sid_fc_off_r: in  std_logic_vector(12 downto 0);
	sid_ld_clk  : in  std_logic;
	sid_ld_addr : in  std_logic_vector(11 downto 0);
	sid_ld_data : in  std_logic_vector(15 downto 0);
	sid_ld_wr   : in  std_logic;
	sid_digifix : in  std_logic;

	-- USER
	pb_i        : in  unsigned(7 downto 0);
	pb_o        : out unsigned(7 downto 0);
	-- Multitini local-port extension: CIA2 port B data direction, so an
	-- attached user-port device can tell a driven-low line from a released
	-- input. The pinned source exports only the output register.
	pb_oe_o     : out unsigned(7 downto 0);
	pa2_i       : in  std_logic;
	pa2_o       : out std_logic;
	pc2_n_o     : out std_logic;
	flag2_n_i   : in  std_logic;
	sp2_i       : in  std_logic;
	sp2_o       : out std_logic;
	sp1_i       : in  std_logic;
	sp1_o       : out std_logic;
	cnt2_i      : in  std_logic;
	cnt2_o      : out std_logic;
	cnt1_i      : in  std_logic;
	cnt1_o      : out std_logic;

	-- IEC
	iec_data_o	: out std_logic;
	iec_data_i	: in  std_logic;
	iec_clk_o	: out std_logic;
	iec_clk_i	: in  std_logic;
	iec_atn_o	: out std_logic;

	c64rom_addr : in  std_logic_vector(13 downto 0);
	c64rom_data : in  std_logic_vector(7 downto 0);
	c64rom_wr   : in  std_logic;

	cass_motor  : out std_logic;
	cass_write  : out std_logic;
	cass_sense  : in  std_logic;
	cass_read   : in  std_logic
);
end fpga64_sid_iec;

-- -----------------------------------------------------------------------

architecture rtl of fpga64_sid_iec is
-- System state machine
type sysCycleDef is (
	CYCLE_EXT0, CYCLE_EXT1, CYCLE_EXT2, CYCLE_EXT3,
	CYCLE_DMA0, CYCLE_DMA1, CYCLE_DMA2, CYCLE_DMA3,
	CYCLE_EXT4, CYCLE_EXT5, CYCLE_EXT6, CYCLE_EXT7,
	CYCLE_VIC0, CYCLE_VIC1, CYCLE_VIC2, CYCLE_VIC3,
	CYCLE_CPU0, CYCLE_CPU1, CYCLE_CPU2, CYCLE_CPU3,
	CYCLE_CPU4, CYCLE_CPU5, CYCLE_CPU6, CYCLE_CPU7,
	CYCLE_CPU8, CYCLE_CPU9, CYCLE_CPUA, CYCLE_CPUB,
	CYCLE_CPUC, CYCLE_CPUD, CYCLE_CPUE, CYCLE_CPUF
);

signal sysCycle     : sysCycleDef := sysCycleDef'low;
signal preCycle     : sysCycleDef := sysCycleDef'low;
signal sysEnable    : std_logic := '0';
-- Delaying memory_wait by one clock lets VIC0/CPUC capture the request once,
-- then holds VIC1/CPUD until the external-memory response is cached.
signal memory_wait_q: std_logic := '0';
-- Match FPGA power-on state explicitly. Without this initialization the
-- pause/refresh boundary never becomes known in a four-state simulation.
signal rfsh_cycle   : unsigned(1 downto 0) := (others => '0');

signal dma_active   : std_logic;
signal dma_slot_valid : std_logic := '0';
signal dma_slot_idle : std_logic := '0';
signal dma_slot_peek : std_logic := '0';
signal dma_peek_done : std_logic := '0';
signal dma_peek_issue, dma_peek_retire, dma_peek_io : std_logic;
signal dma_slot_last_write : std_logic := '0';
signal dma_post_pending : std_logic := '0';
signal dma_post_write : std_logic := '0';
signal dma_post_last_write : std_logic := '0';
signal dma_write_delay : std_logic := '0';
signal dma_wait_vic : std_logic := '0';
signal dma_terminal_extra : std_logic := '0';
signal dma_started : std_logic := '0';
signal bus_access_valid : std_logic;

signal phi0_cpu     : std_logic;
signal cpuHasBus    : std_logic;

signal baLoc        : std_logic;
signal ba_dma       : std_logic;
signal ba_reu       : std_logic;
signal aec          : std_logic;

signal enableCpu    : std_logic;
signal scheduledCpuRequest, scheduledCpuRetire : std_logic;
signal enableVic    : std_logic;
signal enablePixel  : std_logic;
signal enableSid    : std_logic;

signal irq_cia1     : std_logic;
signal irq_cia2     : std_logic;
signal irq_vic      : std_logic;

signal systemWe     : std_logic;
signal pulseWr_io   : std_logic;
signal systemAddr   : unsigned(15 downto 0);

signal cs_io        : std_logic;
signal cs_vic       : std_logic;
signal cs_sid       : std_logic;
signal cs_color     : std_logic;
signal cs_cia1      : std_logic;
signal cs_cia2      : std_logic;
signal cs_ram       : std_logic;
signal cpuWe        : std_logic;
signal cpuWe_pre    : std_logic;
signal cpuAddr      : unsigned(15 downto 0);
signal cpuAddr_pre  : unsigned(15 downto 0);
signal cpuDi        : unsigned(7 downto 0);
signal cpuDo        : unsigned(7 downto 0);
signal cpuDo_pre    : unsigned(7 downto 0);
signal cpuIO        : unsigned(7 downto 0);
signal io_data_i    : unsigned(7 downto 0);
signal ioe_i        : std_logic;
signal iof_i        : std_logic;

signal io_enable    : std_logic;
signal cpu_cyc      : std_logic;
signal turbo_m      : std_logic_vector(2 downto 0);
signal turbo_dense  : std_logic := '0';
signal cpu_extra_allowed : std_logic;

signal reset        : std_logic := '1';

-- CIA signals
signal enableCia_p  : std_logic;
signal enableCia_n  : std_logic;
signal cia1Do       : unsigned(7 downto 0);
signal cia2Do       : unsigned(7 downto 0);
signal cia1_pai     : unsigned(7 downto 0);
signal cia1_pao     : unsigned(7 downto 0);
signal cia1_pbi     : unsigned(7 downto 0);
signal cia1_pbo     : unsigned(7 downto 0);
signal cia2_pai     : unsigned(7 downto 0);
signal cia2_pao     : unsigned(7 downto 0);
signal cia2_pbi     : unsigned(7 downto 0);
signal cia2_pbo     : unsigned(7 downto 0);
signal cia2_pbe     : unsigned(7 downto 0);
signal cia2_pae     : unsigned(7 downto 0);

signal todclk       : std_logic;

-- video
signal vicColorIndex: unsigned(3 downto 0);
signal vicBus       : unsigned(7 downto 0);
signal vicDi        : unsigned(7 downto 0);
signal vicDiAec     : unsigned(7 downto 0);
signal vicAddr      : unsigned(15 downto 0);
signal vicData      : unsigned(7 downto 0);
signal lastVicDi    : unsigned(7 downto 0);
signal vicAddr1514  : unsigned(1 downto 0);
signal colorData    : unsigned(3 downto 0);
signal colorDataAec : unsigned(3 downto 0);
signal turbo_en     : std_logic;
signal turbo_state  : std_logic;

-- SID signals
signal sid_do       : unsigned(7 downto 0);
signal sid_sel_l    : std_logic;
signal sid_sel_r    : std_logic;
signal pot_x1       : std_logic_vector(7 downto 0);
signal pot_y1       : std_logic_vector(7 downto 0);
signal pot_x2       : std_logic_vector(7 downto 0);
signal pot_y2       : std_logic_vector(7 downto 0);

component sid_top
	port (
		reset         : in  std_logic;
		clk           : in  std_logic;
		ce_1m         : in  std_logic;

		cs            : in  std_logic_vector(1 downto 0);
		we            : in  std_logic;
		addr          : in  unsigned(4 downto 0);
		data_in       : in  unsigned(7 downto 0);
		data_out      : out unsigned(7 downto 0);

		pot_x_l       : in  std_logic_vector(7 downto 0) := (others => '0');
		pot_y_l       : in  std_logic_vector(7 downto 0) := (others => '0');
		pot_x_r       : in  std_logic_vector(7 downto 0) := (others => '0');
		pot_y_r       : in  std_logic_vector(7 downto 0) := (others => '0');

		audio_l       : out std_logic_vector(17 downto 0);
		audio_r       : out std_logic_vector(17 downto 0);

		ext_in_l      : in  std_logic_vector(17 downto 0);
		ext_in_r      : in  std_logic_vector(17 downto 0);

		fc_offset_l   : in  std_logic_vector(12 downto 0);
		fc_offset_r   : in  std_logic_vector(12 downto 0);

		filter_en     : in  std_logic_vector(1 downto 0);
		mode          : in  std_logic_vector(1 downto 0);
		cfg           : in  std_logic_vector(3 downto 0);

		ld_clk        : in  std_logic;
		ld_addr       : in  std_logic_vector(11 downto 0);
		ld_data       : in  std_logic_vector(15 downto 0);
		ld_wr         : in  std_logic
  );
end component;

component mos6526
	PORT (
		clk           : in  std_logic;
		mode          : in  std_logic := '0'; -- 0 - 6526 "old", 1 - 8521 "new"
		phi2_p        : in  std_logic;
		phi2_n        : in  std_logic;
		res_n         : in  std_logic;
		cs_n          : in  std_logic;
		rw            : in  std_logic; -- '1' - read, '0' - write
		rs            : in  unsigned(3 downto 0);
		db_in         : in  unsigned(7 downto 0);
		db_out        : out unsigned(7 downto 0);
		pa_in         : in  unsigned(7 downto 0);
		pa_out        : out unsigned(7 downto 0);
		pa_oe         : out unsigned(7 downto 0);
		pb_in         : in  unsigned(7 downto 0);
		pb_out        : out unsigned(7 downto 0);
		pb_oe         : out unsigned(7 downto 0);
		flag_n        : in  std_logic;
		pc_n          : out std_logic;
		tod           : in  std_logic;
		sp_in         : in  std_logic;
		sp_out        : out std_logic;
		cnt_in        : in  std_logic;
		cnt_out       : out std_logic;
		irq_n         : out std_logic
	);
end component;

begin

-- -----------------------------------------------------------------------
-- Local signal to outside world
-- -----------------------------------------------------------------------

io_cycle <= '1' when
	(sysCycle >= CYCLE_EXT0 and sysCycle <= CYCLE_EXT3) or
	(sysCycle >= CYCLE_EXT4 and sysCycle <= CYCLE_EXT7 and rfsh_cycle /= "00")  else '0';

-- -----------------------------------------------------------------------
-- System state machine, controls bus accesses
-- and triggers enables of other components
-- -----------------------------------------------------------------------

sysCycle <= preCycle when sysEnable = '1' else CYCLE_EXT4;
pause_out <= not sysEnable;

process(clk32)
begin
	if rising_edge(clk32) then
		memory_wait_q <= memory_wait;

		-- A cartridge request first appears during VIC0 or CPUC. The registered
		-- wait therefore allows that strobe to complete and freezes the next
		-- phase, before either VIC3 or CPUF can consume ramDin.
		if memory_wait_q = '0' then
			preCycle <= sysCycleDef'succ(preCycle);
			if preCycle = sysCycleDef'high then
				preCycle <= sysCycleDef'low;
				if sysEnable = '1' then
					rfsh_cycle <= rfsh_cycle + 1;
				end if;
			end if;

			refresh <= '0';
			if preCycle = sysCycleDef'pred(CYCLE_EXT4) and rfsh_cycle = "00" then
				sysEnable <= not pause;
				refresh <= '1';
			end if;
		end if;
	end if;
end process;

process(clk32)
begin
	if rising_edge(clk32) then
		if preCycle = sysCycleDef'high then
			reset <= not reset_n;
		end if;
	end if;
end process;

-- PHI0/2-clock emulation
process(clk32)
variable blocked : std_logic;
begin
	if rising_edge(clk32) then
		if reset = '1' then
			phi0_cpu <= '0';
			cpuHasBus <= '0';
			dma_active <= '0';
			dma_slot_valid <= '0';
			dma_slot_idle <= '0';
			dma_slot_peek <= '0';
			dma_peek_done <= '0';
			dma_slot_last_write <= '0';
			dma_post_pending <= '0';
			dma_post_write <= '0';
			dma_post_last_write <= '0';
			dma_write_delay <= '0';
			dma_wait_vic <= '0';
			dma_terminal_extra <= '0';
			dma_started <= '0';
		elsif memory_wait_q = '0' then
			-- Keep the CPU held until the final access's post-cycle BA check.
			if sysCycle = CYCLE_EXT1 or sysCycle = CYCLE_EXT5 then
				dma_active <= dma_req or dma_post_pending or dma_wait_vic;
			end if;
			if sysCycle = sysCycleDef'pred(CYCLE_CPU0) then
				phi0_cpu <= '1';
				dma_slot_valid <= dma_valid;
				dma_slot_idle <= dma_idle;
				dma_slot_peek <= dma_peek;
				dma_slot_last_write <= dma_last_write;
				blocked := '0';
				-- VICE reu_clk_inc_post_write/read check the next native
				-- cycle after an accepted access. Writes get one BA check
				-- of grace; reads wait immediately. A read does not reset
				-- the write-delay latch (important for swap).
				if dma_wait_vic = '1' then
					if ba_reu = '0' then
						blocked := '1';
					else
						dma_wait_vic <= '0';
						if dma_terminal_extra = '1' then
							-- A final write that caused a BA steal has one
							-- additional read-like clock before CPU release.
							blocked := '1';
							dma_terminal_extra <= '0';
							dma_post_pending <= '1';
							dma_post_write <= '0';
						end if;
					end if;
				elsif dma_post_pending = '1' then
					dma_post_pending <= '0';
					if dma_post_write = '1' then
						if ba_reu = '1' then
							dma_write_delay <= '0';
						elsif dma_write_delay = '0' then
							dma_write_delay <= '1';
						else
							blocked := '1';
							dma_write_delay <= '0';
							dma_wait_vic <= '1';
							dma_terminal_extra <= dma_post_last_write;
						end if;
					elsif ba_reu = '0' then
						blocked := '1';
						dma_wait_vic <= '1';
					end if;
				elsif dma_req = '1' and
					((dma_started = '0' and baLoc = '0') or
					 (dma_started = '1' and ba_reu = '0')) then
					-- Initial CPU takeover honors normal VIC BA. A backend
					-- gap cannot manufacture another write-grace cycle.
					blocked := '1';
					dma_wait_vic <= '1';
				end if;
				cpuHasBus <= '0';
				if blocked = '1' then
					dma_active <= '1';
				elsif dma_req = '1' then
					dma_active <= '1';
					dma_started <= '1';
					cpuHasBus <= '1';
				else
					dma_active <= '0';
					dma_started <= '0';
					dma_write_delay <= '0';
					dma_peek_done <= '0';
					if baLoc = '1' or cpuWe_pre = '1' then
						cpuHasBus <= '1';
					end if;
				end if;
			end if;
			if enableCpu = '1' and dma_active = '1' and dma_slot_peek = '0' and
				dma_slot_valid = '1' and cpuHasBus = '1' then
				dma_post_pending <= '1';
				dma_post_write <= dma_we;
				dma_post_last_write <= dma_slot_last_write;
			end if;
			if dma_peek_retire = '1' then
				dma_peek_done <= '1';
			end if;
			-- The penultimate-error comparison is unclocked in VICE. Serve
			-- it in the first quarter of this half-frame, then give the CPU
			-- its normal CPUC/CPUF slot after comparison/status complete.
			if dma_peek_done = '1' and dma_req = '0' and
				sysCycle >= CYCLE_CPU0 and sysCycle < CYCLE_CPUC then
				dma_active <= '0';
				dma_started <= '0';
				dma_peek_done <= '0';
				dma_write_delay <= '0';
				cpuHasBus <= baLoc or cpuWe_pre;
			end if;
			if sysCycle = sysCycleDef'high then
				phi0_cpu <= '0';
				cpuHasBus <= '0';
			end if;
		end if;
	end if;
end process;

process(clk32)
begin
	if rising_edge(clk32) then
		if memory_wait_q = '0' then
			enableVic <= '0';
			enableCia_n <= '0';
			enableCia_p <= '0';
			enableSid <= '0';

			case sysCycle is
			when CYCLE_VIC2 =>
				enableVic <= '1';
			when CYCLE_CPUE =>
				enableVic <= '1';
			when CYCLE_CPUC =>
				enableCia_n <= '1';
			when CYCLE_CPUF =>
				enableCia_p <= '1';
				enableSid <= '1';
			when others =>
				null;
			end case;
		end if;
	end if;
end process;

-- -----------------------------------------------------------------------
-- Color RAM
-- -----------------------------------------------------------------------
colorram: entity work.spram
generic map (
	DATA_WIDTH => 4,
	ADDR_WIDTH => 10
)
port map (
	clk => clk32,
	we => cs_color and pulseWr_io,
	addr => systemAddr(9 downto 0),
	data => cpuDo(3 downto 0),
	q => colorData
);

-- -----------------------------------------------------------------------
-- PLA and bus-switches
-- -----------------------------------------------------------------------
buslogic: entity work.fpga64_buslogic
port map (
	clk => clk32,
	reset => reset,
	bios => bios,

	cpuHasBus => cpuHasBus,
	aec => aec,

	bankSwitch => cpuIO(2 downto 0),

	game => game,
	exrom => exrom,
	io_rom => io_rom,
	io_ext => io_ext or sid_sel_r,
	io_data => io_data_i,

	ramData => ramDin,

	cpuWe => cpuWe,
	cpuAddr => cpuAddr,
	cpuData => cpuDo,
	vicAddr => vicAddr,
	vicData => vicData,
	sidData => sid_do,
	colorData => colorData,
	cia1Data => cia1Do,
	cia2Data => cia2Do,
	lastVicData => lastVicDi,

	systemWe => systemWe,
	systemAddr => systemAddr,
	dataToCpu => cpuDi,
	dataToVic => vicDi,

	io_enable => io_enable and bus_access_valid,

	cs_vic => cs_vic,
	cs_sid => cs_sid,
	cs_color => cs_color,
	cs_cia1 => cs_cia1,
	cs_cia2 => cs_cia2,
	cs_ram => cs_ram,
	cs_ioE => ioe_i,
	cs_ioF => iof_i,
	cs_romL => romL,
	cs_romH => romH,
	cs_UMAXromH => UMAXromH,

	c64rom_addr => c64rom_addr,
	c64rom_data => c64rom_data,
	c64rom_wr => c64rom_wr
);

IOE <= ioe_i;
IOF <= iof_i;
cs_io <= cs_vic or cs_sid or cs_color or cs_cia1 or cs_cia2 or ioe_i or iof_i;

process(clk32)
begin
	if rising_edge(clk32) then
		pulseWr_io <= '0';
		if cpuWe = '1' then
			if sysCycle = CYCLE_CPUC and cpu_cyc = '1' and bus_access_valid = '1' then
				pulseWr_io <= '1';
			end if;
		end if;
	end if;
end process;

-- -----------------------------------------------------------------------
-- VIC-II video interface chip
-- -----------------------------------------------------------------------
process(clk32)
begin
	if rising_edge(clk32) then
		if phi0_cpu = '1' then
			if cpuWe = '1' and cs_vic = '1' then
				vicBus <= cpuDo;
			else
				vicBus <= x"FF";
			end if;
		end if;
	end if;
end process;

-- In the first three cycles after BA went low, the VIC reads
-- $ff as character pointers and
-- as color information the lower 4 bits of the opcode after the access to $d011.
vicDiAec <= vicBus when aec = '0' else vicDi;
colorDataAec <= cpuDi(3 downto 0) when aec = '0' else colorData;

vic: entity work.video_vicii_656x
generic map (
	registeredAddress => true,
	emulateRefresh => true,
	emulateLightpen => true,
	emulateGraphics => true
)
port map (
	clk => clk32,
	reset => reset,
	enaPixel => enablePixel and not memory_wait_q,
	enaData => enableVic and not memory_wait_q,
	phi => phi0_cpu,

	baSync => '0',
	ba => baLoc,
	ba_dma => ba_dma,
	ba_reu => ba_reu,
	char_rom_bank => not (vicAddr(14) or (exrom and not game)),

	mode6569 => (not ntscMode),
	mode6567old => '0',
	mode6567R8 => ntscMode,
	mode6572 => '0',
	variant => vic_variant,

	turbo_en => turbo_en,
	turbo_state => turbo_state,

	cs => cs_vic,
	read_early_active => dma_active and dma_slot_peek,
	read_early => dma_peek_io and not memory_wait_q,
	we => cpuWe,
	lp_n => cia1_pbi(4),

	aRegisters => cpuAddr(5 downto 0),
	diRegisters => cpuDo,
	di => vicDiAec,
	diColor => colorDataAec,
	do => vicData,

	vicAddr => vicAddr(13 downto 0),
	addrValid => aec,

	hsync => hSync,
	vsync => vSync,
	colorIndex => vicColorIndex,

	irq_n => irq_vic
);

c64colors: entity work.fpga64_rgbcolor
port map (
	palette => palette,
	index => vicColorIndex,
	r => r,
	g => g,
	b => b
);

process(clk32)
begin
	if rising_edge(clk32) then
		if sysCycle = CYCLE_VIC3 and memory_wait_q = '0' then
			lastVicDi <= vicDi;
		end if;
	end if;
end process;

-- VIC bank to address lines
--
-- The glue logic on a C64C will generate a glitch during 10 <-> 01
-- generating 00 (in other words, bank 3) for one cycle.
--
-- When using the data direction register to change a single bit 0->1
-- (in other words, decreasing the video bank number by 1 or 2),
-- the bank change is delayed by one cycle. This effect is unstable.
process(clk32)
begin
	if rising_edge(clk32) then
		if phi0_cpu = '0' and enableVic = '1' and memory_wait_q = '0' then
			vicAddr1514 <= not cia2_pao(1 downto 0);
		end if;
	end if;
end process;

-- emulate only the first glitch (enough for Undead from Emulamer)
vicAddr(15 downto 14) <= "11" when ((vicAddr1514 xor not cia2_pao(1 downto 0)) = "11") and (cia2_pao(0) /= cia2_pao(1)) else not unsigned(cia2_pao(1 downto 0));

-- Pixel timing
process(clk32)
begin
	if rising_edge(clk32) then
		if memory_wait_q = '0' then
			enablePixel <= '0';
			if sysCycle = CYCLE_VIC2
			or sysCycle = CYCLE_EXT2
			or sysCycle = CYCLE_DMA2
			or sysCycle = CYCLE_EXT6
			or sysCycle = CYCLE_CPU2
			or sysCycle = CYCLE_CPU6
			or sysCycle = CYCLE_CPUA
			or sysCycle = CYCLE_CPUE then
				enablePixel <= '1';
			end if;
		end if;
	end if;
end process;

-- -----------------------------------------------------------------------
-- SID
-- -----------------------------------------------------------------------

--	Right SID Port: Same,DE00,D420,D500,DF00

sid_sel_l <= cs_sid when sid_mode(2 downto 1) /= 1 else (cs_sid and ((not sid_mode(0) and not cpuAddr(5)) or (sid_mode(0) and not cpuAddr(8))));
sid_sel_r <= sid_sel_l when sid_mode = 0 else ioe_i when sid_mode = 1 else iof_i when sid_mode = 4 else (cs_sid and not sid_sel_l);
io_data_i <= io_data when io_ext = '1' else sid_do when sid_sel_r = '1' else (others => '1');

pot_x1 <= (others => '1' ) when cia1_pao(6) = '0' else not pot1;
pot_y1 <= (others => '1' ) when cia1_pao(6) = '0' else not pot2;
pot_x2 <= (others => '1' ) when cia1_pao(7) = '0' else not pot3;
pot_y2 <= (others => '1' ) when cia1_pao(7) = '0' else not pot4;

sid : sid_top
port map (
	reset => reset,
	clk => clk32,
	ce_1m => enableSid and not memory_wait_q,
	we => pulseWr_io,
	cs => sid_sel_r & sid_sel_l,
	addr => cpuAddr(4 downto 0),
	data_in => cpuDo,
	data_out => sid_do,
	pot_x_l => pot_x1 and pot_x2,
	pot_y_l => pot_y1 and pot_y2,
	-- The dual-SID socket adapter connects paddles only to the first chip.
	-- MAME's unconnected second SID potentiometer inputs read $FF.
	pot_x_r => (others => '1'),
	pot_y_r => (others => '1'),

	audio_l => audio_l,
	audio_r => audio_r,

	ext_in_l(17) => sid_ver(0) and sid_digifix,
	ext_in_l(16 downto 0) => (others => '0'),

	ext_in_r(17) => sid_ver(1) and sid_digifix,
	ext_in_r(16 downto 0) => (others => '0'),

	filter_en => sid_filter,
	mode    => sid_ver,
	cfg     => sid_cfg,

	fc_offset_l => sid_fc_off_l,
	fc_offset_r => sid_fc_off_r,

	ld_clk  => sid_ld_clk,
	ld_addr => sid_ld_addr,
	ld_data => sid_ld_data,
	ld_wr   => sid_ld_wr
);

-- -----------------------------------------------------------------------
-- CIAs
-- -----------------------------------------------------------------------
cia1: mos6526
port map (
	clk => clk32,
	mode => cia_mode,
	phi2_p => enableCia_p and not memory_wait_q,
	phi2_n => (enableCia_n or dma_peek_io) and not memory_wait_q,
	res_n => not reset,
	cs_n => not cs_cia1,
	rw => not cpuWe,

	rs => cpuAddr(3 downto 0),
	db_in => cpuDo,
	db_out => cia1Do,

	pa_in => cia1_pai,
	pa_out => cia1_pao,
	pb_in => cia1_pbi,
	pb_out => cia1_pbo,

	flag_n => cass_read,
	sp_in => sp1_i,
	sp_out => sp1_o,
	cnt_in => cnt1_i,
	cnt_out => cnt1_o,

	tod => todclk,

	irq_n => irq_cia1
);

cia2: mos6526
port map (
	clk => clk32,
	mode => cia_mode,
	phi2_p => enableCia_p and not memory_wait_q,
	phi2_n => (enableCia_n or dma_peek_io) and not memory_wait_q,
	res_n => not reset,
	cs_n => not cs_cia2,
	rw => not cpuWe,

	rs => cpuAddr(3 downto 0),
	db_in => cpuDo,
	db_out => cia2Do,

	-- Multitini local-port extension: an input-direction port A bit reads
	-- its pin regardless of the output register, so a drive strobe level on
	-- PA2 survives read-modify-write of $DD00. Output-direction bits keep the
	-- pinned open-collector emulation.
	pa_in => cia2_pai and (cia2_pao or not cia2_pae),
	pa_out => cia2_pao,
	pa_oe => cia2_pae,
	pb_in => (pb_i and not cia2_pbe) or (cia2_pbo and cia2_pbe),
	pb_out => cia2_pbo,
	pb_oe => cia2_pbe,

	flag_n => flag2_n_i,
	pc_n => pc2_n_o,

	sp_in => sp2_i,
	sp_out => sp2_o,
	cnt_in => cnt2_i,
	cnt_out => cnt2_o,

	tod => todclk,

	irq_n => irq_cia2
);

serialBus: process(clk32)
begin
	if rising_edge(clk32) then
		if sysCycle = CYCLE_EXT5 then
			cia2_pai(7) <= iec_data_i and not cia2_pao(5);
			cia2_pai(6) <= iec_clk_i and not cia2_pao(4);
		end if;
	end if;
end process;

cia2_pai(5 downto 0) <= "111" & pa2_i & "11";

iec_data_o <= not cia2_pao(5);
iec_clk_o  <= not cia2_pao(4);
iec_atn_o  <= not cia2_pao(3);

pb_o  <= cia2_pbo;
pb_oe_o <= cia2_pbe;
pa2_o <= cia2_pao(2);

-- CIA time-of-day must pause with CPU/VIC/CIA enables. Hold both the
-- fractional divider and its output level so resume cannot create an edge.
tod_clock: process(clk32)
variable sum: integer range 0 to 33000000;
begin
	if rising_edge(clk32) then
		if reset = '1' then
			todclk <= '0';
			sum := 0;
		elsif sysEnable = '1' then
			if ntscMode = '1' then
				sum := sum + 120;
				if sum >= 32727266 then
					sum := sum - 32727266;
					todclk <= not todclk;
				end if;
			else
				sum := sum + 100;
				if sum >= 31527954 then
					sum := sum - 31527954;
					todclk <= not todclk;
				end if;
			end if;
		end if;
	end if;
end process;

-- -----------------------------------------------------------------------
-- 6510 CPU / DMA
-- -----------------------------------------------------------------------
cpu: entity work.cpu_6510
generic map (SEPARATE_BUS_START => true)
port map (
	clk => clk32,
	reset => reset,
	enable => enableCpu and not dma_active and not memory_wait_q,
	-- Resolve a prefetched interrupt before issuing its next bus request.
	-- Native RDY-held attempts still start; DMA and external holds do not.
	bus_start => cpu_cyc and not dma_active and not memory_wait_q,
	nmi_n => irq_cia2 and nmi_n,
	nmi_ack => nmi_ack,
	irq_n => irq_cia1 and irq_vic and irq_n and irq_ext_n,
	rdy => baLoc,

	di => cpuDi,
	addr => cpuAddr_pre,
	do => cpuDo_pre,
	we => cpuWe_pre,

	-- Physical external inputs only. The port model owns pull-ups, retained
	-- drive and native-time falloff; do not feed its outputs back as inputs.
	diIO => "000" & cass_sense & "0111",
	port_tick => enableSid and not memory_wait_q,
	doIO => cpuIO
);

cass_motor <= cpuIO(5);
cass_write <= cpuIO(3);

-- CPU port writes put the VIC floating bus byte into backing RAM. VICE's
-- zero_store_dma instead stores the DMA payload without touching port pins.
ramDout <= lastVicDi when systemWe = '1' and systemAddr(15 downto 1) = 0 and dma_active = '0'
	else cpuDo;
ramAddr <= systemAddr;
ramWE   <= systemWe when sysCycle >= CYCLE_CPU0 else '0';
-- DMA readiness gates only the CPU/DMA owner. VIC phi2 fetches must still
-- reach local RAM while the REU is waiting for its expansion-memory data.
ramCE   <= cs_ram and not memory_wait_q when
				sysCycle = CYCLE_VIC0 or
				(cpu_cyc = '1' and (cpuHasBus = '0' or bus_access_valid = '1')) else '0';

bus_access_valid <= not dma_active or (dma_slot_valid and not dma_slot_idle and not dma_peek_done);
dma_ack <= enableCpu and dma_active and dma_slot_valid and cpuHasBus and not memory_wait_q and not dma_peek_done;
-- Only a final verify peek uses this extra slot. It must never issue a
-- write, or advance CPU, VIC, CIA timer or SID native clocks.
dma_peek_issue <= '1' when sysCycle = CYCLE_CPU0 and dma_active = '1' and
	dma_slot_valid = '1' and dma_slot_peek = '1' and dma_peek_done = '0' and cpuHasBus = '1' else '0';
dma_peek_io <= '1' when sysCycle = CYCLE_CPU1 and dma_active = '1' and
	dma_slot_valid = '1' and dma_slot_peek = '1' and dma_peek_done = '0' and cpuHasBus = '1' else '0';
dma_peek_retire <= '1' when sysCycle = CYCLE_CPU3 and dma_active = '1' and
	dma_slot_valid = '1' and dma_slot_peek = '1' and dma_peek_done = '0' and cpuHasBus = '1' else '0';
cpu_cyc <= scheduledCpuRequest or dma_peek_issue;
enableCpu <= scheduledCpuRetire or dma_peek_retire;
-- Register side effects occur once on the completed CPU transaction. DMA
-- accesses cannot retrigger the REU's own command or delayed $FF00 trigger.
cpu_bus_valid <= enableCpu and not dma_active and cpuHasBus and not memory_wait_q;
cpu_bus_addr <= cpuAddr_pre;
cpu_bus_wdata <= cpuDo_pre;
cpu_bus_write <= cpuWe_pre;

-- $0000/$0001 are native-cycle barriers: complete the 6510 port-register
-- update before admitting another accelerated request through its mapping.
-- VIC and DMA ownership take precedence over extra local CPU opportunities.
cpu_extra_allowed <= local_turbo_allowed and cpuHasBus and not dma_active
	when cpuAddr_pre(15 downto 1) /= 0 else '0';

cpu_scheduler: entity work.multitini_c64_cpu_scheduler
port map (
	clk => clk32,
	reset => reset,
	hold => memory_wait_q,
	phase => to_unsigned(sysCycleDef'pos(sysCycle), 5),
	memory_access => cs_ram,
	io_enable => io_enable,
	extra_mask => turbo_m,
	dense_mode => turbo_dense,
	extra_allowed => cpu_extra_allowed,
	request => scheduledCpuRequest,
	retire => scheduledCpuRetire
);

process(clk32)
begin
	if rising_edge(clk32) then
		if memory_wait_q = '0' then
			io_enable <= io_enable and not scheduledCpuRetire;

			if sysCycle = CYCLE_EXT0 then
				io_enable <= '1';
			end if;

			-- 2 points to register DMA request before CPU cycles.
			if sysCycle = CYCLE_EXT1 or sysCycle = CYCLE_EXT5 then
				turbo_en <= turbo_mode(0);
				turbo_m <= "000";
				turbo_dense <= '0';
				if cs_io = '0' and dma_req = '0' and ((turbo_mode(0) and turbo_state) = '1' or turbo_mode(1) = '1') then
					case turbo_speed is
						when "00" => turbo_m <= "010";
						when "01" => turbo_m <= "110";
						when "10" => turbo_m <= "111";
						when "11" => turbo_dense <= '1';
						when others => turbo_m <= "000";
					end case;
				end if;
			end if;
		end if;
	end if;
end process;

cpuAddr <= cpuAddr_pre when dma_active = '0' else dma_addr;
cpuDo   <= cpuDo_pre   when dma_active = '0' else dma_dout;
cpuWe   <= cpuWe_pre   when dma_active = '0' else dma_we;

ext_cycle <= '1' when (sysCycle >= CYCLE_DMA0 and sysCycle <= CYCLE_DMA3) else '0';
dma_cycle <= '1' when (sysCycle >= CYCLE_CPU0 and sysCycle <= CYCLE_CPUF) and cpuHasBus = '1' and dma_active = '1' else '0';
dma_din   <= cpuDi;

-- -----------------------------------------------------------------------
-- Keyboard
-- -----------------------------------------------------------------------
Keyboard: entity work.fpga64_keyboard
port map (
	clk => clk32,

	reset => kbd_reset,
	ps2_key => ps2_key,

	joyA => not unsigned(joyA(6 downto 0)),
	joyB => not unsigned(joyB(6 downto 0)),
	pai => cia1_pao,
	pbi => cia1_pbo,
	pao => cia1_pai,
	pbo => cia1_pbi,

	shift_mod => shift_mod,

	restore_key => freeze_key,
	tape_play => tape_play,
	tape_rew => tape_rew,
	tape_stop => tape_stop,
	tape_ff => tape_ff,
	tape_reset_counter => tape_reset_counter,
	mod_key => mod_key,
	backwardsReadingEnabled => '1'
);

end architecture;
