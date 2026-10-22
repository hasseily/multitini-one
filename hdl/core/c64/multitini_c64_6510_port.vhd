-- SPDX-License-Identifier: GPL-3.0-only
-- 6510 port behavior follows pinned MAME m6510.cpp. Native ticks measure
-- elapsed C64 time, including CPU RDY holds and independent of turbo slots.
-- Reference: mamedev/mame 122719e8d13e2fdb7dd33886455484207de59fc8,
-- src/devices/cpu/m6502/m6510.cpp, BSD-3-Clause,
-- copyright-holders: Olivier Galibert. Original notice and license are in
-- third_party/mame_reference/upstream/ and its NOTICE.md.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity multitini_c64_6510_port is
    generic (
        PULLUP : unsigned(7 downto 0) := x"17";
        FLOATING : unsigned(7 downto 0) := x"C8";
        FALLOFF_MASK : unsigned(7 downto 0) := x"C0";
        FALLOFF_TICKS : positive := 350000
    );
    port (
        clk : in std_logic;
        reset : in std_logic;
        write_enable : in std_logic;
        register_select : in std_logic;
        write_data : in unsigned(7 downto 0);
        external_input : in unsigned(7 downto 0);
        decay_tick : in std_logic;
        read_data : out unsigned(7 downto 0);
        output_pins : out unsigned(7 downto 0)
    );
end entity;

architecture rtl of multitini_c64_6510_port is
    signal direction_q : unsigned(7 downto 0);
    signal data_q : unsigned(7 downto 0);
    signal drive_q : unsigned(7 downto 0);
    type timers_t is array(0 to 7) of natural range 0 to FALLOFF_TICKS;
    signal timers : timers_t;
begin
    output_pins <= (data_q and direction_q) or (PULLUP and not direction_q);

    process(all)
        variable floating_bits : unsigned(7 downto 0);
    begin
        floating_bits := FLOATING and drive_q;
        for bit_index in 0 to 7 loop
            if FALLOFF_MASK(bit_index) = '1' and timers(bit_index) = 0 then
                floating_bits(bit_index) := '0';
            end if;
        end loop;
        if register_select = '0' then
            read_data <= direction_q;
        else
            read_data <= ((external_input or floating_bits) and not direction_q)
                         or (data_q and direction_q);
        end if;
    end process;

    process(clk)
        variable next_direction : unsigned(7 downto 0);
        variable next_data : unsigned(7 downto 0);
    begin
        if rising_edge(clk) then
            if reset = '1' then
                direction_q <= x"FF";
                data_q <= x"FF";
                drive_q <= x"FF";
                timers <= (others => 0);
            else
                for bit_index in 0 to 7 loop
                    if FALLOFF_MASK(bit_index) = '1' and decay_tick = '1' and timers(bit_index) > 0 then
                        timers(bit_index) <= timers(bit_index) - 1;
                    end if;
                end loop;
                if write_enable = '1' then
                    next_direction := direction_q;
                    next_data := data_q;
                    if register_select = '0' then
                        next_direction := write_data;
                        for bit_index in 0 to 7 loop
                            if FALLOFF_MASK(bit_index) = '1' and direction_q(bit_index) = '1' and write_data(bit_index) = '0' then
                                timers(bit_index) <= FALLOFF_TICKS;
                            end if;
                        end loop;
                    else
                        next_data := write_data;
                    end if;
                    direction_q <= next_direction;
                    data_q <= next_data;
                    drive_q <= (next_data and next_direction) or (drive_q and not next_direction);
                end if;
            end if;
        end if;
    end process;
end architecture;
