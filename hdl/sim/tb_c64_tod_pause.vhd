-- SPDX-License-Identifier: GPL-3.0-only
-- Both instances use the verbatim production divider. The reference receives
-- only active machine clocks; the DUT keeps receiving clocks while paused.
-- Equal outputs after every edge prove both output and fractional phase hold.
library ieee;
use ieee.std_logic_1164.all;
use std.env.all;

entity tb_c64_tod_pause is
end entity;

architecture test of tb_c64_tod_pause is
    signal clk : std_logic := '0';
    signal reference_clk : std_logic := '0';
    signal reset : std_logic := '1';
    signal ntsc_mode : std_logic := '0';
    signal enabled : std_logic := '0';
    signal tod : std_logic;
    signal reference_tod : std_logic;
begin
    dut : entity work.c64_tod_test
        port map(clk32 => clk, reset => reset, ntscMode => ntsc_mode,
                 sysEnable => enabled, todclk => tod);
    reference : entity work.c64_tod_test
        port map(clk32 => reference_clk, reset => reset,
                 ntscMode => ntsc_mode, sysEnable => '1',
                 todclk => reference_tod);

    stimulus : process
        procedure cycle(advance_reference : boolean) is
        begin
            clk <= '1';
            if advance_reference then reference_clk <= '1'; end if;
            wait for 1 ns;
            assert tod = reference_tod
                report "FAIL: paused TOD differs from active-clock reference"
                severity failure;
            clk <= '0';
            reference_clk <= '0';
            wait for 1 ns;
        end procedure;

        procedure cycles(count : positive; advance_reference : boolean) is
        begin
            for i in 1 to count loop cycle(advance_reference); end loop;
        end procedure;
    begin
        for mode in 0 to 1 loop
            if mode = 0 then ntsc_mode <= '0';
            else ntsc_mode <= '1'; end if;
            reset <= '1';
            enabled <= '0';
            cycles(2, true);
            assert tod = '0' report "FAIL: paused reset did not clear TOD"
                severity failure;
            reset <= '0';
            enabled <= '1';
            -- Preserve a nonzero fractional remainder through a pause longer
            -- than a complete PAL or NTSC TOD period, first with output low.
            cycles(12345, true);
            enabled <= '0';
            cycles(700000, false);
            enabled <= '1';
            cycles(350000, true);
            assert tod = '1' report "FAIL: resumed TOD did not advance"
                severity failure;
            -- Repeat with output high: forcing low during pause would create
            -- an extra CIA edge, even if the divider accumulator were held.
            enabled <= '0';
            cycles(700000, false);
            enabled <= '1';
            cycles(700000, true);
            -- Reset must still take effect while paused.
            enabled <= '0';
            reset <= '1';
            cycles(2, true);
            assert tod = '0' report "FAIL: reset during pause was ignored"
                severity failure;
            reset <= '0';
            cycles(100, false);
            enabled <= '1';
            cycles(350000, true);
        end loop;
        report "PASS: C64 PAL and NTSC TOD pause preserves phase and level";
        finish;
        wait;
    end process;
end architecture;
