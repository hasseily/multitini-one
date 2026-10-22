-- SPDX-License-Identifier: GPL-2.0-or-later
-- Multitini compatibility port of MiSTer C64 commit
-- 62fc697e3c9b5982c9dc76aa8cd03db545d20803. The T65 instance uses direct
-- entity instantiation so standards-based VHDL tools do not require a missing
-- component declaration. The local port models MAME C64 reset, pullups,
-- retained input charge and native-time decay independently of CPU turbo.
-- BEGIN PINNED SOURCE BODY
-- -----------------------------------------------------------------------
--
--                                 FPGA 64
--
--     A fully functional commodore 64 implementation in a single FPGA
--
-- -----------------------------------------------------------------------
-- Copyright 2005-2008 by Peter Wendrich (pwsoft@syntiac.com)
-- http://www.syntiac.com/fpga64.html
-- -----------------------------------------------------------------------
--
-- 6510 wrapper for 65xx core
-- Adds 8 bit I/O port mapped at addresses $0000 to $0001
--
-- -----------------------------------------------------------------------

library IEEE;
use ieee.std_logic_1164.ALL;
use ieee.numeric_std.ALL;

-- -----------------------------------------------------------------------

entity cpu_6510 is
	generic (
		PULLUP        : unsigned(7 downto 0) := x"17";
		FLOATING      : unsigned(7 downto 0) := x"C8";
		FALLOFF_MASK  : unsigned(7 downto 0) := x"C0";
		FALLOFF_TICKS : positive := 350000
	);
	port (
		clk     : in  std_logic;
		enable  : in  std_logic;
		reset   : in  std_logic;
		nmi_n   : in  std_logic;
		nmi_ack : out std_logic;
		irq_n   : in  std_logic;
		rdy     : in  std_logic;
		port_tick : in std_logic := '0';

		di      : in  unsigned(7 downto 0);
		do      : out unsigned(7 downto 0);
		addr    : out unsigned(15 downto 0);
		we      : out std_logic;

		diIO    : in  unsigned(7 downto 0);
		doIO    : out unsigned(7 downto 0)
	);
end cpu_6510;

-- -----------------------------------------------------------------------

architecture rtl of cpu_6510 is
	signal localA : std_logic_vector(23 downto 0);
	signal localDi : std_logic_vector(7 downto 0);
	signal localDo : std_logic_vector(7 downto 0);
	signal localWe : std_logic;

	signal portRead : unsigned(7 downto 0);

	signal accessIO : std_logic;
begin

	cpu: entity work.T65
	port map(
		Mode    => "00",
		Res_n   => not reset,
		Enable  => enable,
		Clk     => clk,
		Rdy     => rdy,
		Abort_n => '1',
		IRQ_n   => irq_n,
		NMI_n   => nmi_n,
		SO_n    => '1',
		R_W_n   => localWe,
		A       => localA,
		DI      => localDi,
		DO      => localDo,
		NMI_ack => nmi_ack
	);

	accessIO <= '1' when localA(15 downto 1) = X"000"&"000" else '0';
	localDi <= localDo when localWe = '0' else std_logic_vector(di)
		when accessIO = '0' else std_logic_vector(portRead);

	io_port: entity work.multitini_c64_6510_port
	generic map (
		PULLUP => PULLUP, FLOATING => FLOATING,
		FALLOFF_MASK => FALLOFF_MASK, FALLOFF_TICKS => FALLOFF_TICKS
	)
	port map (
		clk => clk, reset => reset,
		write_enable => accessIO and not localWe and enable,
		register_select => localA(0), write_data => unsigned(localDo),
		external_input => diIO, decay_tick => port_tick,
		read_data => portRead, output_pins => doIO
	);

	-- Cunnect zee wires
	addr <= unsigned(localA(15 downto 0));
	do <= unsigned(localDo);
	we <= not localWe;
end architecture;
