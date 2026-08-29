-- SPDX-License-Identifier: GPL-2.0-or-later
-- Multitini standards-compatible port of the MiSTer C64 dprom entity.
-- No initialization file is consumed; all product ROM data is written at run time.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity dprom is
    generic (
        INIT_FILE  : string := "";
        ADDR_WIDTH : natural := 14;
        DATA_WIDTH : natural := 8
    );
    port (
        wrclock   : in  std_logic;
        wraddress : in  std_logic_vector(ADDR_WIDTH - 1 downto 0) := (others => '0');
        data      : in  std_logic_vector(DATA_WIDTH - 1 downto 0) := (others => '0');
        wren      : in  std_logic := '0';
        rdclock   : in  std_logic;
        rdaddress : in  std_logic_vector(ADDR_WIDTH - 1 downto 0);
        q         : out std_logic_vector(DATA_WIDTH - 1 downto 0);
        cs        : in  std_logic := '1'
    );
end entity;

architecture rtl of dprom is
    subtype word_t is std_logic_vector(DATA_WIDTH - 1 downto 0);
    type memory_t is array (0 to (2 ** ADDR_WIDTH) - 1) of word_t;
    signal ram : memory_t;
    signal q0  : word_t;
    attribute ram_style : string;
    attribute ram_style of ram : signal is "block";
begin
    -- Keep the upstream generic for entity compatibility, but deliberately do
    -- not attach INIT_FILE to the memory inference.
    q <= q0 when cs = '1' else (others => '1');

    process (wrclock)
    begin
        if rising_edge(wrclock) and wren = '1' then
            ram(to_integer(unsigned(wraddress))) <= data;
        end if;
    end process;

    process (rdclock)
    begin
        if rising_edge(rdclock) then
            q0 <= ram(to_integer(unsigned(rdaddress)));
        end if;
    end process;

    assert INIT_FILE = ""
        report "Multitini dprom ignores non-empty INIT_FILE"
        severity failure;
end architecture;
