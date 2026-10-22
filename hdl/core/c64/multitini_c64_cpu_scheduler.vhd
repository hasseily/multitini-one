-- SPDX-License-Identifier: GPL-3.0-only
-- CPU bus transactions within FPGA64's unchanged 32-clock machine frame.
-- Issue owns one synchronous-memory request; retire advances the 6510 only
-- after that request's response is available. A memory wait freezes pending
-- retirements and suppresses both strobes, rather than reissuing a write.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity multitini_c64_cpu_scheduler is
    port (
        clk           : in  std_logic;
        reset         : in  std_logic;
        hold          : in  std_logic;
        phase         : in  unsigned(4 downto 0);
        memory_access : in  std_logic;
        io_enable     : in  std_logic;
        extra_mask    : in  std_logic_vector(2 downto 0);
        dense_mode    : in  std_logic;
        extra_allowed : in  std_logic;
        request       : out std_logic;
        retire        : out std_logic
    );
end entity;

architecture rtl of multitini_c64_cpu_scheduler is
    signal issue_native : std_logic;
    signal issue_extra  : std_logic;
    signal issue_fast   : std_logic;
    signal issue_slow   : std_logic;
    signal pending_slow : std_logic_vector(2 downto 0) := (others => '0');
    signal pending_fast : std_logic := '0';
    signal bus_idle     : std_logic;
begin
    -- Keep one accepted transaction in flight. Production controls change
    -- outside the CPU half-frame; this also makes a late mode change defer
    -- an opportunity instead of merging two retirements into one pulse.
    bus_idle <= '1' when pending_slow = "000" and pending_fast = '0' else '0';

    -- The native transaction keeps the original CPUC -> CPUF phases. This
    -- includes I/O, DMA and cycles where VIC BA holds the CPU's RDY low.
    issue_native <= '1' when bus_idle = '1' and phase = 28 and
        (io_enable = '1' or memory_access = '1') else '0';

    -- Standard upstream modes retain four-clock slots. The internal dense
    -- mode uses six local-only two-clock slots, then the native slot. It is
    -- seven bus opportunities per frame, not a change to the machine clock.
    issue_extra <= '1' when bus_idle = '1' and memory_access = '1' and
        extra_allowed = '1' and
        ((dense_mode = '1' and
          (phase = 16 or phase = 18 or phase = 20 or phase = 22 or
           phase = 24 or phase = 26)) or
         (dense_mode = '0' and
          ((phase = 16 and extra_mask(0) = '1') or
           (phase = 20 and extra_mask(1) = '1') or
           (phase = 24 and extra_mask(2) = '1')))) else '0';

    issue_fast <= issue_extra and dense_mode;
    issue_slow <= issue_native or (issue_extra and not dense_mode);
    request <= (issue_fast or issue_slow) and not hold and not reset;
    retire <= (pending_fast or pending_slow(2)) and not hold and not reset;

    process(clk)
    begin
        if rising_edge(clk) then
            if reset = '1' then
                pending_slow <= (others => '0');
                pending_fast <= '0';
            elsif hold = '0' then
                pending_slow <= pending_slow(1 downto 0) & issue_slow;
                pending_fast <= issue_fast;
            end if;
        end if;
    end process;
end architecture;
