-- SPDX-License-Identifier: GPL-3.0-only
-- Original flattened test boundary; sampler2 and its dependencies are exact
-- test-only GPL reference imports at 1541ultimate fea2798c.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
entity c64_ua_upstream_bridge is
    generic (g_clock_freq : natural := 100000000);
    port (clock, reset, io_write, io_read : in std_logic;
          io_address, io_wdata : in std_logic_vector(7 downto 0);
          io_rdata : out std_logic_vector(7 downto 0);
          io_ack, irq, memory_request : out std_logic;
          memory_address : out std_logic_vector(25 downto 0);
          memory_tag : out std_logic_vector(7 downto 0);
          sample_l, sample_r : out std_logic_vector(17 downto 0);
          new_sample : out std_logic);
end entity;
architecture test of c64_ua_upstream_bridge is
    signal req : t_io_req := c_io_req_init;
    signal resp : t_io_resp;
    signal mreq : t_mem_req;
    signal mresp : t_mem_resp := c_mem_resp_init;
    signal left_value, right_value : signed(17 downto 0);
begin
    req.read <= io_read;
    req.write <= io_write;
    req.address <= resize(unsigned(io_address),24);
    req.data <= io_wdata;
    io_rdata <= resp.data;
    io_ack <= resp.ack;
    memory_request <= mreq.request;
    memory_address <= std_logic_vector(mreq.address);
    memory_tag <= mreq.tag;
    sample_l <= std_logic_vector(left_value);
    sample_r <= std_logic_vector(right_value);
    mresp.rack <= mreq.request;
    mresp.rack_tag <= mreq.tag;
    process(clock)
    begin
        if rising_edge(clock) then
            mresp.dack_tag <= X"00";
            if mreq.request='1' then
                mresp.dack_tag <= mreq.tag;
                mresp.data <= std_logic_vector(mreq.address(7 downto 0) xor mreq.address(15 downto 8)) xor X"A6";
            end if;
        end if;
    end process;
    reference: entity work.sampler2
        generic map(g_clock_freq=>g_clock_freq,g_support_16bit=>true,g_num_voices=>8)
        port map(clock=>clock,reset=>reset,io_req=>req,io_resp=>resp,
            mem_req=>mreq,mem_resp=>mresp,irq=>irq,sample_L=>left_value,
            sample_R=>right_value,new_sample=>new_sample);
end architecture;
